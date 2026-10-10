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
