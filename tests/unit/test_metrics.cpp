#include <gtest/gtest.h>

#include <av/metrics/metrics.hpp>

#include <cmath>
#include <vector>

namespace {

// Energy envelope with an exact exponential decay of the given RT60.
std::vector<float> exponentialEnergy(double sampleRate, double rt60,
                                     double durationSec) {
  const double tau = rt60 / (6.0 * std::log(10.0));
  const auto count =
      static_cast<std::size_t>(std::llround(durationSec * sampleRate));
  std::vector<float> energy(count);
  for (std::size_t i = 0; i < count; ++i) {
    const double t = static_cast<double>(i) / sampleRate;
    energy[i] = static_cast<float>(std::exp(-t / tau));
  }
  return energy;
}

}  // namespace

TEST(Metrics, SchroederT30RecoversKnownRt60) {
  const double sampleRate = 8000.0;
  const double expected = 0.5;
  const std::vector<float> energy =
      exponentialEnergy(sampleRate, expected, 1.0);
  const double t30 = av::schroederT30(energy, sampleRate);
  EXPECT_NEAR(t30, expected, 0.1 * expected);
}

TEST(Metrics, SchroederT30ZeroForSilence) {
  const std::vector<float> energy(64, 0.0F);
  EXPECT_DOUBLE_EQ(av::schroederT30(energy, 48000.0), 0.0);
}

TEST(Metrics, EdtFiniteOnExponentialDecay) {
  const double sampleRate = 8000.0;
  const std::vector<float> energy =
      exponentialEnergy(sampleRate, 0.5, 1.0);
  const double value = av::edt(energy, sampleRate);
  EXPECT_TRUE(std::isfinite(value));
  EXPECT_GT(value, 0.0);
  // Pure exponential: EDT matches the decay rate of the envelope.
  EXPECT_NEAR(value, 0.5, 0.1 * 0.5);
}

TEST(Metrics, EarlyReflectionsFindKnownPeak) {
  const double sampleRate = 10000.0;
  std::vector<float> energy(1000, 0.0F);
  energy[100] = 1.0F;  // t = 10 ms
  energy[300] = 0.5F;  // t = 30 ms

  const std::vector<av::EarlyReflection> refs =
      av::earlyReflections(energy, sampleRate, 5, 0.080);
  ASSERT_EQ(refs.size(), 2U);
  EXPECT_NEAR(refs[0].t, 0.010, 1e-9);
  EXPECT_NEAR(refs[0].amp, 1.0, 1e-6);
  EXPECT_NEAR(refs[1].t, 0.030, 1e-9);
  EXPECT_NEAR(refs[1].amp, 0.5, 1e-6);
}

TEST(Metrics, EarlyReflectionsMaxPeaksKeepsLoudest) {
  const double sampleRate = 10000.0;
  std::vector<float> energy(1000, 0.0F);
  energy[100] = 1.0F;
  energy[300] = 0.5F;

  const std::vector<av::EarlyReflection> refs =
      av::earlyReflections(energy, sampleRate, 1, 0.080);
  ASSERT_EQ(refs.size(), 1U);
  EXPECT_NEAR(refs[0].t, 0.010, 1e-9);
}

TEST(SpatialField, IndexMathIsXFastest) {
  av::SpatialField field;
  field.nx = 4;
  field.ny = 3;
  field.nz = 2;
  EXPECT_EQ(field.cellCount(), 24U);
  EXPECT_EQ(field.index(0, 0, 0), 0U);
  EXPECT_EQ(field.index(1, 0, 0), 1U);
  EXPECT_EQ(field.index(0, 1, 0), 4U);
  EXPECT_EQ(field.index(0, 0, 1), 12U);
  EXPECT_EQ(field.index(3, 2, 1), 3U + 4U * (2U + 3U * 1U));
  EXPECT_EQ(field.index(3, 2, 1), 23U);
}

TEST(SpatialField, MakeFieldGridCoversBounds) {
  const av::Aabb bounds{{0.0, 0.0, 0.0}, {6.0, 3.0, 4.0}};
  const av::SpatialField field = av::makeFieldGrid(bounds, 8, 4, 4);

  EXPECT_EQ(field.nx, 8);
  EXPECT_EQ(field.ny, 4);
  EXPECT_EQ(field.nz, 4);
  EXPECT_DOUBLE_EQ(field.origin.x, 0.0);
  EXPECT_DOUBLE_EQ(field.origin.y, 0.0);
  EXPECT_DOUBLE_EQ(field.origin.z, 0.0);
  // Cubic cells cover the largest per-axis step: max(6/8, 3/4, 4/4) = 1.0.
  EXPECT_DOUBLE_EQ(field.cellSize, 1.0);
  EXPECT_DOUBLE_EQ(field.bandHz, 0.0);
  EXPECT_EQ(field.unit, "energy");
  ASSERT_EQ(field.values.size(), field.cellCount());
  for (float value : field.values) {
    EXPECT_FLOAT_EQ(value, 0.0F);
  }
}

TEST(SpatialField, MakeFieldGridClampsDims) {
  const av::Aabb bounds{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
  const av::SpatialField field = av::makeFieldGrid(bounds, 0, 1000, 5);
  EXPECT_EQ(field.nx, 1);
  EXPECT_EQ(field.ny, av::kMaxFieldDim);
  EXPECT_EQ(field.nz, 5);
  EXPECT_EQ(field.values.size(), field.cellCount());
}

TEST(SpatialField, DepositEnergyInsideAndOutside) {
  const av::Aabb bounds{{0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}};
  av::SpatialField field = av::makeFieldGrid(bounds, 2, 2, 2);
  ASSERT_DOUBLE_EQ(field.cellSize, 0.5);

  av::depositEnergy(field, {0.1, 0.1, 0.1}, 3.0);
  av::depositEnergy(field, {0.6, 0.1, 0.1}, 1.5);
  // Both points in cell (0,0,0) and (1,0,0) respectively.
  EXPECT_FLOAT_EQ(field.values[field.index(0, 0, 0)], 3.0F);
  EXPECT_FLOAT_EQ(field.values[field.index(1, 0, 0)], 1.5F);

  // Outside the grid: no-op.
  av::depositEnergy(field, {-0.1, 0.1, 0.1}, 9.0);
  av::depositEnergy(field, {0.1, 0.1, 1.2}, 9.0);
  EXPECT_FLOAT_EQ(field.values[field.index(0, 0, 0)], 3.0F);
  EXPECT_FLOAT_EQ(field.values[field.index(0, 0, 1)], 0.0F);
}
