#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh.hpp>
#include <av/metrics/metrics.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "rt_core.hpp"

namespace av {
namespace {

struct HybridMethodParams {
  double earlyTimeSec = 0.08;
  int earlyOrder = 2;
  bool useSabineTail = false;
  rtcore::RtParams rt;
};

[[nodiscard]] HybridMethodParams parseMethodParams(const SimParams& params) {
  HybridMethodParams out;
  out.rt = rtcore::parseRtParams(params);
  const nlohmann::json& mp = params.methodParams;
  if (mp.is_object()) {
    out.earlyTimeSec = std::max(0.0, mp.value("earlyTimeSec", out.earlyTimeSec));
    out.earlyOrder = std::max(0, mp.value("earlyOrder", out.earlyOrder));
    out.useSabineTail = mp.value("useSabineTail", out.useSabineTail);
  }
  return out;
}

class HybridEngine : public IEngine {
 public:
  [[nodiscard]] EngineDescriptor descriptor() const override {
    EngineDescriptor desc;
    desc.id = "hybrid";
    desc.name = "Hybrid (early/late RT)";
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
    return rtcore::validateMeshRtScenario(scenario, "hybrid");
  }

  Result run(const Scenario& scenario, const SimParams& params,
             const CancelToken& cancel, ProgressFn progress) override {
    Result result;
    if (cancel.cancelled()) {
      return result;
    }
    const auto setup = rtcore::prepareRtRun(scenario, params);
    if (!setup) {
      return result;
    }
    const Mesh* mesh = setup->mesh;
    const double speedOfSound = setup->speedOfSound;
    const int sampleRate = setup->sampleRate;
    const std::size_t sampleCount = setup->sampleCount;

    const HybridMethodParams hyb = parseMethodParams(params);
    const Bvh bvh(*mesh);

    SpatialField field = std::move(setup->field);
    const bool wantField = setup->wantField;

    const std::size_t sourceCount = setup->sourceCount;
    const std::size_t pairCount = setup->pairCount;

    IrBuffer ir = std::move(setup->ir);

    // Early and late deposits are accumulated separately and summed at the
    // end (early = discrete-dense arrivals, late = statistical tail).
    std::vector<float> earlySamples(pairCount * sampleCount, 0.0F);
    std::vector<float> lateSamples(pairCount * sampleCount, 0.0F);

    RayPaths paths;
    const auto deposit = [&](std::size_t pairIndex, double t, double energy,
                             int order) {
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
    };

    const bool completed =
        rtcore::traceRays(scenario, *mesh, bvh, hyb.rt, speedOfSound, cancel,
                          progress, "hybrid", deposit,
                          wantField ? &field : nullptr, paths);
    if (!completed) {
      return Result{};
    }

    const std::size_t totalRays =
        sourceCount * static_cast<std::size_t>(hyb.rt.rayCount);

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
      const double volume = rtcore::meshVolume(*mesh);
      const double meanAlpha = rtcore::meshMeanAlpha(scenario, *mesh);
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
      rtcore::normalizeFieldEnergy(field, totalRays);
      result.field = std::move(field);
    }

    for (std::size_t i = 0; i < ir.samples.size(); ++i) {
      ir.samples[i] = earlySamples[i] + lateSamples[i];
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

REGISTER_ENGINE(HybridEngine);

}  // namespace

void forceLinkHybridEngine() {}

}  // namespace av
