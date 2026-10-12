#pragma once

#include <optional>
#include <string>
#include <vector>

namespace av {

// Polar radiation pattern: relative gain (dB) vs angle, one curve per band.
// Layout (schemas): gainDb[i] belongs to freqsHz[i] and is sampled at
// anglesDeg (gainDb[i].size() == anglesDeg.size()). Curves are relative —
// 0 dB is the on-axis reference of the card/source, not an absolute level.
struct PolarCurve {
  std::vector<double> freqsHz;
  std::vector<double> anglesDeg;
  std::vector<std::vector<double>> gainDb;
};

// Source/catalog directivity. type is the analytic family name
// ("omni"|"dipole"|"cardioid"|"custom"); horizontal/vertical are optional
// measured (or datasheet) curves. When curves are absent a consumer may fall
// back to an analytic preset for omni/dipole/cardioid ("custom" without
// curves has no pattern to show). "horizontal" is the azimuth plane,
// "vertical" the elevation plane.
struct Directivity {
  std::string type = "omni";
  std::string description;
  std::optional<PolarCurve> horizontal;
  std::optional<PolarCurve> vertical;
};

// Source aim (additive, default 0). yawDeg rotates about +Y: 0 faces +Z,
// positive turns toward +X. pitchDeg tilts up/down: positive looks up.
struct SourceOrientation {
  double yawDeg = 0.0;
  double pitchDeg = 0.0;
};

}  // namespace av
