#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh.hpp>
#include <av/materials/material.hpp>
#include <av/metrics/metrics.hpp>
#include <av/units.hpp>

#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "rt_core.hpp"

namespace av {
namespace {

class RayTracingEngine : public IEngine {
 public:
  [[nodiscard]] EngineDescriptor descriptor() const override {
    EngineDescriptor desc;
    desc.id = "ray-tracing";
    desc.name = "Stochastic Ray Tracing";
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
      result.errors.emplace_back("ray-tracing supports mesh rooms only");
      result.ok = false;
      return result;
    }
    if (scenario.loadedMesh == nullptr) {
      result.errors.emplace_back(
          "ray-tracing requires an in-memory mesh (Scenario::loadedMesh)");
    } else if (scenario.loadedMesh->triangles.empty() ||
               scenario.loadedMesh->positions.empty()) {
      result.errors.emplace_back("loaded mesh has no geometry");
    }
    if (scenario.sources.empty()) {
      result.errors.emplace_back("ray-tracing requires at least one source");
    }
    if (scenario.receivers.empty()) {
      result.errors.emplace_back("ray-tracing requires at least one receiver");
    }
    if (scenario.loadedMesh != nullptr) {
      for (const std::string& slot : scenario.loadedMesh->materialSlots) {
        const std::string id = rtcore::materialIdForSlot(scenario, slot);
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

    const rtcore::RtParams rt = rtcore::parseRtParams(params);
    const Bvh bvh(*mesh);

    // Optional spatial field: cubic grid over the mesh AABB, filled by
    // depositing ray path energy during tracing (see deposit below).
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
    ir.bandsHz.clear();  // broadband, like image-source
    ir.samples.assign(pairCount * sampleCount, 0.0F);
    ir.channelNames.reserve(pairCount);
    for (const Source& source : scenario.sources) {
      for (const Receiver& receiver : scenario.receivers) {
        ir.channelNames.push_back(source.id + ":" + receiver.id);
      }
    }

    RayPaths paths;
    const auto deposit = [&](std::size_t pairIndex, double t, double energy,
                             int /*order*/) {
      const long long index =
          static_cast<long long>(std::llround(t * static_cast<double>(sampleRate)));
      if (index < 0 || static_cast<std::size_t>(index) >= sampleCount) {
        return;
      }
      ir.samples[pairIndex * sampleCount + static_cast<std::size_t>(index)] +=
          static_cast<float>(energy);
    };

    const bool completed =
        rtcore::traceRays(scenario, *mesh, bvh, rt, speedOfSound, cancel,
                          progress, "ray-tracing", deposit,
                          wantField ? &field : nullptr, paths);
    if (!completed) {
      return Result{};
    }

    const std::size_t totalRays =
        sourceCount * static_cast<std::size_t>(rt.rayCount);

    // Monte Carlo mean over rays so the envelope scale does not grow with
    // rayCount (decay rate / T30 are scale-invariant, but comparisons are not).
    if (totalRays > 0) {
      const float inv = 1.0F / static_cast<float>(totalRays);
      for (float& sample : ir.samples) {
        sample *= inv;
      }
    }

    // Same Monte Carlo mean for the field, additionally per cell volume so
    // the stored value is an energy *density* (linear, not dB).
    if (wantField && totalRays > 0) {
      rtcore::normalizeFieldEnergy(field, totalRays);
      result.field = std::move(field);
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
    const double volume = rtcore::meshVolume(*mesh);
    std::vector<double> bands = params.bandsHz;
    if (bands.empty()) {
      bands.assign(kDefaultBandsHz.begin(), kDefaultBandsHz.end());
    }
    for (double band : bands) {
      double weighted = 0.0;
      for (std::size_t i = 0; i < mesh->triangles.size(); ++i) {
        const double a = rtcore::triangleArea(*mesh, mesh->triangles[i]);
        weighted += a * rtcore::slotAlphaAtBand(scenario, *mesh,
                                                static_cast<std::uint32_t>(i),
                                                band);
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

REGISTER_ENGINE(RayTracingEngine);

}  // namespace

void forceLinkRayTracingEngine() {}

}  // namespace av
