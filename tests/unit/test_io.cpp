#include <gtest/gtest.h>

#include <av/io/io.hpp>
#include <av/units.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("av_test_io_" + name);
}

av::Material makeMaterial(std::string id, std::vector<double> absorption) {
  av::Material material;
  material.id = id;
  material.name = id;
  material.bandsHz.assign(av::kDefaultBandsHz.begin(),
                          av::kDefaultBandsHz.end());
  material.absorption = std::move(absorption);
  material.scattering = 0.1;
  return material;
}

av::Scenario makeShoeboxScenario() {
  av::Scenario scenario;
  scenario.schemaVersion = 1;
  scenario.name = "demo-scenario";
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.rect.w = 6.0;
  scenario.room.rect.h = 2.8;
  scenario.room.rect.d = 4.5;
  scenario.room.rect.faces = {
      {"floor", "mat-carpet"}, {"ceiling", "mat-plaster"},
      {"x+", "mat-brick"},     {"x-", "mat-brick"},
      {"z+", "mat-brick"},     {"z-", "mat-brick"}};
  scenario.materials.push_back(
      makeMaterial("mat-carpet", {0.10, 0.20, 0.30, 0.40, 0.40, 0.40}));
  scenario.materials.push_back(
      makeMaterial("mat-plaster", {0.10, 0.10, 0.15, 0.20, 0.20, 0.20}));
  scenario.materials.push_back(
      makeMaterial("mat-brick", {0.10, 0.10, 0.12, 0.15, 0.15, 0.20}));

  av::Source source;
  source.id = "src-1";
  source.position = {1.2, 1.5, 1.0};
  source.powerDb = 80.0;
  scenario.sources.push_back(source);

  av::Receiver receiver;
  receiver.id = "rcv-1";
  receiver.position = {4.0, 1.5, 3.0};
  scenario.receivers.push_back(receiver);

  scenario.params.engine = "image-source";
  scenario.params.maxReflectionOrder = 10;
  scenario.params.sampleRate = 16000;
  scenario.params.durationSec = 1.0;
  scenario.params.bandsHz.assign(av::kDefaultBandsHz.begin(),
                                 av::kDefaultBandsHz.end());
  scenario.params.speedOfSound = 343.0;
  scenario.params.signedIr = false;
  scenario.params.methodParams = nlohmann::json::object();
  return scenario;
}

std::uint32_t readU32Le(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

float readF32Le(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
  const std::uint32_t bits = readU32Le(bytes, offset);
  float value = 0.0F;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

}  // namespace

TEST(Io, ScenarioJsonRoundTrip) {
  const av::Scenario scenario = makeShoeboxScenario();
  const nlohmann::json json = av::scenarioToJson(scenario);
  const av::Scenario back = av::scenarioFromJson(json);

  EXPECT_EQ(back.schemaVersion, scenario.schemaVersion);
  EXPECT_EQ(back.name, scenario.name);
  EXPECT_EQ(back.room.type, av::Room::Type::Rect);
  EXPECT_DOUBLE_EQ(back.room.rect.w, scenario.room.rect.w);
  EXPECT_DOUBLE_EQ(back.room.rect.h, scenario.room.rect.h);
  EXPECT_DOUBLE_EQ(back.room.rect.d, scenario.room.rect.d);
  EXPECT_EQ(back.room.rect.faces, scenario.room.rect.faces);

  ASSERT_EQ(back.materials.size(), scenario.materials.size());
  for (std::size_t i = 0; i < back.materials.size(); ++i) {
    EXPECT_EQ(back.materials[i].id, scenario.materials[i].id);
    EXPECT_EQ(back.materials[i].bandsHz, scenario.materials[i].bandsHz);
    EXPECT_EQ(back.materials[i].absorption, scenario.materials[i].absorption);
    EXPECT_DOUBLE_EQ(back.materials[i].scattering,
                     scenario.materials[i].scattering);
  }

  ASSERT_EQ(back.sources.size(), 1U);
  EXPECT_EQ(back.sources[0].id, "src-1");
  EXPECT_DOUBLE_EQ(back.sources[0].position.x, 1.2);
  EXPECT_DOUBLE_EQ(back.sources[0].powerDb, 80.0);
  ASSERT_EQ(back.receivers.size(), 1U);
  EXPECT_EQ(back.receivers[0].id, "rcv-1");

  EXPECT_EQ(back.params.engine, scenario.params.engine);
  EXPECT_EQ(back.params.maxReflectionOrder, scenario.params.maxReflectionOrder);
  EXPECT_EQ(back.params.sampleRate, scenario.params.sampleRate);
  EXPECT_DOUBLE_EQ(back.params.durationSec, scenario.params.durationSec);
  EXPECT_EQ(back.params.bandsHz, scenario.params.bandsHz);
  EXPECT_DOUBLE_EQ(back.params.speedOfSound, scenario.params.speedOfSound);
  EXPECT_EQ(back.params.signedIr, scenario.params.signedIr);
}

TEST(Io, ScenarioFileRoundTrip) {
  const av::Scenario scenario = makeShoeboxScenario();
  const std::filesystem::path path = tempPath("scenario.json");
  av::saveScenario(scenario, path);
  const av::Scenario back = av::loadScenario(path);
  EXPECT_EQ(back.name, scenario.name);
  EXPECT_EQ(back.params.engine, scenario.params.engine);
  std::filesystem::remove(path);
}

TEST(Io, WavHeaderMagicAndDataSize) {
  av::IrBuffer ir;
  ir.sampleRate = 16000.0;
  ir.durationSec = 4.0 / 16000.0;
  ir.channels = 2;
  ir.samples = {
      0.0F, 0.1F, 0.2F, 0.3F,  // channel 0, planar
      1.0F, 1.1F, 1.2F, 1.3F,  // channel 1, planar
  };
  ir.channelNames = {"src-1:rcv-1", "src-2:rcv-1"};

  const std::filesystem::path path = tempPath("ir.wav");
  av::writeWav(path, ir);

  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input.good());
  const std::vector<std::uint8_t> bytes(
      (std::istreambuf_iterator<char>(input)),
      std::istreambuf_iterator<char>());
  ASSERT_GE(bytes.size(), 58U);

  EXPECT_EQ(bytes[0], static_cast<std::uint8_t>('R'));
  EXPECT_EQ(bytes[1], static_cast<std::uint8_t>('I'));
  EXPECT_EQ(bytes[2], static_cast<std::uint8_t>('F'));
  EXPECT_EQ(bytes[3], static_cast<std::uint8_t>('F'));
  EXPECT_EQ(bytes[8], static_cast<std::uint8_t>('W'));
  EXPECT_EQ(bytes[9], static_cast<std::uint8_t>('A'));
  EXPECT_EQ(bytes[10], static_cast<std::uint8_t>('V'));
  EXPECT_EQ(bytes[11], static_cast<std::uint8_t>('E'));

  // fmt: format tag 3 (IEEE float), 32 bits, 2 channels.
  EXPECT_EQ(readU32Le(bytes, 20) & 0xFFFFu, 3u);
  EXPECT_EQ(readU32Le(bytes, 22) & 0xFFFFu, 2u);
  EXPECT_EQ(readU32Le(bytes, 24), 16000u);
  EXPECT_EQ(readU32Le(bytes, 34) & 0xFFFFu, 32u);

  // data chunk at offset 50 (after fmt-18 + fact), size at 54.
  EXPECT_EQ(bytes[50], static_cast<std::uint8_t>('d'));
  EXPECT_EQ(bytes[51], static_cast<std::uint8_t>('a'));
  EXPECT_EQ(bytes[52], static_cast<std::uint8_t>('t'));
  EXPECT_EQ(bytes[53], static_cast<std::uint8_t>('a'));
  const std::uint32_t dataSize = readU32Le(bytes, 54);
  EXPECT_EQ(dataSize, ir.samples.size() * 4u);
  EXPECT_EQ(bytes.size(), 58u + dataSize);

  // Planar input is interleaved on write: frame 0 is (ch0[0], ch1[0]).
  EXPECT_FLOAT_EQ(readF32Le(bytes, 58), 0.0F);
  EXPECT_FLOAT_EQ(readF32Le(bytes, 62), 1.0F);
  EXPECT_FLOAT_EQ(readF32Le(bytes, 66), 0.1F);
  EXPECT_FLOAT_EQ(readF32Le(bytes, 70), 1.1F);

  std::filesystem::remove(path);
}

TEST(Io, MetricsJsonContainsSabineKey) {
  av::Metrics metrics;
  metrics.rt60[500.0].schroederT30 = 0.38;
  metrics.rt60[500.0].sabine = 0.40;
  metrics.rt60[500.0].eyring = 0.42;
  metrics.edt[500.0] = 0.12;
  metrics.earlyReflections.push_back(av::EarlyReflection{0.01, 0.5, 1});

  const nlohmann::json json = av::metricsToJson(metrics);
  ASSERT_TRUE(json.contains("rt60"));
  ASSERT_TRUE(json["rt60"].contains("500"));
  EXPECT_TRUE(json["rt60"]["500"].contains("sabine"));
  EXPECT_DOUBLE_EQ(json["rt60"]["500"]["sabine"].get<double>(), 0.40);
  EXPECT_DOUBLE_EQ(json["rt60"]["500"]["eyring"].get<double>(), 0.42);
  ASSERT_TRUE(json.contains("edt"));
  EXPECT_DOUBLE_EQ(json["edt"]["500"].get<double>(), 0.12);
  ASSERT_TRUE(json.contains("earlyReflections"));
  EXPECT_EQ(json["earlyReflections"].size(), 1U);

  const std::filesystem::path path = tempPath("metrics.json");
  av::Result result;
  result.metrics = metrics;
  av::writeMetricsJson(path, result);
  std::ifstream input(path);
  const nlohmann::json fromFile = nlohmann::json::parse(input);
  EXPECT_TRUE(fromFile["rt60"]["500"].contains("sabine"));
  std::filesystem::remove(path);
}

TEST(Io, FieldGridParamsRoundTrip) {
  av::Scenario scenario = makeShoeboxScenario();
  scenario.params.fieldGrid = {.enabled = true, .nx = 8, .ny = 4, .nz = 6};

  const nlohmann::json json = av::scenarioToJson(scenario);
  ASSERT_TRUE(json["params"].contains("fieldGrid"));
  EXPECT_TRUE(json["params"]["fieldGrid"]["enabled"].get<bool>());
  EXPECT_EQ(json["params"]["fieldGrid"]["nx"].get<int>(), 8);

  const av::Scenario back = av::scenarioFromJson(json);
  EXPECT_TRUE(back.params.fieldGrid.enabled);
  EXPECT_EQ(back.params.fieldGrid.nx, 8);
  EXPECT_EQ(back.params.fieldGrid.ny, 4);
  EXPECT_EQ(back.params.fieldGrid.nz, 6);

  // Absent by default (schema keeps fieldGrid optional).
  const av::Scenario plain = makeShoeboxScenario();
  EXPECT_FALSE(av::scenarioToJson(plain)["params"].contains("fieldGrid"));
  EXPECT_FALSE(plain.params.fieldGrid.enabled);
}

TEST(Io, FieldToJsonShapeAndWrite) {
  av::SpatialField field;
  field.nx = 2;
  field.ny = 2;
  field.nz = 1;
  field.origin = {1.0, 2.0, 3.0};
  field.cellSize = 0.5;
  field.bandHz = 0.0;
  field.unit = "energy";
  field.values = {0.25F, 0.5F, 0.75F, 1.0F};

  const nlohmann::json json = av::fieldToJson(field);
  EXPECT_EQ(json["nx"].get<int>(), 2);
  EXPECT_EQ(json["ny"].get<int>(), 2);
  EXPECT_EQ(json["nz"].get<int>(), 1);
  ASSERT_TRUE(json["origin"].is_array());
  EXPECT_EQ(json["origin"].size(), 3U);
  EXPECT_DOUBLE_EQ(json["origin"][0].get<double>(), 1.0);
  EXPECT_DOUBLE_EQ(json["cellSize"].get<double>(), 0.5);
  EXPECT_DOUBLE_EQ(json["bandHz"].get<double>(), 0.0);
  EXPECT_EQ(json["unit"].get<std::string>(), "energy");
  ASSERT_TRUE(json["values"].is_array());
  EXPECT_EQ(json["values"].size(), 4U);
  EXPECT_FLOAT_EQ(json["values"][2].get<float>(), 0.75F);

  av::Result result;
  result.field = field;
  const nlohmann::json full = av::resultToJson(result);
  ASSERT_TRUE(full.contains("field"));
  EXPECT_EQ(full["field"]["nx"].get<int>(), 2);
  EXPECT_EQ(full["field"]["values"].size(), 4U);

  const std::filesystem::path path = tempPath("field.json");
  av::writeFieldJson(path, result);
  std::ifstream input(path);
  const nlohmann::json fromFile = nlohmann::json::parse(input);
  EXPECT_EQ(fromFile["values"].size(), 4U);
  std::filesystem::remove(path);

  // No field -> no "field" key in the result JSON.
  EXPECT_FALSE(av::resultToJson(av::Result{}).contains("field"));

  // Rays key shape the GUI viewport consumes (result.rays.paths[] polylines):
  // points/amplitude/order per path, empty "paths" when capturePaths was off,
  // key omitted entirely when the engine has no rays artifact.
  av::RayPaths rays;
  av::RayPolyline polyline;
  polyline.points = {{0.0, 0.0, 0.0}, {1.0, 0.5, 0.0}, {2.0, 0.5, 1.0}};
  polyline.amplitude = 0.75;
  polyline.order = 2;
  rays.paths.push_back(polyline);

  av::Result withRays;
  withRays.rays = rays;
  const nlohmann::json withRaysJson = av::resultToJson(withRays);
  ASSERT_TRUE(withRaysJson.contains("rays"));
  ASSERT_EQ(withRaysJson["rays"]["paths"].size(), 1U);
  const nlohmann::json& entry = withRaysJson["rays"]["paths"][0];
  ASSERT_TRUE(entry["points"].is_array());
  EXPECT_EQ(entry["points"].size(), 3U);
  EXPECT_DOUBLE_EQ(entry["points"][0][0].get<double>(), 0.0);
  EXPECT_DOUBLE_EQ(entry["points"][2][2].get<double>(), 1.0);
  EXPECT_DOUBLE_EQ(entry["amplitude"].get<double>(), 0.75);
  EXPECT_EQ(entry["order"].get<int>(), 2);

  av::Result emptyRays;
  emptyRays.rays = av::RayPaths{};
  EXPECT_TRUE(av::resultToJson(emptyRays)["rays"]["paths"].empty());
  EXPECT_FALSE(av::resultToJson(av::Result{}).contains("rays"));
}
