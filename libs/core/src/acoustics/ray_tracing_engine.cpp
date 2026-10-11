#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh.hpp>
#include <av/metrics/metrics.hpp>

#include <cmath>
#include <string>

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
    return rtcore::validateMeshRtScenario(scenario, "ray-tracing");
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
    rtcore::addIrDerivedMetrics(ir, metrics);
    rtcore::addMeshAnalyticRt60(scenario, *mesh, params.bandsHz, speedOfSound,
                                metrics);

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
