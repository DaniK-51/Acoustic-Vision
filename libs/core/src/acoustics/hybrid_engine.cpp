#include <av/acoustics/engine.hpp>
#include <av/geometry/bvh.hpp>
#include <av/geometry/mesh.hpp>
#include <av/materials/material.hpp>
#include <av/metrics/metrics.hpp>
#include <av/units.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <vector>

namespace av {
namespace {

struct HybridMethodParams {
  double earlyTimeSec = 0.08;
  int earlyOrder = 2;
  int rayCount = 2000;
  int maxBounces = 32;
  std::uint64_t seed = 42;
  double receiverRadius = 0.5;
  bool useSabineTail = false;
  int capturePathLimit = 64;
};

[[nodiscard]] HybridMethodParams parseMethodParams(const SimParams& params) {
  HybridMethodParams out;
  const nlohmann::json& mp = params.methodParams;
  if (mp.is_object()) {
    out.earlyTimeSec = std::max(0.0, mp.value("earlyTimeSec", out.earlyTimeSec));
    out.earlyOrder = std::max(0, mp.value("earlyOrder", out.earlyOrder));
    out.rayCount = std::max(1, mp.value("rayCount", out.rayCount));
    out.maxBounces = std::max(1, mp.value("maxBounces", out.maxBounces));
    out.seed = static_cast<std::uint64_t>(
        mp.value("seed", static_cast<std::int64_t>(out.seed)));
    out.receiverRadius =
        std::max(1e-6, mp.value("receiverRadius", out.receiverRadius));
    out.useSabineTail = mp.value("useSabineTail", out.useSabineTail);
    out.capturePathLimit =
        std::max(0, mp.value("capturePathLimit", out.capturePathLimit));
  }
  if (!mp.contains("maxBounces") && params.maxReflectionOrder > 0) {
    out.maxBounces = params.maxReflectionOrder;
  }
  return out;
}

// Slot name -> scenario material id: explicit override first, otherwise the
// slot name itself (applyMaterialOverrides may already have rewritten it).
[[nodiscard]] std::string materialIdForSlot(const Scenario& scenario,
                                            const std::string& slot) {
  const auto it = scenario.room.mesh.materialOverrides.find(slot);
  return it != scenario.room.mesh.materialOverrides.end() ? it->second : slot;
}

[[nodiscard]] double slotAlpha(const Scenario& scenario, const Mesh& mesh,
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

// Nearest listed band (simulation uses per-band tables without interpolation);
// mirrors the lookup used by analyticRt60 in metrics.cpp.
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

// Closed-mesh volume via the divergence theorem (sum of signed tetrahedra);
// abs() absorbs the into-room vs out-of-room winding convention.
[[nodiscard]] double meshVolume(const Mesh& mesh) {
  double vol = 0.0;
  for (const Triangle& triangle : mesh.triangles) {
    const Vec3& p0 = mesh.positions[triangle.v0];
    const Vec3& p1 = mesh.positions[triangle.v1];
    const Vec3& p2 = mesh.positions[triangle.v2];
    vol += dot(p0, cross(p1, p2));
  }
  return std::abs(vol) / 6.0;
}

[[nodiscard]] double triangleArea(const Mesh& mesh, const Triangle& triangle) {
  const Vec3& p0 = mesh.positions[triangle.v0];
  const Vec3& p1 = mesh.positions[triangle.v1];
  const Vec3& p2 = mesh.positions[triangle.v2];
  return 0.5 * length(cross(p1 - p0, p2 - p0));
}

[[nodiscard]] double slotAlphaAtBand(const Scenario& scenario, const Mesh& mesh,
                                     std::uint32_t triangleIndex,
                                     double bandHz) {
  const Triangle& triangle = mesh.triangles[triangleIndex];
  if (triangle.materialSlot >= mesh.materialSlots.size()) {
    return 0.0;
  }
  const std::string& slot = mesh.materialSlots[triangle.materialSlot];
  const std::string id = materialIdForSlot(scenario, slot);
  const Material* material = findMaterial(scenario, id);
  return material != nullptr ? absorptionAtNearest(*material, bandHz) : 0.0;
}

// Uniform direction on the unit sphere (Archimedes).
[[nodiscard]] Vec3 sampleDirection(std::mt19937_64& rng) {
  std::uniform_real_distribution<double> uniform(0.0, 1.0);
  const double z = 1.0 - 2.0 * uniform(rng);
  const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
  const double phi = 2.0 * 3.14159265358979323846 * uniform(rng);
  return {r * std::cos(phi), r * std::sin(phi), z};
}

// Area-weighted mean absorption over the mesh shell (broadband), used for the
// optional analytic Sabine tail.
[[nodiscard]] double meshMeanAlpha(const Scenario& scenario, const Mesh& mesh) {
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

class HybridEngine : public IEngine {
 public:
  [[nodiscard]] EngineDescriptor descriptor() const override {
    EngineDescriptor desc;
    desc.id = "hybrid";
    desc.name = "Hybrid Early + RT Late";
    desc.version = "0.1.0";
    desc.caps.supportsRect = false;
    desc.caps.supportsMesh = true;
    desc.caps.supportsRayPaths = true;
    desc.caps.supportsBands = true;
    desc.caps.maxOrder = 0;
    desc.paramsSchema = nlohmann::json{
        {"type", "object"},
        {"properties",
         nlohmann::json{
             {"earlyTimeSec",
              nlohmann::json{{"type", "number"},
                             {"minimum", 0},
                             {"default", 0.08}}},
             {"earlyOrder",
              nlohmann::json{{"type", "integer"},
                             {"minimum", 0},
                             {"default", 2}}},
             {"rayCount",
              nlohmann::json{{"type", "integer"},
                             {"minimum", 1},
                             {"default", 2000}}},
             {"maxBounces",
              nlohmann::json{{"type", "integer"},
                             {"minimum", 1},
                             {"default", 32}}},
             {"seed",
              nlohmann::json{{"type", "integer"},
                             {"minimum", 0},
                             {"default", 42}}},
             {"receiverRadius",
              nlohmann::json{{"type", "number"},
                             {"exclusiveMinimum", 0},
                             {"default", 0.5}}},
             {"useSabineTail",
              nlohmann::json{{"type", "boolean"}, {"default", false}}},
             {"capturePathLimit",
              nlohmann::json{{"type", "integer"},
                             {"minimum", 0},
                             {"default", 64}}}}},
        {"additionalProperties", false}};
    desc.artifacts = {"metrics", "ir", "rays", "field"};
    return desc;
  }

  [[nodiscard]] ValidateResult validate(const Scenario& scenario) const override {
    ValidateResult result;
    if (scenario.room.type != Room::Type::Mesh) {
      result.errors.emplace_back("hybrid supports mesh rooms only");
      result.ok = false;
      return result;
    }
    if (scenario.loadedMesh == nullptr) {
      result.errors.emplace_back(
          "hybrid requires an in-memory mesh (Scenario::loadedMesh)");
    } else if (scenario.loadedMesh->triangles.empty() ||
               scenario.loadedMesh->positions.empty()) {
      result.errors.emplace_back("loaded mesh has no geometry");
    }
    if (scenario.sources.empty()) {
      result.errors.emplace_back("hybrid requires at least one source");
    }
    if (scenario.receivers.empty()) {
      result.errors.emplace_back("hybrid requires at least one receiver");
    }
    if (scenario.loadedMesh != nullptr) {
      for (const std::string& slot : scenario.loadedMesh->materialSlots) {
        const std::string id = materialIdForSlot(scenario, slot);
        if (findMaterial(scenario, id) == nullptr) {
          result.warnings.push_back("no resolvable material for mesh slot '" +
                                    slot + "' (treated as alpha=0)");
        }
      }
    }
    if (scenario.params.signedIr) {
      result.warnings.emplace_back(
          "signedIr is not produced; output is an unsigned energy envelope");
    }
    if (scenario.params.fieldGrid.enabled) {
      const FieldGridParams& grid = scenario.params.fieldGrid;
      if (grid.nx <= 0 || grid.ny <= 0 || grid.nz <= 0) {
        result.errors.emplace_back("fieldGrid requires positive nx, ny, nz");
      } else if (grid.nx > kMaxFieldDim || grid.ny > kMaxFieldDim ||
                 grid.nz > kMaxFieldDim) {
        result.warnings.emplace_back(
            "fieldGrid dims exceed kMaxFieldDim (" +
            std::to_string(kMaxFieldDim) + "); values will be clamped");
      }
    }
    result.ok = result.errors.empty();
    return result;
  }

  Result run(const Scenario& scenario, const SimParams& params,
             const CancelToken& cancel, ProgressFn progress) override {
    Result result;
    if (cancel.cancelled()) {
      return result;
    }
    const Mesh* mesh = scenario.loadedMesh.get();
    if (mesh == nullptr || mesh->triangles.empty() ||
        mesh->positions.empty()) {
      return result;
    }
    const double speedOfSound = params.speedOfSound;
    const int sampleRate = params.sampleRate;
    const std::size_t sampleCount = static_cast<std::size_t>(
        std::llround(params.durationSec * static_cast<double>(sampleRate)));
    if (sampleRate <= 0 || sampleCount == 0 || speedOfSound <= 0.0) {
      return result;
    }

    const HybridMethodParams hyb = parseMethodParams(params);
    const Bvh bvh(*mesh);

    // Optional spatial field: cubic grid over the mesh AABB, filled by
    // depositing ray path energy during tracing (same convention as
    // ray-tracing).
    SpatialField field;
    bool wantField = params.fieldGrid.enabled && params.fieldGrid.nx > 0 &&
                     params.fieldGrid.ny > 0 && params.fieldGrid.nz > 0;
    if (wantField) {
      field = makeFieldGrid(boundsOf(*mesh), params.fieldGrid.nx,
                            params.fieldGrid.ny, params.fieldGrid.nz);
      wantField = field.cellSize > 0.0;
    }

    const std::size_t sourceCount = scenario.sources.size();
    const std::size_t receiverCount = scenario.receivers.size();
    const std::size_t pairCount = sourceCount * receiverCount;

    IrBuffer ir;
    ir.sampleRate = static_cast<double>(sampleRate);
    ir.durationSec = params.durationSec;
    ir.channels = pairCount;
    ir.bandsHz.clear();  // broadband, like image-source / ray-tracing
    ir.samples.assign(pairCount * sampleCount, 0.0F);
    ir.channelNames.reserve(pairCount);
    for (const Source& source : scenario.sources) {
      for (const Receiver& receiver : scenario.receivers) {
        ir.channelNames.push_back(source.id + ":" + receiver.id);
      }
    }

    // Early and late deposits are accumulated separately and summed at the
    // end (early = discrete-dense arrivals, late = statistical tail).
    std::vector<float> earlySamples(pairCount * sampleCount, 0.0F);
    std::vector<float> lateSamples(pairCount * sampleCount, 0.0F);

    RayPaths paths;
    const bool capturePaths =
        params.capturePaths ||
        (params.methodParams.is_object() &&
         params.methodParams.value("capturePaths", false));

    if (progress) {
      progress(Progress{0.0, "hybrid"});
    }

    std::mt19937_64 rng(hyb.seed);
    const std::size_t totalRays =
        sourceCount * static_cast<std::size_t>(hyb.rayCount);
    std::size_t raysDone = 0;
    int lastDecile = 0;

    for (std::size_t s = 0; s < sourceCount; ++s) {
      const Source& source = scenario.sources[s];
      // Power dB -> linear energy scale (docs/engines.md section 5 uses
      // energy ratios; source.powerDb is a level).
      const double sourceEnergy = std::pow(10.0, source.powerDb / 10.0);

      for (int rayIndex = 0; rayIndex < hyb.rayCount; ++rayIndex) {
        if (cancel.cancelled()) {
          return Result{};
        }

        Vec3 origin = source.position;
        Vec3 direction = sampleDirection(rng);
        double pathEnergy = sourceEnergy;
        double pathLength = 0.0;
        int order = 0;

        RayPolyline polyline;
        if (capturePaths) {
          polyline.points.push_back(source.position);
        }

        for (int bounce = 0; bounce < hyb.maxBounces; ++bounce) {
          if (cancel.cancelled()) {
            return Result{};
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
            if (dist > hyb.receiverRadius) {
              continue;
            }
            const double L = pathLength + sProj;
            if (L <= 1e-9) {
              continue;
            }
            const double amplitude = std::sqrt(pathEnergy) / L;
            const double energy = amplitude * amplitude;
            const double t = L / speedOfSound;
            const std::size_t pairIndex = s * receiverCount + r;

            // Early = dense deposit: arrivals before earlyTimeSec, or at
            // bounce order <= earlyOrder, are resolved at sub-sample spacing
            // (2-tap linear split). Everything else is the late tail and is
            // snapped to a single sample like ray-tracing.
            const bool isEarly =
                t < hyb.earlyTimeSec || order <= hyb.earlyOrder;
            if (isEarly) {
              const double pos = t * static_cast<double>(sampleRate);
              const long long i0 = static_cast<long long>(std::floor(pos));
              const double frac = pos - static_cast<double>(i0);
              const std::size_t base = pairIndex * sampleCount;
              if (i0 >= 0 && static_cast<std::size_t>(i0) < sampleCount) {
                earlySamples[base + static_cast<std::size_t>(i0)] +=
                    static_cast<float>(energy * (1.0 - frac));
              }
              if (i0 + 1 >= 0 &&
                  static_cast<std::size_t>(i0 + 1) < sampleCount) {
                earlySamples[base + static_cast<std::size_t>(i0 + 1)] +=
                    static_cast<float>(energy * frac);
              }
            } else {
              const long long index = static_cast<long long>(
                  std::llround(t * static_cast<double>(sampleRate)));
              if (index >= 0 &&
                  static_cast<std::size_t>(index) < sampleCount) {
                lateSamples[pairIndex * sampleCount +
                            static_cast<std::size_t>(index)] +=
                    static_cast<float>(energy);
              }
            }
          }

          // Field deposit: Riemann sum of pathEnergy/L^2 along the segment
          // (the same 1/r^2 energy convention as the receiver spheres),
          // sampled about once per cell-length of path.
          if (wantField) {
            const double segLen = hit.t;
            const int samples = std::max(
                1, static_cast<int>(std::ceil(segLen / field.cellSize)));
            const double step = segLen / static_cast<double>(samples);
            for (int i = 0; i < samples; ++i) {
              const double tSample = step * (static_cast<double>(i) + 0.5);
              const double L = pathLength + tSample;
              if (L <= 1e-9) {
                continue;
              }
              depositEnergy(field, origin + direction * tSample,
                            pathEnergy / (L * L) * step);
            }
          }

          pathLength += hit.t;
          origin = hit.position;
          if (capturePaths) {
            polyline.points.push_back(hit.position);
          }
          ++order;

          const double alpha = slotAlpha(scenario, *mesh, hit.triangleIndex);
          pathEnergy *= (1.0 - alpha);

          // Specular reflection; sign-independent in r = d - 2(d.n)n.
          const Vec3 n = hit.normal;
          direction = normalized(direction - n * (2.0 * dot(direction, n)));
          // Nudge off the surface so the next query does not re-hit it.
          origin = origin + direction * 1e-6;
        }

        if (capturePaths &&
            paths.paths.size() <
                static_cast<std::size_t>(hyb.capturePathLimit) &&
            polyline.points.size() >= 2) {
          polyline.amplitude = std::sqrt(pathEnergy);
          polyline.order = order;
          paths.paths.push_back(std::move(polyline));
        }

        ++raysDone;
        if (progress && totalRays > 0) {
          const int decile = static_cast<int>((raysDone * 10) / totalRays);
          if (decile > lastDecile) {
            lastDecile = decile;
            progress(Progress{static_cast<double>(raysDone) /
                                  static_cast<double>(totalRays),
                              "hybrid"});
          }
        }
      }
    }

    // Monte Carlo mean over rays so the envelope scale does not grow with
    // rayCount (decay rate / T30 are scale-invariant, but comparisons are not).
    if (totalRays > 0) {
      const float inv = 1.0F / static_cast<float>(totalRays);
      for (float& sample : earlySamples) {
        sample *= inv;
      }
      for (float& sample : lateSamples) {
        sample *= inv;
      }
    }

    // Optional analytic Sabine tail: replace the stochastic late part from
    // earlyTimeSec onward with an exponential energy decay matched to the
    // combined level just before the stitch point.
    if (hyb.useSabineTail && pairCount > 0 && sampleCount > 0) {
      const double area = surfaceArea(*mesh);
      const double volume = meshVolume(*mesh);
      const double meanAlpha = meshMeanAlpha(scenario, *mesh);
      const double t60 = sabineRt60(volume, area, meanAlpha, speedOfSound);
      if (std::isfinite(t60) && t60 > 0.0) {
        const long long i0 = static_cast<long long>(
            std::llround(hyb.earlyTimeSec * static_cast<double>(sampleRate)));
        const long long window = std::max(
            1LL, static_cast<long long>(
                     std::llround(0.010 * static_cast<double>(sampleRate))));
        const long long lo = std::max(0LL, i0 - window);
        for (std::size_t ch = 0; ch < pairCount; ++ch) {
          double level = 0.0;
          long long n = 0;
          for (long long i = lo; i < i0 && i < static_cast<long long>(sampleCount);
               ++i) {
            level += static_cast<double>(earlySamples[ch * sampleCount +
                                                     static_cast<std::size_t>(i)]) +
                     static_cast<double>(lateSamples[ch * sampleCount +
                                                     static_cast<std::size_t>(i)]);
            ++n;
          }
          if (n > 0) {
            level /= static_cast<double>(n);
          }
          if (!(level > 0.0)) {
            continue;  // nothing to stitch from; keep the RT late part
          }
          for (std::size_t i = static_cast<std::size_t>(
                   std::max(0LL, i0));
               i < sampleCount; ++i) {
            const double t = static_cast<double>(i) /
                                 static_cast<double>(sampleRate) -
                             hyb.earlyTimeSec;
            lateSamples[ch * sampleCount + i] = static_cast<float>(
                level * std::pow(10.0, -6.0 * t / t60));
          }
        }
      }
    }

    // Same Monte Carlo mean for the field, additionally per cell volume so
    // the stored value is an energy *density* (linear, not dB).
    if (wantField && totalRays > 0) {
      const double cellVolume =
          field.cellSize * field.cellSize * field.cellSize;
      const double inv =
          1.0 / (static_cast<double>(totalRays) * cellVolume);
      for (float& value : field.values) {
        value *= static_cast<float>(inv);
      }
      result.field = std::move(field);
    }

    for (std::size_t i = 0; i < ir.samples.size(); ++i) {
      ir.samples[i] = earlySamples[i] + lateSamples[i];
    }

    Metrics metrics;
    if (pairCount > 0) {
      const std::span<const float> firstChannel(ir.samples.data(), sampleCount);
      metrics.rt60[0.0].schroederT30 = schroederT30(firstChannel, ir.sampleRate);
      metrics.edt[0.0] = edt(firstChannel, ir.sampleRate);
      metrics.earlyReflections =
          earlyReflections(firstChannel, ir.sampleRate, 8, 0.080);
    }

    // Analytic Sabine/Eyring over the mesh shell (area-weighted alpha).
    const double area = surfaceArea(*mesh);
    const double volume = meshVolume(*mesh);
    std::vector<double> bands = params.bandsHz;
    if (bands.empty()) {
      bands.assign(kDefaultBandsHz.begin(), kDefaultBandsHz.end());
    }
    for (double band : bands) {
      double weighted = 0.0;
      for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
        const double a = triangleArea(*mesh, mesh->triangles[i]);
        weighted += a * slotAlphaAtBand(scenario, *mesh,
                                        static_cast<std::uint32_t>(i), band);
      }
      const double meanAlpha = area > 0.0 ? weighted / area : 0.0;
      Rt60 rt60;
      rt60.sabine = sabineRt60(volume, area, meanAlpha, speedOfSound);
      rt60.eyring = eyringRt60(volume, area, meanAlpha, speedOfSound);
      metrics.rt60[band] = rt60;
    }

    result.metrics = std::move(metrics);
    result.ir = std::move(ir);
    result.rays = std::move(paths);
    if (progress) {
      progress(Progress{1.0, "done"});
    }
    return result;
  }
};

REGISTER_ENGINE(HybridEngine);

}  // namespace

void forceLinkHybridEngine() {}

}  // namespace av
