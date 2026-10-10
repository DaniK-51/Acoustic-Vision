#include <gtest/gtest.h>

#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/metrics/metrics.hpp>
#include <av/units.hpp>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace {

av::Material makeMaterial(std::string id,
                          std::vector<double> absorption) {
  av::Material material;
  material.id = id;
  material.name = id;
  material.bandsHz.assign(av::kDefaultBandsHz.begin(),
                          av::kDefaultBandsHz.end());
  material.absorption = std::move(absorption);
  return material;
}

// Rect room with one uniform material on all six faces.
av::Scenario makeUniformRoom(double alpha, double w, double h, double d) {
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.rect.w = w;
  scenario.room.rect.h = h;
  scenario.room.rect.d = d;
  for (const char* face : {"floor", "ceiling", "x+", "x-", "z+", "z-"}) {
    scenario.room.rect.faces[face] = "mat-uniform";
  }
  scenario.materials.push_back(
      makeMaterial("mat-uniform", {alpha, alpha, alpha, alpha, alpha, alpha}));
  return scenario;
}

// Shoebox 6 x 2.8 x 4.5 with mixed materials (alpha ~ 0.1-0.4).
av::Scenario makeShoebox() {
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.rect.w = 6.0;
  scenario.room.rect.h = 2.8;
  scenario.room.rect.d = 4.5;
  scenario.room.rect.faces = {
      {"floor", "mat-carpet"}, {"ceiling", "mat-plaster"},
      {"x+", "mat-brick"},     {"x-", "mat-brick"},
      {"z+", "mat-glass"},     {"z-", "mat-brick"}};
  scenario.materials.push_back(
      makeMaterial("mat-carpet", {0.10, 0.20, 0.30, 0.40, 0.40, 0.40}));
  scenario.materials.push_back(
      makeMaterial("mat-plaster", {0.10, 0.10, 0.15, 0.20, 0.20, 0.20}));
  scenario.materials.push_back(
      makeMaterial("mat-brick", {0.10, 0.10, 0.12, 0.15, 0.15, 0.20}));
  scenario.materials.push_back(
      makeMaterial("mat-glass", {0.10, 0.10, 0.12, 0.15, 0.20, 0.25}));

  av::Source source;
  source.id = "src-1";
  source.position = {1.2, 1.5, 1.0};
  scenario.sources.push_back(source);

  av::Receiver receiver;
  receiver.id = "rcv-1";
  receiver.position = {4.0, 1.5, 3.0};
  scenario.receivers.push_back(receiver);

  scenario.params.engine = "image-source";
  scenario.params.maxReflectionOrder = 8;
  scenario.params.sampleRate = 16000;
  scenario.params.durationSec = 1.0;
  scenario.params.bandsHz.assign(av::kDefaultBandsHz.begin(),
                                 av::kDefaultBandsHz.end());
  return scenario;
}

}  // namespace

TEST(BuiltinEngines, RegistryListsBuiltinsAfterLink) {
  av::linkBuiltinEngines();
  auto& registry = av::EngineRegistry::instance();
  EXPECT_TRUE(registry.contains("sabine-eyring"));
  EXPECT_TRUE(registry.contains("image-source"));

  const auto sabine = registry.create("sabine-eyring");
  ASSERT_NE(sabine, nullptr);
  EXPECT_EQ(sabine->descriptor().artifacts,
            std::vector<std::string>{"metrics"});

  const auto imageSource = registry.create("image-source");
  ASSERT_NE(imageSource, nullptr);
  EXPECT_EQ(imageSource->descriptor().artifacts,
            (std::vector<std::string>{"metrics", "ir"}));
}

TEST(SabineEyringEngine, KnownHandValue) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("sabine-eyring");
  ASSERT_NE(engine, nullptr);

  // V = 240 m^3, S = 268 m^2, alpha = 0.2:
  // T = 24*ln(10)*V/(c*S*alpha) ~ 0.7214 s; Eyring ~ 0.6466 s.
  av::Scenario scenario = makeUniformRoom(0.2, 10.0, 3.0, 8.0);
  ASSERT_TRUE(engine->validate(scenario).ok);

  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.metrics.has_value());
  EXPECT_FALSE(result.ir.has_value());

  const av::Rt60& rt = result.metrics->rt60.at(500.0);
  EXPECT_NEAR(rt.sabine, av::sabineRt60(240.0, 268.0, 0.2), 1e-9);
  EXPECT_NEAR(rt.sabine, 0.7214, 1e-3);
  EXPECT_NEAR(rt.eyring, av::eyringRt60(240.0, 268.0, 0.2), 1e-9);
  EXPECT_NEAR(rt.eyring, 0.6466, 1e-3);
  EXPECT_DOUBLE_EQ(rt.schroederT30, 0.0);
}

TEST(SabineEyringEngine, EmptyAbsorptionIsInfinite) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("sabine-eyring");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeUniformRoom(0.0, 10.0, 3.0, 8.0);
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.metrics.has_value());
  EXPECT_TRUE(std::isinf(result.metrics->rt60.at(500.0).sabine));
  EXPECT_TRUE(std::isinf(result.metrics->rt60.at(500.0).eyring));
}

TEST(SabineEyringEngine, HighAbsorptionIsSmall) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("sabine-eyring");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeUniformRoom(0.9, 10.0, 3.0, 8.0);
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.metrics.has_value());
  const av::Rt60& rt = result.metrics->rt60.at(500.0);
  EXPECT_LT(rt.sabine, 0.2);
  EXPECT_LT(rt.eyring, 0.2);
  EXPECT_LT(rt.eyring, rt.sabine);
}

TEST(SabineEyringEngine, RejectsMeshRoom) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("sabine-eyring");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeUniformRoom(0.2, 10.0, 3.0, 8.0);
  scenario.room.type = av::Room::Type::Mesh;
  EXPECT_FALSE(engine->validate(scenario).ok);
}

TEST(ImageSourceEngine, ShoeboxIrAndMetrics) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("image-source");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeShoebox();
  ASSERT_TRUE(engine->validate(scenario).ok);

  const av::CancelToken cancel;
  const av::Result result =
      engine->run(scenario, scenario.params, cancel, nullptr);
  ASSERT_TRUE(result.ir.has_value());
  ASSERT_TRUE(result.metrics.has_value());

  const av::IrBuffer& ir = *result.ir;
  EXPECT_EQ(ir.channels, 1U);
  ASSERT_EQ(ir.channelNames.size(), 1U);
  EXPECT_EQ(ir.channelNames[0], "src-1:rcv-1");
  EXPECT_TRUE(ir.bandsHz.empty());

  const std::size_t count = ir.samples.size();
  ASSERT_EQ(count, 16000U);
  double totalEnergy = 0.0;
  for (float sample : ir.samples) {
    totalEnergy += static_cast<double>(sample);
  }
  EXPECT_GT(totalEnergy, 0.0);

  // Direct sound: first arrival at r/c.
  const double dx = 4.0 - 1.2;
  const double dy = 1.5 - 1.5;
  const double dz = 3.0 - 1.0;
  const double r = std::sqrt(dx * dx + dy * dy + dz * dz);
  const double expectedDelay = r / av::kSpeedOfSound;
  std::size_t first = count;
  for (std::size_t i = 0; i < count; ++i) {
    if (ir.samples[i] > 0.0F) {
      first = i;
      break;
    }
  }
  ASSERT_LT(first, count);
  EXPECT_NEAR(static_cast<double>(first) / ir.sampleRate, expectedDelay,
              2.5 / ir.sampleRate);

  // Broadband Schroeder T30 from the IR stays in a sane range.
  const std::span<const float> channel(ir.samples.data(), count);
  const double t30 = av::schroederT30(channel, ir.sampleRate);
  EXPECT_GT(t30, 0.05);
  EXPECT_LT(t30, 5.0);
  EXPECT_NEAR(result.metrics->rt60.at(0.0).schroederT30, t30, 1e-12);
  EXPECT_TRUE(std::isfinite(result.metrics->edt.at(0.0)));

  // Analytic per-band Sabine/Eyring is populated alongside.
  const av::Rt60& rt = result.metrics->rt60.at(500.0);
  EXPECT_GT(rt.sabine, 0.05);
  EXPECT_LT(rt.sabine, 5.0);
  EXPECT_GT(rt.eyring, 0.0);
}

TEST(ImageSourceEngine, DirectSoundDelayMatchesDistance) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("image-source");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeShoebox();
  scenario.params.maxReflectionOrder = 0;  // direct sound only
  const av::Result result =
      engine->run(scenario, scenario.params, av::CancelToken{}, nullptr);
  ASSERT_TRUE(result.ir.has_value());

  const av::IrBuffer& ir = *result.ir;
  std::size_t first = ir.samples.size();
  for (std::size_t i = 0; i < ir.samples.size(); ++i) {
    if (ir.samples[i] > 0.0F) {
      first = i;
      break;
    }
  }
  ASSERT_LT(first, ir.samples.size());
  const double r = std::sqrt(2.8 * 2.8 + 2.0 * 2.0);
  EXPECT_NEAR(static_cast<double>(first) / ir.sampleRate,
              r / av::kSpeedOfSound, 2.5 / ir.sampleRate);
  EXPECT_DOUBLE_EQ(result.metrics->rt60.at(0.0).schroederT30, 0.0);
}

TEST(ImageSourceEngine, RejectsMeshRoom) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("image-source");
  ASSERT_NE(engine, nullptr);

  av::Scenario scenario = makeShoebox();
  scenario.room.type = av::Room::Type::Mesh;
  const av::ValidateResult result = engine->validate(scenario);
  EXPECT_FALSE(result.ok);
  EXPECT_FALSE(result.errors.empty());
}

TEST(ImageSourceEngine, RejectsMissingSourceOrReceiver) {
  av::linkBuiltinEngines();
  auto engine = av::EngineRegistry::instance().create("image-source");
  ASSERT_NE(engine, nullptr);

  av::Scenario noSource = makeShoebox();
  noSource.sources.clear();
  EXPECT_FALSE(engine->validate(noSource).ok);

  av::Scenario noReceiver = makeShoebox();
  noReceiver.receivers.clear();
  EXPECT_FALSE(engine->validate(noReceiver).ok);
}
