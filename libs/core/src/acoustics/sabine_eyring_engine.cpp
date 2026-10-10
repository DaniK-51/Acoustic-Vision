#include <av/acoustics/engine.hpp>
#include <av/metrics/metrics.hpp>

namespace av {
namespace {

class SabineEyringEngine : public IEngine {
 public:
  [[nodiscard]] EngineDescriptor descriptor() const override {
    EngineDescriptor desc;
    desc.id = "sabine-eyring";
    desc.name = "Sabine-Eyring Analytic RT60";
    desc.version = "1.0.0";
    desc.caps.supportsRect = true;
    desc.caps.supportsMesh = false;
    desc.caps.supportsRayPaths = false;
    desc.caps.supportsBands = true;
    desc.caps.maxOrder = 0;
    desc.paramsSchema = nlohmann::json::object();
    desc.artifacts = {"metrics"};
    return desc;
  }

  [[nodiscard]] ValidateResult validate(const Scenario& scenario) const override {
    ValidateResult result;
    if (scenario.room.type != Room::Type::Rect) {
      result.errors.emplace_back("sabine-eyring supports rectangular rooms only");
      result.ok = false;
      return result;
    }
    const RectRoom& room = scenario.room.rect;
    if (room.w <= 0.0 || room.h <= 0.0 || room.d <= 0.0) {
      result.errors.emplace_back("rect room must have positive size");
    }
    for (const RectFace& face : rectFaces(room)) {
      if (face.materialId.empty() ||
          findMaterial(scenario, face.materialId) == nullptr) {
        result.errors.push_back("face '" + face.name +
                                "' has no resolvable material");
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
    if (progress) {
      progress(Progress{0.0, "analytic-rt60"});
    }
    Metrics metrics;
    metrics.rt60 = analyticRt60(scenario.room.rect, scenario, params.bandsHz,
                                params.speedOfSound);
    result.metrics = std::move(metrics);
    if (progress) {
      progress(Progress{1.0, "done"});
    }
    return result;
  }
};

REGISTER_ENGINE(SabineEyringEngine);

}  // namespace

void forceLinkSabineEyringEngine() {}

}  // namespace av
