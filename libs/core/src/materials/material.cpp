#include <av/materials/material.hpp>

#include <cmath>

namespace av {

double meanAbsorption(const Material& material) {
  if (material.absorption.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (double a : material.absorption) {
    sum += a;
  }
  return sum / static_cast<double>(material.absorption.size());
}

double sabineRt60(double volume, double surfaceArea, double meanAlpha,
                  double speedOfSound) {
  // IEEE handles alpha = 0 (inf) and degenerate geometry; no branch needed.
  return (24.0 * std::log(10.0) * volume) /
         (speedOfSound * surfaceArea * meanAlpha);
}

double eyringRt60(double volume, double surfaceArea, double meanAlpha,
                  double speedOfSound) {
  // -ln(1-alpha) -> 0 as alpha -> 0 (inf RT60); -> +inf as alpha -> 1 (zero RT60).
  return (24.0 * std::log(10.0) * volume) /
         (speedOfSound * surfaceArea * -std::log(1.0 - meanAlpha));
}

}  // namespace av
