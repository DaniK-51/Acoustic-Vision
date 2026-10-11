#include <gtest/gtest.h>

#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh.hpp>
#include <av/io/io.hpp>
#include <av/metrics/metrics.hpp>
#include <av/units.hpp>

#include <cmath>
#include <filesystem>
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
// tests/unit/test_mesh.cpp makeUnitCube).
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

  scenario.params.engine = "ray-tracing";
  scenario.params.sampleRate = 8000;
  scenario.params.durationSec = 0.6;
  scenario.params.bandsHz.assign(av::kDefaultBandsHz.begin(),
                                 av::kDefaultBandsHz.end());
  scenario.params.methodParams = nlohmann::json{
      {"rayCount", 4000}, {"maxBounces", 64}, {"seed", 42},
      {"receiverRadius", 0.4}, {"capturePathLimit", 32}};
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

TEST(RayTracingEngine, DescriptorArtifactsAndCaps) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  const av::EngineDescriptor desc = engine->descriptor();
  EXPECT_EQ(desc.id, "ray-tracing");
  EXPECT_EQ(desc.version, "0.1.0");
  EXPECT_FALSE(desc.name.empty());
  EXPECT_TRUE(desc.caps.supportsMesh);
  EXPECT_FALSE(desc.caps.supportsRect);
  EXPECT_TRUE(desc.caps.supportsRayPaths);
  EXPECT_EQ(desc.artifacts,
            (std::vector<std::string>{"metrics", "ir", "rays"}));
  EXPECT_TRUE(desc.paramsSchema.is_object());
  EXPECT_TRUE(desc.paramsSchema.contains("properties"));
}

TEST(RayTracingEngine, LinkBuiltinEnginesPullsRayTracing) {
  av::linkBuiltinEngines();
  EXPECT_TRUE(av::EngineRegistry::instance().contains("ray-tracing"));
  EXPECT_NE(av::EngineRegistry::instance().create("ray-tracing"), nullptr);
}

TEST(RayTracingEngine, ValidateRejectsRectRoom) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.rect = {6.0, 3.0, 4.0, {}};
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());
}

TEST(RayTracingEngine, ValidateRejectsMissingLoadedMesh) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.loadedMesh = nullptr;
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_FALSE(result.ok);
}

TEST(RayTracingEngine, ValidateRejectsEmptyMesh) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.loadedMesh = std::make_shared<av::Mesh>();
  EXPECT_FALSE(engine->validate(scenario).ok);
}

TEST(RayTracingEngine, ValidateRejectsEmptySourcesOrReceivers) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario noSource = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  noSource.sources.clear();
  EXPECT_FALSE(engine->validate(noSource).ok);

  av::Scenario noReceiver = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  noReceiver.receivers.clear();
  EXPECT_FALSE(engine->validate(noReceiver).ok);
}

TEST(RayTracingEngine, ValidateWarnsOnMissingMaterials) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  scenario.materials.clear();
  scenario.room.mesh.materialOverrides.clear();
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_TRUE(result.ok);
  EXPECT_FALSE(result.warnings.empty());
}

TEST(RayTracingEngine, ValidateOkOnMeshBox) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  const av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.2);
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_TRUE(result.ok) << (result.errors.empty() ? "" : result.errors.front());
  EXPECT_TRUE(result.errors.empty());
}

TEST(RayTracingEngine, BoxMeshProducesEnergyAndFiniteRt60) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
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

  // Analytic Sabine/Eyring from the mesh shell is populated per band.
  const av::Rt60& rt = result.metrics->rt60.at(500.0);
  EXPECT_TRUE(std::isfinite(rt.sabine));
  EXPECT_GT(rt.sabine, 0.0);
  EXPECT_GT(rt.eyring, 0.0);
  EXPECT_LT(rt.eyring, rt.sabine);
}

TEST(RayTracingEngine, BoxObjFixtureProducesEnergy) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  // Real box.obj fixture (unit cube, zones floor/ceiling/walls) with a
  // carpet-like absorption on all zones via materialOverrides.
  const av::MeshRoom room{
      .uri = (std::filesystem::path(AV_FIXTURES_DIR) / "box.obj").string(),
      .unit = "m",
      .scale = 1.0,
      .materialOverrides = {{"floor", "mat-carpet"},
                            {"ceiling", "mat-carpet"},
                            {"walls", "mat-carpet"}},
  };
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Mesh;
  scenario.room.mesh = room;
  scenario.materials.push_back(
      makeMaterial("mat-carpet", {0.10, 0.20, 0.30, 0.40, 0.40, 0.40}));
  auto mesh = std::make_shared<av::Mesh>(av::loadMeshRoom(room));
  ASSERT_EQ(mesh->triangles.size(), 12U);
  scenario.loadedMesh = mesh;

  av::Source source;
  source.id = "src-1";
  source.position = {0.3, 0.5, 0.3};
  scenario.sources.push_back(source);
  av::Receiver receiver;
  receiver.id = "rcv-1";
  receiver.position = {0.7, 0.5, 0.7};
  scenario.receivers.push_back(receiver);

  scenario.params.engine = "ray-tracing";
  scenario.params.sampleRate = 8000;
  scenario.params.durationSec = 0.5;
  scenario.params.capturePaths = true;
  scenario.params.methodParams = nlohmann::json{
      {"rayCount", 2000}, {"maxBounces", 48}, {"seed", 3},
      {"receiverRadius", 0.25}, {"capturePathLimit", 16}};

  ASSERT_TRUE(engine->validate(scenario).ok);
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.ir.has_value());
  EXPECT_GT(totalEnergy(*result.ir), 0.0);
  ASSERT_TRUE(result.rays.has_value());
  EXPECT_FALSE(result.rays->paths.empty());
  EXPECT_LE(result.rays->paths.size(), 16U);
  ASSERT_TRUE(result.metrics.has_value());
  EXPECT_TRUE(std::isfinite(result.metrics->rt60.at(0.0).schroederT30));
}

TEST(RayTracingEngine, AnalyticRt60MatchesHandValue) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  // Unit-cube shell: V = 1 m^3, S = 6 m^2, uniform alpha = 0.2.
  av::Scenario scenario = makeMeshScenario(1.0, 1.0, 1.0, 0.2);
  scenario.params.methodParams = nlohmann::json{{"rayCount", 50},
                                                {"maxBounces", 4},
                                                {"seed", 1}};
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.metrics.has_value());

  const av::Rt60& rt = result.metrics->rt60.at(500.0);
  EXPECT_NEAR(rt.sabine, av::sabineRt60(1.0, 6.0, 0.2), 1e-9);
  EXPECT_NEAR(rt.eyring, av::eyringRt60(1.0, 6.0, 0.2), 1e-9);
}

TEST(RayTracingEngine, CapturePathsNonempty) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  scenario.params.capturePaths = true;
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.rays.has_value());
  const av::RayPaths& rays = *result.rays;
  ASSERT_FALSE(rays.paths.empty());
  const av::RayPolyline& path = rays.paths.front();
  EXPECT_GE(path.points.size(), 2U);
  EXPECT_GE(path.order, 1);
  EXPECT_GT(path.amplitude, 0.0);
  EXPECT_LE(rays.paths.size(), 32U);  // capturePathLimit from methodParams
}

TEST(RayTracingEngine, CapturePathsEmptyWhenDisabled) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  scenario.params.capturePaths = false;
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.rays.has_value());
  EXPECT_TRUE(result.rays->paths.empty());
}

TEST(RayTracingEngine, CancelledTokenReturnsEmpty) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  av::CancelToken cancel;
  cancel.cancel();
  const av::Result result = engine->run(scenario, scenario.params, cancel, nullptr);
  EXPECT_FALSE(result.ir.has_value());
  EXPECT_FALSE(result.metrics.has_value());
  EXPECT_FALSE(result.rays.has_value());
}

TEST(RayTracingEngine, SeedIsDeterministic) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  const av::Result a =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  const av::Result b =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(a.ir.has_value());
  ASSERT_TRUE(b.ir.has_value());
  ASSERT_EQ(a.ir->samples.size(), b.ir->samples.size());
  EXPECT_EQ(a.ir->samples, b.ir->samples);

  scenario.params.methodParams["seed"] = 7;
  const av::Result c =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(c.ir.has_value());
  EXPECT_NE(a.ir->samples, c.ir->samples);
}

TEST(RayTracingEngine, ProgressIsCoarseAndCompletes) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("ray-tracing");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeMeshScenario(6.0, 3.0, 4.0, 0.25);
  scenario.params.methodParams["rayCount"] = 200;
  std::vector<double> fractions;
  const av::Result result = engine->run(
      scenario, scenario.params, av::CancelToken{},
      [&](const av::Progress& p) { fractions.push_back(p.fraction); });
  EXPECT_TRUE(result.ir.has_value());
  ASSERT_FALSE(fractions.empty());
  EXPECT_DOUBLE_EQ(fractions.back(), 1.0);
  // Coarse: roughly per-10% steps, not per-ray.
  EXPECT_LT(fractions.size(), 20U);
}
