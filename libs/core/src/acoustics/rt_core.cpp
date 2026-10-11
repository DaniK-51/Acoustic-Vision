#include "rt_core.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include <av/materials/material.hpp>

namespace av::rtcore {

RtParams parseRtParams(const SimParams& params) {
  RtParams out;
  const nlohmann::json& mp = params.methodParams;
  if (mp.is_object()) {
    out.rayCount = std::max(1, mp.value("rayCount", out.rayCount));
    out.maxBounces = std::max(1, mp.value("maxBounces", out.maxBounces));
    out.seed = static_cast<std::uint64_t>(
        mp.value("seed", static_cast<std::int64_t>(out.seed)));
    out.receiverRadius =
        std::max(1e-6, mp.value("receiverRadius", out.receiverRadius));
    out.capturePathLimit =
        std::max(0, mp.value("capturePathLimit", out.capturePathLimit));
    out.capturePaths = mp.value("capturePaths", out.capturePaths);
  }
  if (!mp.contains("maxBounces") && params.maxReflectionOrder > 0) {
    out.maxBounces = params.maxReflectionOrder;
  }
  out.capturePaths = out.capturePaths || params.capturePaths;
  return out;
}

std::string materialIdForSlot(const Scenario& scenario,
                              const std::string& slot) {
  const auto it = scenario.room.mesh.materialOverrides.find(slot);
  return it != scenario.room.mesh.materialOverrides.end() ? it->second : slot;
}

namespace {

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

double slotAlpha(const Scenario& scenario, const Mesh& mesh,
                 std::uint32_t triangleIndex) {
  const Triangle& triangle = mesh.triangles[triangleIndex];
  if (triangle.materialSlot >= mesh.materialSlots.size()) {
    return 0.0;
  }
  const std::string& slot = mesh.materialSlots[triangle.materialSlot];
  const std::string id = materialIdForSlot(scenario, slot);
  const Material* material = findMaterial(scenario, id);
  return material != nullptr ? meanAbsorption(*material) : 0.0;
}

double slotAlphaAtBand(const Scenario& scenario, const Mesh& mesh,
                       std::uint32_t triangleIndex, double bandHz) {
  const Triangle& triangle = mesh.triangles[triangleIndex];
  if (triangle.materialSlot >= mesh.materialSlots.size()) {
    return 0.0;
  }
  const std::string& slot = mesh.materialSlots[triangle.materialSlot];
  const std::string id = materialIdForSlot(scenario, slot);
  const Material* material = findMaterial(scenario, id);
  return material != nullptr ? absorptionAtNearest(*material, bandHz) : 0.0;
}

double meshVolume(const Mesh& mesh) {
  double vol = 0.0;
  for (const Triangle& triangle : mesh.triangles) {
    const Vec3& p0 = mesh.positions[triangle.v0];
    const Vec3& p1 = mesh.positions[triangle.v1];
    const Vec3& p2 = mesh.positions[triangle.v2];
    vol += dot(p0, cross(p1, p2));
  }
  return std::abs(vol) / 6.0;
}

double triangleArea(const Mesh& mesh, const Triangle& triangle) {
  const Vec3& p0 = mesh.positions[triangle.v0];
  const Vec3& p1 = mesh.positions[triangle.v1];
  const Vec3& p2 = mesh.positions[triangle.v2];
  return 0.5 * length(cross(p1 - p0, p2 - p0));
}

double meshMeanAlpha(const Scenario& scenario, const Mesh& mesh) {
  const double area = surfaceArea(mesh);
  if (area <= 0.0) {
    return 0.0;
  }
  double weighted = 0.0;
  for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
    weighted += triangleArea(mesh, mesh.triangles[i]) *
                slotAlpha(scenario, mesh, static_cast<std::uint32_t>(i));
  }
  return weighted / area;
}

Vec3 sampleDirection(std::mt19937_64& rng) {
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  const double z = 1.0 - 2.0 * uniform(rng);
  const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
  const double phi = 2.0 * 3.14159265358979323846 * uniform(rng);
  return {r * std::cos(phi), r * std::sin(phi), z};
}

bool traceRays(const Scenario& scenario, const Mesh& mesh, const Bvh& bvh,
               const RtParams& rt, double speedOfSound,
               const CancelToken& cancel, const ProgressFn& progress,
               const char* stageLabel, const ReceiverHitFn& onReceiverHit,
               SpatialField* field, RayPaths& paths) {
  const bool wantField = field != nullptr && field->cellSize > 0.0;
  const std::size_t sourceCount = scenario.sources.size();
  const std::size_t receiverCount = scenario.receivers.size();

  if (progress) {
    progress(Progress{0.0, stageLabel});
  }

  std::mt19937_64 rng(rt.seed);
  const std::size_t totalRays =
      sourceCount * static_cast<std::size_t>(rt.rayCount);
  std::size_t raysDone = 0;
  int lastDecile = 0;

  for (std::size_t s = 0; s < sourceCount; ++s) {
    const Source& source = scenario.sources[s];
    // Power dB -> linear energy scale (docs/engines.md section 5 uses
    // energy ratios; source.powerDb is a level).
    const double sourceEnergy = std::pow(10.0, source.powerDb / 10.0);

    for (int rayIndex = 0; rayIndex < rt.rayCount; ++rayIndex) {
      if (cancel.cancelled()) {
        return false;
      }

      Vec3 origin = source.position;
      Vec3 direction = sampleDirection(rng);
      double pathEnergy = sourceEnergy;
      double pathLength = 0.0;
      int order = 0;

      RayPolyline polyline;
      if (rt.capturePaths) {
        polyline.points.push_back(source.position);
      }

      for (int bounce = 0; bounce < rt.maxBounces; ++bounce) {
        if (cancel.cancelled()) {
          return false;
        }
        RayHit hit;
        // tMin offset: origin sits on the previous hit surface after the
        // epsilon nudge below; keep the same triangle out of the next query.
        if (!bvh.intersect(Ray{origin, direction}, 0.0, 1e12, hit)) {
          break;
        }

        // Receiver-sphere detection along the segment [origin, hit].
        // Deposit at closest approach: amplitude ~ 1/L (total path length,
        // matching the ISM 1/r rule), energy = amplitude^2.
        for (std::size_t r = 0; r < receiverCount; ++r) {
          const Vec3& recv = scenario.receivers[r].position;
          const double sProj =
              std::clamp(dot(recv - origin, direction), 0.0, hit.t);
          const Vec3 closest = origin + direction * sProj;
          const double dist = length(recv - closest);
          if (dist > rt.receiverRadius) {
            continue;
          }
          const double L = pathLength + sProj;
          if (L <= 1e-9) {
            continue;
          }
          const double amplitude = std::sqrt(pathEnergy) / L;
          const double energy = amplitude * amplitude;
          const double t = L / speedOfSound;
          onReceiverHit(s * receiverCount + r, t, energy, order);
        }

        // Field deposit: Riemann sum of pathEnergy/L^2 along the segment
        // (the same 1/r^2 energy convention as the receiver spheres),
        // sampled about once per cell-length of path.
        if (wantField) {
          const double segLen = hit.t;
          const int samples = std::max(
              1, static_cast<int>(std::ceil(segLen / field->cellSize)));
          const double step = segLen / static_cast<double>(samples);
          for (int i = 0; i < samples; ++i) {
            const double tSample = step * (static_cast<double>(i) + 0.5);
            const double L = pathLength + tSample;
            if (L <= 1e-9) {
              continue;
            }
            depositEnergy(*field, origin + direction * tSample,
                          pathEnergy / (L * L) * step);
          }
        }

        pathLength += hit.t;
        origin = hit.position;
        if (rt.capturePaths) {
          polyline.points.push_back(hit.position);
        }
        ++order;

        const double alpha = slotAlpha(scenario, mesh, hit.triangleIndex);
        pathEnergy *= (1.0 - alpha);

        // Specular reflection; sign-independent in r = d - 2(d.n)n.
        const Vec3 n = hit.normal;
        direction = normalized(direction - n * (2.0 * dot(direction, n)));
        // Nudge off the surface so the next query does not re-hit it.
        origin = origin + direction * 1e-6;
      }

      if (rt.capturePaths &&
          paths.paths.size() <
              static_cast<std::size_t>(rt.capturePathLimit) &&
          polyline.points.size() >= 2) {
        polyline.amplitude = std::sqrt(pathEnergy);
        polyline.order = order;
        paths.paths.push_back(std::move(polyline));
      }

      ++raysDone;
      if (progress && totalRays > 0) {
        const int decile =
            static_cast<int>((raysDone * 10) / totalRays);
        if (decile > lastDecile) {
          lastDecile = decile;
          progress(Progress{static_cast<double>(raysDone) /
                                static_cast<double>(totalRays),
                            stageLabel});
        }
      }
    }
  }
  return true;
}

void normalizeFieldEnergy(SpatialField& field, std::size_t totalRays) {
  if (totalRays == 0) {
    return;
  }
  const double cellVolume = field.cellSize * field.cellSize * field.cellSize;
  const double inv = 1.0 / (static_cast<double>(totalRays) * cellVolume);
  for (float& value : field.values) {
    value *= static_cast<float>(inv);
  }
}

}  // namespace av::rtcore
