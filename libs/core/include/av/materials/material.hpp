#pragma once

#include <string>
#include <vector>

#include <av/units.hpp>

namespace av {

struct Material {
  std::string id;
  std::string name;
  std::vector<double> bandsHz;
  std::vector<double> absorption;
  double scattering = 0.0;
};

// Arithmetic mean of absorption coefficients across bands.
[[nodiscard]] double meanAbsorption(const Material& material);

// T60 = 24*ln(10)*V / (c*S*meanAlpha); equals the documented 0.161*V/(S*meanAlpha)
// at c = kSpeedOfSound.
[[nodiscard]] double sabineRt60(double volume, double surfaceArea,
                                double meanAlpha,
                                double speedOfSound = kSpeedOfSound);

// T60 = 24*ln(10)*V / (c*S*(-ln(1-meanAlpha))).
[[nodiscard]] double eyringRt60(double volume, double surfaceArea,
                                double meanAlpha,
                                double speedOfSound = kSpeedOfSound);

}  // namespace av
