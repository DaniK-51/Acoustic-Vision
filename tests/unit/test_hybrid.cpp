#include <gtest/gtest.h>

#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh.hpp>
#include <av/metrics/metrics.hpp>
#include <av/units.hpp>

#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

av::Material makeMaterial(std::string id, std::vector<double> absorption) {
  av::Material material;
  material.id = id;
  material.name = id;
  material.bandsHz.assign(av::kDefaultBandsHz.begin(),
                          av::kDefaultBandsHz.end());
  material.absorption = std::move(absorption);
  return material;
}

// Axis-aligned box [0,w]x[0,h]x[0,d] as 12 triangles (same layout as
// tests/unit/test_ray_tracing.cpp makeBoxMesh).
av::Mesh makeBoxMesh(double w, double h, double d, std::uint32_t slot = 0) {
  av::Mesh mesh;
  mesh.positions = {
      {0.0, 0.0, 0.0}, {w, 0.0, 0.0}, {w, h, 0.0}, {0.0, h, 0.0},
      {0.0, 0.0, d},   {w, 0.0, d},   {w, h, d},   {0.0, h, d},
  };
  const std::uint32_t quads[6][4] = {
      {0, 1, 2, 3},  // z = 0
      {4, 7, 6, 5},  // z = d
      {0, 4, 5, 1},  // y = 0
      {3, 2, 6, 7},  // y = h
      {0, 3, 7, 4},  // x = 0
      {1, 5, 6, 2},  // x = w
  };
  for (const auto& q : quads) {
    mesh.triangles.push_back({q[0], q[1], q[2], slot});
    mesh.triangles.push_back({q[0], q[2], q[3], slot});
  }
  mesh.materialSlots.push_back("shell");
  return mesh;
}

// Mesh room scenario with one in-memory box mesh and one source/receiver pair.
av::Scenario makeMeshScenario(double w, double h, double d, double alpha) {
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Mesh;
  scenario.room.mesh.uri = "in-memory";
  scenario.materials.push_back(
      makeMaterial("mat-uniform", {alpha, alpha, alpha, alpha, alpha, alpha}));
  scenario.room.mesh.materialOverrides = {{"shell", "mat-uniform"}};

  auto mesh = std::make_shared<av::Mesh>(makeBoxMesh(w, h, d));
  scenario.loadedMesh = mesh;

  av::Source source;
  source.id = "src-1";
  source.position = {w * 0.25, h * 0.5, d * 0.25};
  scenario.sources.push_back(source);

  av::Receiver receiver;
  receiver.id = "rcv-1";
  receiver.position = {w * 0.75, h * 0.5, d * 0.75};
  scenario.receivers.push_back(receiver);

  scenario.params.engine = "hybrid";
  scenario.params.sampleRate = 8000;
  scenario.params.durationSec = 0.6;
  scenario.params.bandsHz.assign(av::kDefaultBandsHz.begin(),
                                 av::kDefaultBandsHz.end());
  scenario.params.methodParams = nlohmann::json{
      {"earlyTimeSec", 0.08},
      {"earlyOrder", 2},
      {"rayCount", 4000},
      {"maxBounces", 64},
      {"seed", 42},
      {"receiverRadius", 0.4},
      {"useSabineTail", false},
      {"capturePathLimit", 32}};
  return scenario;
}

double totalEnergy(const av::IrBuffer& ir) {
  double sum = 0.0;
  for (float sample : ir.samples) {
    sum += static_cast<double>(sample);
  }
  return sum;
}

}  // namespace

TEST(HybridEngine, DescriptorIdArtifactsAndParamsSchema) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  const av::EngineDescriptor desc = engine->descriptor();
  EXPECT_EQ(desc.id, "hybrid");
  EXPECT_EQ(desc.version, "0.1.0");
  EXPECT_FALSE(desc.name.empty());
  EXPECT_TRUE(desc.caps.supportsMesh);
  EXPECT_FALSE(desc.caps.supportsRect);
  EXPECT_TRUE(desc.caps.supportsRayPaths);
  EXPECT_EQ(desc.artifacts,
            (std::vector<std::string>{"metrics", "ir", "rays", "field"}));

  ASSERT_TRUE(desc.paramsSchema.is_object());
  ASSERT_TRUE(desc.paramsSchema.contains("properties"));
  const nlohmann::json& props = desc.paramsSchema.at("properties");
  for (const char* key :
       {"earlyTimeSec", "earlyOrder", "rayCount", "maxBounces", "seed",
        "receiverRadius", "useSabineTail"}) {
    EXPECT_TRUE(props.contains(key)) << key;
  }
  EXPECT_DOUBLE_EQ(props.at("earlyTimeSec").at("default").get<double>(), 0.08);
  EXPECT_FALSE(props.at("useSabineTail").at("default").get<bool>());
}

TEST(HybridEngine, LinkBuiltinEnginesPullsHybrid) {
  av::linkBuiltinEngines();
  EXPECT_TRUE(av::EngineRegistry::instance().contains("hybrid"));
  EXPECT_NE(av::EngineRegistry::instance().create("hybrid"), nullptr);
}

TEST(HybridEngine, ValidateRejectsRectRoom) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.rect = {6.0, 3.0, 4.0, {}};
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());
}

TEST(HybridEngine, ValidateRejectsMissingLoadedMesh) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.loadedMesh = nullptr;
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_FALSE(result.ok);
}

TEST(HybridEngine, ValidateRejectsEmptyMesh) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.loadedMesh = std::make_shared<av::Mesh>();
  EXPECT_FALSE(engine->validate(scenario).ok);
}

TEST(HybridEngine, ValidateRejectsEmptySourcesOrReceivers) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario noSource = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  noSource.sources.clear();
  EXPECT_FALSE(engine->validate(noSource).ok);

  av::Scenario noReceiver = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  noReceiver.receivers.clear();
  EXPECT_FALSE(engine->validate(noReceiver).ok);
}

TEST(HybridEngine, ValidateOkOnMeshBox) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  const av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());
  EXPECT_TRUE(result.errors.empty());
}

TEST(HybridEngine, BoxMeshProducesEnergyFiniteRt60AndEarlyReflections) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  ASSERT_TRUE(engine->validate(scenario).ok);

  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.ir.has_value());
  ASSERT_TRUE(result.metrics.has_value());

  const av::IrBuffer& ir = *result.ir;
  EXPECT_EQ(ir.channels, 1U);
  ASSERT_EQ(ir.channelNames.size(), 1U);
  EXPECT_EQ(ir.channelNames[0], "src-1:rcv-1");
  EXPECT_GT(totalEnergy(ir), 0.0);

  const std::size_t count = ir.samples.size();
  const std::span<const float> channel(ir.samples.data(), count);
  const double t30 = av::schroederT30(channel, ir.sampleRate);
  EXPECT_TRUE(std::isfinite(t30));
  EXPECT_GT(t30, 0.0);
  EXPECT_LT(t30, 5.0);
  EXPECT_NEAR(result.metrics->rt60.at(0.0).schroederT30, t30, 1e-12);
  EXPECT_TRUE(std::isfinite(result.metrics->edt.at(0.0)));

  // Early reflections exist inside the 80 ms window (peak-picked from IR).
  EXPECT_FALSE(result.metrics->earlyReflections.empty());

  // Analytic Sabine/Eyring from the mesh shell is populated per band.
  const av::Rt60& rt = result.metrics->rt60.at(500.0);
  EXPECT_TRUE(std::isfinite(rt.sabine));
  EXPECT_GT(rt.sabine, 0.0);
  EXPECT_GT(rt.eyring, 0.0);
  EXPECT_LT(rt.eyring, rt.sabine);
}

TEST(HybridEngine, FieldFilledWhenEnabled) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  scenario.params.fieldGrid = {.enabled = true, .nx = 8, .ny = 4, .nz = 6};
  scenario.params.methodParams["rayCount"] = 500;
  ASSERT_TRUE(engine->validate(scenario).ok);

  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.field.has_value());
  const av::SpatialField& field = *result.field;
  EXPECT_EQ(field.nx, 8);
  EXPECT_EQ(field.ny, 4);
  EXPECT_EQ(field.nz, 6);
  EXPECT_EQ(field.values.size(), field.cellCount());
  double total = 0.0;
  for (float value : field.values) {
    EXPECT_TRUE(std::isfinite(value));
    EXPECT_GE(value, 0.0F);
    total += static_cast<double>(value);
  }
  EXPECT_GT(total, 0.0);
}

TEST(HybridEngine, FieldEmptyWhenDisabled) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  EXPECT_FALSE(scenario.params.fieldGrid.enabled);
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  EXPECT_FALSE(result.field.has_value());
}

TEST(HybridEngine, CapturePathsNonempty) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  scenario.params.capturePaths = true;
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.rays.has_value());
  EXPECT_FALSE(result.rays->paths.empty());
  EXPECT_LE(result.rays->paths.size(), 32U);
}

TEST(HybridEngine, UseSabineTailProducesEnergyAndFiniteRt60) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  scenario.params.methodParams["useSabineTail"] = true;
  scenario.params.durationSec = 1.0;
  ASSERT_TRUE(engine->validate(scenario).ok);

  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.ir.has_value());
  ASSERT_TRUE(result.metrics.has_value());
  EXPECT_GT(totalEnergy(*result.ir), 0.0);

  const av::IrBuffer& ir = *result.ir;
  const std::span<const float> channel(ir.samples.data(),
                                       ir.samples.size());
  const double t30 = av::schroederT30(channel, ir.sampleRate);
  EXPECT_TRUE(std::isfinite(t30));
  EXPECT_GT(t30, 0.0);
}

TEST(HybridEngine, CancelledTokenReturnsEmpty) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("hybrid");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  av::CancelToken cancel;
  cancel.cancel();
  const av::Result result = engine->run(scenario, scenario.params, cancel, nullptr);
  EXPECT_FALSE(result.ir.has_value());
  EXPECT_FALSE(result.metrics.has_value());
  EXPECT_FALSE(result.rays.has_value());
}

TEST(HybridEngine, RegistryListsHybrid) {
  av::linkBuiltinEngines();
  const auto descriptors = av::EngineRegistry::instance().descriptors();
  bool found = false;
  for (const auto& d : descriptors) {
    if (d.id == "hybrid") {
      found = true;
      EXPECT_EQ(d.artifacts,
                (std::vector<std::string>{"metrics", "ir", "rays", "field"}));
    }
  }
  EXPECT_TRUE(found);
}
