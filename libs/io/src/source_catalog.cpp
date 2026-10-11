#include <av/io/source_catalog.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace av {
namespace {

void requireNonEmptyString(const nlohmann::json& json, const char* field) {
  if (!json.contains(field) || !json.at(field).is_string() ||
      json.at(field).get<std::string>().empty()) {
    throw std::runtime_error(std::string("expected non-empty string '") + field +
                             "'");
  }
}

SourceCatalogDirectivity directivityFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected a directivity object");
  }
  SourceCatalogDirectivity directivity;
  directivity.type = json.at("type").get<std::string>();
  if (directivity.type != "omni" && directivity.type != "dipole" &&
      directivity.type != "cardioid" && directivity.type != "custom") {
    throw std::runtime_error("directivity.type must be omni|dipole|cardioid|custom");
  }
  directivity.description = json.value("description", std::string{});
  return directivity;
}

SourceCatalogSpectrum spectrumFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected a spectrum object");
  }
  SourceCatalogSpectrum spectrum;
  spectrum.bandsHz = json.at("bandsHz").get<std::vector<double>>();
  spectrum.responseDb = json.at("responseDb").get<std::vector<double>>();
  if (spectrum.bandsHz.empty()) {
    throw std::runtime_error("spectrum.bandsHz must be non-empty");
  }
  if (spectrum.bandsHz.size() != spectrum.responseDb.size()) {
    throw std::runtime_error(
        "spectrum.bandsHz and spectrum.responseDb must have the same length");
  }
  return spectrum;
}

SourceCatalogEntry entryFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected a catalog entry object");
  }
  requireNonEmptyString(json, "id");
  requireNonEmptyString(json, "name");
  requireNonEmptyString(json, "disclaimer");
  if (!json.contains("powerDb") || !json.at("powerDb").is_number()) {
    throw std::runtime_error("expected number 'powerDb'");
  }

  SourceCatalogEntry entry;
  entry.id = json.at("id").get<std::string>();
  entry.name = json.at("name").get<std::string>();
  entry.manufacturer = json.value("manufacturer", std::string{});
  entry.category = json.value("category", std::string{});
  entry.powerDb = json.at("powerDb").get<double>();
  entry.spectrum = spectrumFromJson(json.at("spectrum"));
  entry.directivity = directivityFromJson(json.at("directivity"));
  entry.notes = json.value("notes", std::string{});
  entry.sourceUrl = json.value("sourceUrl", std::string{});
  entry.disclaimer = json.at("disclaimer").get<std::string>();
  return entry;
}

}  // namespace

SourceCatalog sourceCatalogFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected a catalog object");
  }
  requireNonEmptyString(json, "disclaimer");
  if (!json.contains("sources") || !json.at("sources").is_array()) {
    throw std::runtime_error("expected array 'sources'");
  }

  SourceCatalog catalog;
  catalog.schemaVersion = json.value("schemaVersion", 1);
  catalog.disclaimer = json.at("disclaimer").get<std::string>();
  for (const nlohmann::json& item : json.at("sources")) {
    catalog.sources.push_back(entryFromJson(item));
  }
  return catalog;
}

SourceCatalog loadSourceCatalogFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open source catalog: " + path.string());
  }
  nlohmann::json json;
  try {
    in >> json;
  } catch (const std::exception& error) {
    throw std::runtime_error("invalid source catalog JSON in " + path.string() +
                             ": " + error.what());
  }
  return sourceCatalogFromJson(json);
}

nlohmann::json sourceCatalogEntryToJson(const SourceCatalogEntry& entry) {
  nlohmann::json json;
  json["id"] = entry.id;
  json["name"] = entry.name;
  if (!entry.manufacturer.empty()) {
    json["manufacturer"] = entry.manufacturer;
  }
  if (!entry.category.empty()) {
    json["category"] = entry.category;
  }
  json["powerDb"] = entry.powerDb;
  json["spectrum"] = {{"bandsHz", entry.spectrum.bandsHz},
                      {"responseDb", entry.spectrum.responseDb}};
  json["directivity"] = {{"type", entry.directivity.type}};
  if (!entry.directivity.description.empty()) {
    json["directivity"]["description"] = entry.directivity.description;
  }
  if (!entry.notes.empty()) {
    json["notes"] = entry.notes;
  }
  if (!entry.sourceUrl.empty()) {
    json["sourceUrl"] = entry.sourceUrl;
  }
  json["disclaimer"] = entry.disclaimer;
  return json;
}

nlohmann::json sourceCatalogToJson(const SourceCatalog& catalog) {
  nlohmann::json json;
  json["disclaimer"] = catalog.disclaimer;
  json["sources"] = nlohmann::json::array();
  for (const SourceCatalogEntry& entry : catalog.sources) {
    json["sources"].push_back(sourceCatalogEntryToJson(entry));
  }
  return json;
}

std::optional<std::filesystem::path> resolveSourceCatalogPath(
    const std::filesystem::path& exeDir) {
  if (const char* overridePath = std::getenv("AV_SOURCE_CATALOG");
      overridePath != nullptr && *overridePath != '\0') {
    return std::filesystem::path(overridePath);
  }
  if (exeDir.empty()) {
    return std::nullopt;
  }
  return exeDir / "source_catalog.json";
}

SourceCatalog loadDefaultSourceCatalog(const std::filesystem::path& exeDir) {
  SourceCatalog empty;
  empty.disclaimer = kExampleSpecsDisclaimer;

  const std::optional<std::filesystem::path> path =
      resolveSourceCatalogPath(exeDir);
  if (!path.has_value()) {
    std::cerr << "warning: source catalog path unresolved; "
                 "GET /v1/source-catalog will return an empty list\n";
    return empty;
  }
  try {
    return loadSourceCatalogFile(*path);
  } catch (const std::exception& error) {
    std::cerr << "warning: " << error.what()
              << "; GET /v1/source-catalog will return an empty list\n";
    return empty;
  }
}

}  // namespace av
