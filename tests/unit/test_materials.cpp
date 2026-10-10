#include <gtest/gtest.h>

#include <av/materials/material.hpp>
#include <av/units.hpp>

TEST(Materials, MeanAbsorptionArithmeticMean) {
  av::Material mat;
  mat.absorption = {0.10, 0.25, 0.35, 0.40, 0.50};
  EXPECT_DOUBLE_EQ(av::meanAbsorption(mat), 0.32);
}

TEST(Materials, MeanAbsorptionEmptyIsZero) {
  av::Material mat;
  EXPECT_DOUBLE_EQ(av::meanAbsorption(mat), 0.0);
}

TEST(Materials, SabineRt60KnownValue) {
  // Documented form T = 0.161*V/(S*alpha) = 0.161*200/(200*0.2) = 0.805 s.
  const double t = av::sabineRt60(200.0, 200.0, 0.2);
  EXPECT_NEAR(t, 0.805, 1e-3);
}

TEST(Materials, EyringRt60KnownValue) {
  // T = 0.161*V/(S*(-ln(1-alpha))) with alpha = 0.2: -ln(0.8) ~ 0.22314,
  // T ~ 0.161*200/(200*0.22314) ~ 0.722 s.
  const double t = av::eyringRt60(200.0, 200.0, 0.2);
  EXPECT_NEAR(t, 0.722, 1e-3);
}

TEST(Materials, EyringBelowSabineForHighAbsorption) {
  EXPECT_LT(av::eyringRt60(100.0, 80.0, 0.8), av::sabineRt60(100.0, 80.0, 0.8));
}
