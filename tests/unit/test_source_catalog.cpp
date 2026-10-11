#include <gtest/gtest.h>

#include <av/io/source_catalog.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

// Minimal valid card for parse tests (inline so fixtures stay scenario-only).
nlohmann::json sampleEntry() {
  return nlohmann::json{
      {"id", "unit-test-monitor"},
      {"name", "Unit Test Monitor"},
      {"category", "studio-monitor"},
      {"powerDb", 100.0},
      {"spectrum",
       {{"bandsHz", {250.0, 500.0, 1000.0}}, {"responseDb", {0.0, 1.0, -1.0}}}},
      {"directivity", {{"type", "cardioid"}, {"description", "front"}}},
      {"disclaimer", av::kExampleSpecsDisclaimer},
  };
}

nlohmann::json sampleCatalog() {
  return nlohmann::json{
      {"schemaVersion", 1},
      {"disclaimer", av::kExampleSpecsDisclaimer},
      {"sources", nlohmann::json::array({sampleEntry()})},
  };
}

// Portable env override for resolveSourceCatalogPath tests.
class EnvOverride {
 public:
  explicit EnvOverride(const std::string& value) {
#ifdef _WIN32
    _putenv_s("AV_SOURCE_CATALOG", value.c_str());
#else
    if (value.empty()) {
      unsetenv("AV_SOURCE_CATALOG");
    } else {
      setenv("AV_SOURCE_CATALOG", value.c_str(), 1);
    }
#endif
  }
  EnvOverride(const EnvOverride&) = delete;
  EnvOverride& operator=(const EnvOverride&) = delete;
  ~EnvOverride() {
#ifdef _WIN32
    _putenv_s("AV_SOURCE_CATALOG", "");
#else
    unsetenv("AV_SOURCE_CATALOG");
#endif
  }
};

TEST(SourceCatalogTest, ParsesValidDocument) {
  const av::SourceCatalog catalog = av::sourceCatalogFromJson(sampleCatalog());
  EXPECT_EQ(catalog.schemaVersion, 1);
  EXPECT_EQ(catalog.disclaimer, av::kExampleSpecsDisclaimer);
  ASSERT_EQ(catalog.sources.size(), 1u);
  const av::SourceCatalogEntry& entry = catalog.sources.front();
  EXPECT_EQ(entry.id, "unit-test-monitor");
  EXPECT_EQ(entry.name, "Unit Test Monitor");
  EXPECT_EQ(entry.category, "studio-monitor");
  EXPECT_TRUE(entry.manufacturer.empty());
  EXPECT_DOUBLE_EQ(entry.powerDb, 100.0);
  EXPECT_EQ(entry.spectrum.bandsHz.size(), 3u);
  EXPECT_EQ(entry.spectrum.responseDb.size(), 3u);
  EXPECT_EQ(entry.directivity.type, "cardioid");
  EXPECT_EQ(entry.directivity.description, "front");
  EXPECT_FALSE(entry.disclaimer.empty());
}

TEST(SourceCatalogTest, BuiltInDataFileLoadsAndHasDisclaimer) {
  ASSERT_TRUE(fs::exists(AV_SOURCE_CATALOG_DATA))
      << "built-in catalog missing: " << AV_SOURCE_CATALOG_DATA;
  const av::SourceCatalog catalog =
      av::loadSourceCatalogFile(AV_SOURCE_CATALOG_DATA);
  EXPECT_FALSE(catalog.disclaimer.empty());
  EXPECT_GE(catalog.sources.size(), 3u);
  for (const av::SourceCatalogEntry& entry : catalog.sources) {
    EXPECT_FALSE(entry.id.empty());
    EXPECT_FALSE(entry.name.empty());
    EXPECT_FALSE(entry.disclaimer.empty());
    EXPECT_EQ(entry.spectrum.bandsHz.size(), entry.spectrum.responseDb.size());
    EXPECT_FALSE(entry.spectrum.bandsHz.empty());
    EXPECT_TRUE(entry.directivity.type == "omni" ||
                entry.directivity.type == "dipole" ||
                entry.directivity.type == "cardioid" ||
                entry.directivity.type == "custom");
  }
}

TEST(SourceCatalogTest, RejectsMissingEntryDisclaimer) {
  nlohmann::json json = sampleCatalog();
  json["sources"][0].erase("disclaimer");
  EXPECT_THROW(av::sourceCatalogFromJson(json), std::runtime_error);
}

TEST(SourceCatalogTest, RejectsMissingCatalogDisclaimer) {
  nlohmann::json json = sampleCatalog();
  json.erase("disclaimer");
  EXPECT_THROW(av::sourceCatalogFromJson(json), std::runtime_error);
}

TEST(SourceCatalogTest, RejectsSpectrumLengthMismatch) {
  nlohmann::json json = sampleCatalog();
  json["sources"][0]["spectrum"]["responseDb"] = nlohmann::json::array({0.0});
  EXPECT_THROW(av::sourceCatalogFromJson(json), std::runtime_error);
}

TEST(SourceCatalogTest, RejectsUnknownDirectivityType) {
  nlohmann::json json = sampleCatalog();
  json["sources"][0]["directivity"]["type"] = "hypercardioid";
  EXPECT_THROW(av::sourceCatalogFromJson(json), std::runtime_error);
}

TEST(SourceCatalogTest, RejectsMissingPowerDb) {
  nlohmann::json json = sampleCatalog();
  json["sources"][0].erase("powerDb");
  EXPECT_THROW(av::sourceCatalogFromJson(json), std::runtime_error);
}

TEST(SourceCatalogTest, RoundTripsThroughJson) {
  const av::SourceCatalog catalog = av::sourceCatalogFromJson(sampleCatalog());
  const av::SourceCatalog again =
      av::sourceCatalogFromJson(av::sourceCatalogToJson(catalog));
  EXPECT_EQ(again.disclaimer, catalog.disclaimer);
  ASSERT_EQ(again.sources.size(), catalog.sources.size());
  EXPECT_EQ(again.sources[0].id, catalog.sources[0].id);
  EXPECT_DOUBLE_EQ(again.sources[0].powerDb, catalog.sources[0].powerDb);
}

TEST(SourceCatalogTest, JsonShapeMatchesApiContract) {
  const av::SourceCatalog catalog = av::sourceCatalogFromJson(sampleCatalog());
  const nlohmann::json body = av::sourceCatalogToJson(catalog);
  ASSERT_TRUE(body.is_object());
  ASSERT_TRUE(body.contains("disclaimer"));
  EXPECT_TRUE(body.at("disclaimer").is_string());
  ASSERT_TRUE(body.contains("sources"));
  ASSERT_TRUE(body.at("sources").is_array());
  ASSERT_EQ(body.at("sources").size(), 1u);
  const nlohmann::json& entry = body.at("sources").at(0);
  for (const char* field :
       {"id", "name", "powerDb", "spectrum", "directivity", "disclaimer"}) {
    EXPECT_TRUE(entry.contains(field)) << "missing " << field;
  }
  EXPECT_TRUE(entry.at("spectrum").at("bandsHz").is_array());
  EXPECT_TRUE(entry.at("spectrum").at("responseDb").is_array());
}

TEST(SourceCatalogTest, EnvOverrideResolvesAndLoads) {
  const fs::path dir = fs::temp_directory_path() / "av_source_catalog_env";
  fs::create_directories(dir);
  const fs::path file = dir / "custom_catalog.json";
  {
    std::ofstream out(file);
    out << sampleCatalog().dump();
  }
  EnvOverride env(file.string());
  const std::optional<fs::path> resolved = av::resolveSourceCatalogPath();
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, file);
  const av::SourceCatalog catalog = av::loadDefaultSourceCatalog();
  EXPECT_EQ(catalog.sources.size(), 1u);
  fs::remove_all(dir);
}

TEST(SourceCatalogTest, MissingFileYieldsEmptyCatalogWithDisclaimer) {
  EnvOverride env((fs::temp_directory_path() / "no_such_catalog.json").string());
  const av::SourceCatalog catalog = av::loadDefaultSourceCatalog();
  EXPECT_TRUE(catalog.sources.empty());
  EXPECT_EQ(catalog.disclaimer, av::kExampleSpecsDisclaimer);
}

TEST(SourceCatalogTest, ExeDirFallbackNamesSourceCatalogJson) {
  EnvOverride env("");
  const std::optional<fs::path> resolved =
      av::resolveSourceCatalogPath(fs::path("/tmp/av-bin"));
  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(*resolved, fs::path("/tmp/av-bin") / "source_catalog.json");
}

}  // namespace
