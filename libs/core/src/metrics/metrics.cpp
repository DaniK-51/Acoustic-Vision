#include <av/metrics/metrics.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace av {
namespace {

// Backward integral of the energy envelope: sch[i] = sum_{k>=i} e[k].
[[nodiscard]] std::vector<double> backwardIntegral(std::span<const float> energy) {
  std::vector<double> sch(energy.size(), 0.0);
  double acc = 0.0;
  for (std::size_t i = energy.size(); i-- > 0;) {
    acc += static_cast<double>(energy[i]);
    sch[i] = acc;
  }
  return sch;
}

[[nodiscard]] std::size_t peakIndex(std::span<const float> energy) {
  std::size_t iPeak = 0;
  float best = 0.0F;
  for (std::size_t i = 0; i < energy.size(); ++i) {
    if (energy[i] > best) {
      best = energy[i];
      iPeak = i;
    }
  }
  return iPeak;
}

// Least-squares slope of y over x; 0 when the fit is degenerate.
[[nodiscard]] double fitSlope(const std::vector<double>& xs,
                              const std::vector<double>& ys) {
  const std::size_t n = xs.size();
  if (n < 2) {
    return 0.0;
  }
  double meanX = 0.0;
  double meanY = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    meanX += xs[i];
    meanY += ys[i];
  }
  meanX /= static_cast<double>(n);
  meanY /= static_cast<double>(n);
  double num = 0.0;
  double den = 0.0;
  for (std::size_t i = 0; i < n; ++i) {
    const double dx = xs[i] - meanX;
    num += dx * (ys[i] - meanY);
    den += dx * dx;
  }
  if (den <= 0.0) {
    return 0.0;
  }
  return num / den;
}

// 60 dB extrapolation of the Schroeder curve restricted to [loDb, hiDb].
[[nodiscard]] double decayTimeFromFit(std::span<const float> energy,
                                      double sampleRate, double loDb,
                                      double hiDb) {
  if (energy.size() < 2 || sampleRate <= 0.0) {
    return 0.0;
  }
  const std::vector<double> sch = backwardIntegral(energy);
  const std::size_t i0 = peakIndex(energy);
  const double ref = sch[i0];
  if (ref <= 0.0) {
    return 0.0;
  }

  std::vector<double> xs;
  std::vector<double> ys;
  for (std::size_t i = i0; i < energy.size(); ++i) {
    if (sch[i] <= 0.0) {
      break;
    }
    const double db = 10.0 * std::log10(sch[i] / ref);
    if (db >= loDb && db <= hiDb) {
      xs.push_back(static_cast<double>(i - i0) / sampleRate);
      ys.push_back(db);
    }
  }
  const double slope = fitSlope(xs, ys);
  if (slope >= 0.0) {
    return 0.0;
  }
  return -60.0 / slope;
}

// Face areas in rectFaces() order: floor, ceiling, x+, x-, z+, z-.
[[nodiscard]] std::array<double, 6> rectFaceAreas(const RectRoom& room) {
  return {room.w * room.d, room.w * room.d, room.h * room.d,
          room.h * room.d, room.w * room.h, room.w * room.h};
}

// Nearest listed band (simulation uses per-band tables without interpolation).
[[nodiscard]] double absorptionAtNearest(const Material& material,
                                         double bandHz) {
  const std::size_t n =
      std::min(material.bandsHz.size(), material.absorption.size());
  if (n == 0) {
    return 0.0;
  }
  std::size_t best = 0;
  double bestDist = std::abs(material.bandsHz[0] - bandHz);
  for (std::size_t i = 1; i < n; ++i) {
    const double dist = std::abs(material.bandsHz[i] - bandHz);
    if (dist < bestDist) {
      bestDist = dist;
      best = i;
    }
  }
  return material.absorption[best];
}

}  // namespace

double schroederT30(std::span<const float> energy, double sampleRate) {
  return decayTimeFromFit(energy, sampleRate, -35.0, -5.0);
}

double edt(std::span<const float> energy, double sampleRate) {
  return decayTimeFromFit(energy, sampleRate, -10.0, 0.0);
}

std::vector<EarlyReflection> earlyReflections(std::span<const float> energy,
                                              double sampleRate,
                                              std::size_t maxPeaks,
                                              double windowSec) {
  std::vector<EarlyReflection> out;
  if (energy.empty() || sampleRate <= 0.0 || maxPeaks == 0 || windowSec <= 0.0) {
    return out;
  }
  const long long windowSamples = std::llround(windowSec * sampleRate);
  const std::size_t window = std::min(
      energy.size(), windowSamples > 0 ? static_cast<std::size_t>(windowSamples)
                                       : std::size_t{0});

  std::vector<std::pair<float, std::size_t>> peaks;
  for (std::size_t i = 0; i < window; ++i) {
    const float amp = energy[i];
    if (amp <= 0.0F) {
      continue;
    }
    const bool leftRising = (i == 0) || (amp > energy[i - 1]);
    const bool rightFalling = (i + 1 >= window) || (amp >= energy[i + 1]);
    if (leftRising && rightFalling) {
      peaks.emplace_back(amp, i);
    }
  }

  std::sort(peaks.begin(), peaks.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });
  if (peaks.size() > maxPeaks) {
    peaks.resize(maxPeaks);
  }
  std::sort(peaks.begin(), peaks.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });

  out.reserve(peaks.size());
  for (const auto& [amp, idx] : peaks) {
    EarlyReflection er;
    er.t = static_cast<double>(idx) / sampleRate;
    er.amp = static_cast<double>(amp);
    er.order = 0;
    out.push_back(er);
  }
  return out;
}

std::map<double, Rt60> analyticRt60(const RectRoom& room,
                                    const Scenario& scenario,
                                    const std::vector<double>& bandsHz,
                                    double speedOfSound) {
  std::vector<double> bands = bandsHz;
  if (bands.empty()) {
    bands.assign(kDefaultBandsHz.begin(), kDefaultBandsHz.end());
  }

  const std::array<double, 6> areas = rectFaceAreas(room);
  double totalArea = 0.0;
  for (double a : areas) {
    totalArea += a;
  }
  const double volume = room.w * room.h * room.d;
  const std::vector<RectFace> faces = rectFaces(room);

  std::map<double, Rt60> out;
  for (double band : bands) {
    double weighted = 0.0;
    for (std::size_t f = 0; f < faces.size(); ++f) {
      double alpha = 0.0;
      if (const Material* material =
              findMaterial(scenario, faces[f].materialId)) {
        alpha = absorptionAtNearest(*material, band);
      }
      weighted += areas[f] * alpha;
    }
    const double meanAlpha = totalArea > 0.0 ? weighted / totalArea : 0.0;
    Rt60 rt;
    rt.sabine = sabineRt60(volume, totalArea, meanAlpha, speedOfSound);
    rt.eyring = eyringRt60(volume, totalArea, meanAlpha, speedOfSound);
    out[band] = rt;
  }
  return out;
}

SpatialField makeFieldGrid(const Aabb& bounds, int nx, int ny, int nz) {
  SpatialField field;
  field.nx = std::clamp(nx, 1, kMaxFieldDim);
  field.ny = std::clamp(ny, 1, kMaxFieldDim);
  field.nz = std::clamp(nz, 1, kMaxFieldDim);
  field.origin = bounds.min;

  const double ex = std::max(0.0, bounds.max.x - bounds.min.x);
  const double ey = std::max(0.0, bounds.max.y - bounds.min.y);
  const double ez = std::max(0.0, bounds.max.z - bounds.min.z);
  // Cubic cells: one size must cover the room on every axis, so take the
  // largest per-axis step (cells overshoot on the smaller axes).
  field.cellSize =
      std::max({ex / static_cast<double>(field.nx),
                ey / static_cast<double>(field.ny),
                ez / static_cast<double>(field.nz)});
  field.bandHz = 0.0;
  field.unit = "energy";
  field.values.assign(field.cellCount(), 0.0F);
  return field;
}

void depositEnergy(SpatialField& field, const Vec3& p, double energy) {
  if (field.cellSize <= 0.0 || field.values.empty()) {
    return;
  }
  const double gx = (p.x - field.origin.x) / field.cellSize;
  const double gy = (p.y - field.origin.y) / field.cellSize;
  const double gz = (p.z - field.origin.z) / field.cellSize;
  const int x = static_cast<int>(std::floor(gx));
  const int y = static_cast<int>(std::floor(gy));
  const int z = static_cast<int>(std::floor(gz));
  if (x < 0 || y < 0 || z < 0 || x >= field.nx || y >= field.ny ||
      z >= field.nz) {
    return;
  }
  field.values[field.index(x, y, z)] += static_cast<float>(energy);
}

}  // namespace av
