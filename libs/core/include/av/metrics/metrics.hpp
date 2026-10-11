#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <av/scenario/scenario.hpp>
#include <av/units.hpp>

namespace av {

struct Rt60 {
  double schroederT30 = 0.0;
  double sabine = 0.0;
  double eyring = 0.0;
};

struct EarlyReflection {
  double t = 0.0;
  double amp = 0.0;
  int order = 0;  // 0 = unknown (peak-picked from IR alone)
};

// Values keyed by band center frequency, Hz.
// Key 0.0 denotes broadband values derived from a broadband IR.
struct Metrics {
  std::map<double, Rt60> rt60;
  std::map<double, double> edt;
  std::vector<EarlyReflection> earlyReflections;
};

// Multi-channel IR, planar [channel][sampleCount] with
// sampleCount = samples.size() / channels. bandsHz empty => broadband.
// samples hold an unsigned energy envelope (see docs/engines.md section 5:
// energy = amplitude^2); channelNames has one entry per channel ("srcId:rcvId").
struct IrBuffer {
  double sampleRate = 0.0;
  double durationSec = 0.0;
  std::size_t channels = 0;
  std::vector<double> bandsHz;
  std::vector<float> samples;
  std::vector<std::string> channelNames;
};

// --- Spatial field (heat-map artifact) ---

// Regular cubic-cell grid of energy density over the room volume.
// values stores *linear* energy density, not dB (unit is always "energy" in
// v1): convert to level at the API/UI edge. Index order is x-fastest,
// i = x + nx*(y + ny*z) (documented order shared with GUI/JSON).
struct SpatialField {
  int nx = 0;
  int ny = 0;
  int nz = 0;
  // World-space min corner of cell (0,0,0).
  Vec3 origin{};
  // Cubic cell edge length, m (v1: one size for all axes).
  double cellSize = 0.0;
  // 0 = broadband (the only mode engines produce in v1).
  double bandHz = 0.0;
  // "energy" | "dB"; v1 stores linear energy, so always "energy".
  std::string unit = "energy";
  // size nx*ny*nz.
  std::vector<float> values;

  [[nodiscard]] std::size_t cellCount() const {
    return static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny) *
           static_cast<std::size_t>(nz);
  }

  // Index of cell (x,y,z), x fastest.
  [[nodiscard]] std::size_t index(int x, int y, int z) const {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(nx) *
               (static_cast<std::size_t>(y) +
                static_cast<std::size_t>(ny) * static_cast<std::size_t>(z));
  }
};

// Per-axis cell-count cap: keeps grids drawable by the GUI and result JSON
// payloads small (max 64^3 cells). Larger requests are clamped with a
// validate() warning.
inline constexpr int kMaxFieldDim = 64;

// Zero-filled grid covering `bounds`: origin = bounds.min, cubic cellSize =
// max(extent/n) over the axes so the grid spans the whole box (cells may
// overshoot on smaller axes). Dims are clamped to [1, kMaxFieldDim].
[[nodiscard]] SpatialField makeFieldGrid(const Aabb& bounds, int nx, int ny,
                                         int nz);

// Accumulates `energy` into the cell containing `p`; no-op outside the grid.
void depositEnergy(SpatialField& field, const Vec3& p, double energy);

// One traced ray path in world space: source position then successive wall
// hits. amplitude is the bounce-factor amplitude (prod sqrt(1-alpha), without
// geometric 1/r); order is the wall-hit count along the path.
struct RayPolyline {
  std::vector<Vec3> points;
  double amplitude = 0.0;
  int order = 0;
};

struct RayPaths {
  std::vector<RayPolyline> paths;
};

struct Findings {};

// --- IR-derived metrics (input: one channel of an unsigned energy envelope) ---

// Schroeder backward integration of the energy decay, linear fit of -5..-35 dB;
// returns T30 extrapolated to 60 dB (60/|slope|), or 0 when the fit is not
// possible. Time zero is the peak sample (direct sound).
[[nodiscard]] double schroederT30(std::span<const float> energy,
                                  double sampleRate);

// Linear fit of 0..-10 dB of the Schroeder curve, reported as the 60 dB
// equivalent; 0 when the fit is not possible.
[[nodiscard]] double edt(std::span<const float> energy, double sampleRate);

// Top-`maxPeaks` local maxima of the envelope inside [0, windowSec], returned
// in time order. `amp` is the envelope value at the peak sample.
[[nodiscard]] std::vector<EarlyReflection> earlyReflections(
    std::span<const float> energy, double sampleRate, std::size_t maxPeaks,
    double windowSec = 0.080);

// --- Analytic metrics (rect room) ---

// Per-band RT60 from area-weighted mean absorption over the six faces.
// `bandsHz` empty => kDefaultBandsHz. schroederT30 is left 0 (no IR).
[[nodiscard]] std::map<double, Rt60> analyticRt60(
    const RectRoom& room, const Scenario& scenario,
    const std::vector<double>& bandsHz, double speedOfSound = kSpeedOfSound);

}  // namespace av
