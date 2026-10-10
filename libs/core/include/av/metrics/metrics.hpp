#pragma once

#include <cstddef>
#include <map>
#include <vector>

namespace av {

struct Rt60 {
  double schroederT30 = 0.0;
  double sabine = 0.0;
  double eyring = 0.0;
};

struct EarlyReflection {
  double t = 0.0;
  double amp = 0.0;
  int order = 0;
};

// Values keyed by band center frequency, Hz.
struct Metrics {
  std::map<double, Rt60> rt60;
  std::map<double, double> edt;
  std::vector<EarlyReflection> earlyReflections;
};

// Multi-channel IR (one channel per receiver); planar [channel][sampleCount].
// bandsHz empty => broadband; otherwise one set of samples per band is intended
// (per-band packing lands with the engines that produce it).
struct IrBuffer {
  double sampleRate = 0.0;
  double durationSec = 0.0;
  std::size_t channels = 0;
  std::vector<double> bandsHz;
  std::vector<float> samples;
};

// Artifact placeholders; shapes land with the producing engines (P0-P2).
struct SpatialField {};
struct RayPaths {};
struct Findings {};

}  // namespace av
