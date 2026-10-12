#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/scenario/directivity.hpp>

namespace av {

// Built-in source catalog (read-only API data, docs/idea.md decision 6).
// Cards are fact-shaped placeholders — see the required disclaimer fields
// and schemas/source-catalog.schema.json. Engines and JobManager stay free
// of this type; GUI maps a card onto a scenario Source later.

inline constexpr const char* kExampleSpecsDisclaimer =
    "example specs, not verified vendor data";

// Card directivity: analytic type plus optional horizontal/vertical polar
// curves (same layout as Source.directivity).
using SourceCatalogDirectivity = Directivity;

struct SourceCatalogSpectrum {
  // Relative level offsets (dB) against the card's broadband powerDb.
  // bandsHz.size() == responseDb.size().
  std::vector<double> bandsHz;
  std::vector<double> responseDb;
};

struct SourceCatalogEntry {
  std::string id;
  std::string name;
  std::string manufacturer;
  std::string category;
  // Rated/typical maximum SPL in dB SPL referenced to 1 m on-axis — NOT
  // electrical watts (schemas/source-catalog.schema.json).
  double powerDb = 0.0;
  SourceCatalogSpectrum spectrum;
  SourceCatalogDirectivity directivity;
  std::string notes;
  std::string sourceUrl;
  std::string disclaimer;
};

struct SourceCatalog {
  int schemaVersion = 1;
  std::string disclaimer;
  std::vector<SourceCatalogEntry> sources;
};

// Parses a catalog document (schemas/source-catalog.schema.json). Throws
// std::runtime_error on structural errors: missing/ill-typed required fields,
// empty id/name/disclaimer, spectrum length mismatch, unknown directivity
// type. Optional string fields default to "".
[[nodiscard]] SourceCatalog sourceCatalogFromJson(const nlohmann::json& json);

// File helper; throws std::runtime_error on IO or parse failure.
[[nodiscard]] SourceCatalog loadSourceCatalogFile(
    const std::filesystem::path& path);

// GET /v1/source-catalog body: {disclaimer, sources: [entries]}.
[[nodiscard]] nlohmann::json sourceCatalogToJson(const SourceCatalog& catalog);
[[nodiscard]] nlohmann::json sourceCatalogEntryToJson(
    const SourceCatalogEntry& entry);

// Resolves the catalog file path: $AV_SOURCE_CATALOG when set and non-empty,
// otherwise `exeDir / "source_catalog.json"`. Returns nullopt only when no
// env override is set and exeDir is empty.
[[nodiscard]] std::optional<std::filesystem::path> resolveSourceCatalogPath(
    const std::filesystem::path& exeDir = {});

// Startup loader (av-api). Missing/empty exeDir → still consults
// $AV_SOURCE_CATALOG; unreadable/invalid/missing file is not fatal — logs a
// warning to stderr and returns a catalog with kExampleSpecsDisclaimer and
// zero sources (the endpoint answers 200 with an empty list).
[[nodiscard]] SourceCatalog loadDefaultSourceCatalog(
    const std::filesystem::path& exeDir = {});

}  // namespace av
