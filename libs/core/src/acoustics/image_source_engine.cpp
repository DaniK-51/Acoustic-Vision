#include <av/acoustics/engine.hpp>
#include <av/materials/material.hpp>
#include <av/metrics/metrics.hpp>

#include <array>
#include <cmath>

#include "rt_core.hpp"

namespace av {
namespace {

// Parity lattice coordinate (docs/engines.md section 5): even indices place the
// image at n*L + s, odd ones flip the source across the far plane.
[[nodiscard]] double imageCoord(int n, double length, double s) {
  if (n % 2 == 0) {
    return static_cast<double>(n) * length + s;
  }
  return static_cast<double>(n + 1) * length - s;
}

// Reflection counts on the +/- faces of one axis. Positive lattice indices
// leave the room through the + face first; the parity of the index decides the
// remaining alternation (verified against unfolded-plane crossings).
void axisBounceCounts(int n, int& plus, int& minus) {
  const int a = n < 0 ? -n : n;
  if (n >= 0) {
    plus = (a + 1) / 2;
    minus = a / 2;
  } else {
    plus = a / 2;
    minus = (a + 1) / 2;
  }
}

// Face indices in rectFaces() order: floor, ceiling, x+, x-, z+, z-.
constexpr std::size_t kFloor = 0;
constexpr std::size_t kCeiling = 1;
constexpr std::size_t kXPlus = 2;
constexpr std::size_t kXMinus = 3;
constexpr std::size_t kZPlus = 4;
constexpr std::size_t kZMinus = 5;

class ImageSourceEngine : public IEngine {
 public:
  [[nodiscard]] EngineDescriptor descriptor() const override {
    EngineDescriptor desc;
    desc.id = "image-source";
    desc.name = "Image Source Method";
    desc.version = "1.0.0";
    desc.caps.supportsRect = true;
    desc.caps.supportsMesh = false;
    desc.caps.supportsRayPaths = false;
    desc.caps.supportsBands = true;
    desc.caps.maxOrder = 0;
    desc.paramsSchema = nlohmann::json::object();
    desc.artifacts = {"metrics", "ir"};
    return desc;
  }

  [[nodiscard]] ValidateResult validate(const Scenario& scenario) const override {
    ValidateResult result = rtcore::validateRectScenario(scenario, "image-source");
    if (scenario.room.type != Room::Type::Rect) {
      return result;
    }
    const RectRoom& room = scenario.room.rect;
    if (scenario.sources.empty()) {
      result.errors.emplace_back("image-source requires at least one source");
    }
    if (scenario.receivers.empty()) {
      result.errors.emplace_back("image-source requires at least one receiver");
    }
    for (const Source& source : scenario.sources) {
      if (!insideRoom(source.position, room)) {
        result.errors.push_back("source '" + source.id + "' is outside the room");
      }
    }
    for (const Receiver& receiver : scenario.receivers) {
      if (!insideRoom(receiver.position, room)) {
        result.errors.push_back("receiver '" + receiver.id +
                                "' is outside the room");
      }
    }
    if (scenario.params.signedIr) {
      result.warnings.emplace_back(
          "signedIr is not produced in P0; output is an unsigned energy envelope");
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

    const RectRoom& room = scenario.room.rect;
    const double speedOfSound = params.speedOfSound;
    const int order = params.maxReflectionOrder > 0 ? params.maxReflectionOrder : 0;
    const int sampleRate = params.sampleRate;
    const std::size_t sampleCount = static_cast<std::size_t>(
        std::llround(params.durationSec * static_cast<double>(sampleRate)));
    if (sampleRate <= 0 || sampleCount == 0 || speedOfSound <= 0.0) {
      return result;
    }

    const std::size_t pairCount =
        scenario.sources.size() * scenario.receivers.size();

    IrBuffer ir;
    ir.sampleRate = static_cast<double>(sampleRate);
    ir.durationSec = params.durationSec;
    ir.channels = pairCount;
    ir.bandsHz.clear();  // broadband
    ir.samples.assign(pairCount * sampleCount, 0.0F);
    ir.channelNames.reserve(pairCount);

    // Broadband bounce factors: energy reflection (1 - mean alpha) per face.
    std::array<double, 6> faceEnergyReflect{};
    const std::vector<RectFace> faces = rectFaces(room);
    for (std::size_t f = 0; f < faces.size(); ++f) {
      double alpha = 0.0;
      if (const Material* material =
              findMaterial(scenario, faces[f].materialId)) {
        alpha = meanAbsorption(*material);
      }
      faceEnergyReflect[f] = 1.0 - alpha;
    }

    if (progress) {
      progress(Progress{0.0, "image-source"});
    }

    std::size_t pairIndex = 0;
    for (const Source& source : scenario.sources) {
      for (const Receiver& receiver : scenario.receivers) {
        if (cancel.cancelled()) {
          return Result{};
        }
        ir.channelNames.push_back(source.id + ":" + receiver.id);
        float* channel = ir.samples.data() + pairIndex * sampleCount;

        for (int i = -order; i <= order; ++i) {
          for (int j = -order; j <= order; ++j) {
            for (int k = -order; k <= order; ++k) {
              if (cancel.cancelled()) {
                return Result{};
              }
              const int bounces =
                  (i < 0 ? -i : i) + (j < 0 ? -j : j) + (k < 0 ? -k : k);
              if (bounces > order) {
                continue;
              }

              const Vec3 image{
                  imageCoord(i, room.w, source.position.x),
                  imageCoord(j, room.h, source.position.y),
                  imageCoord(k, room.d, source.position.z)};
              const Vec3 delta = image - receiver.position;
              const double r = std::sqrt(delta.x * delta.x + delta.y * delta.y +
                                         delta.z * delta.z);
              if (r <= 0.0) {
                continue;
              }

              const double t = r / speedOfSound;
              const long long index =
                  static_cast<long long>(std::llround(t * static_cast<double>(sampleRate)));
              if (index < 0 ||
                  static_cast<std::size_t>(index) >= sampleCount) {
                continue;
              }

              int nXPlus = 0;
              int nXMinus = 0;
              int nYPlus = 0;
              int nYMinus = 0;
              int nZPlus = 0;
              int nZMinus = 0;
              axisBounceCounts(i, nXPlus, nXMinus);
              axisBounceCounts(j, nYPlus, nYMinus);
              axisBounceCounts(k, nZPlus, nZMinus);

              // amplitude = (1/r) * sqrt(prod(1-alpha)); energy = amplitude^2.
              double bounceEnergy = 1.0;
              bounceEnergy *= std::pow(faceEnergyReflect[kXPlus],
                                       static_cast<double>(nXPlus));
              bounceEnergy *= std::pow(faceEnergyReflect[kXMinus],
                                       static_cast<double>(nXMinus));
              bounceEnergy *= std::pow(faceEnergyReflect[kCeiling],
                                       static_cast<double>(nYPlus));
              bounceEnergy *= std::pow(faceEnergyReflect[kFloor],
                                       static_cast<double>(nYMinus));
              bounceEnergy *= std::pow(faceEnergyReflect[kZPlus],
                                       static_cast<double>(nZPlus));
              bounceEnergy *= std::pow(faceEnergyReflect[kZMinus],
                                       static_cast<double>(nZMinus));

              const double amplitude = std::sqrt(bounceEnergy) / r;
              const double energy = amplitude * amplitude;
              channel[index] += static_cast<float>(energy);
            }
          }
        }

        ++pairIndex;
        if (progress) {
          progress(Progress{static_cast<double>(pairIndex) /
                                static_cast<double>(pairCount),
                            "image-source"});
        }
      }
    }

    Metrics metrics;
    // IR-derived metrics are broadband (channel 0); analytic Sabine/Eyring
    // are per band from area-weighted mean absorption.
    rtcore::addIrDerivedMetrics(ir, metrics);
    const std::map<double, Rt60> analytic =
        analyticRt60(room, scenario, params.bandsHz, speedOfSound);
    for (const auto& [band, rt] : analytic) {
      metrics.rt60[band].sabine = rt.sabine;
      metrics.rt60[band].eyring = rt.eyring;
    }

    result.metrics = std::move(metrics);
    result.ir = std::move(ir);
    if (progress) {
      progress(Progress{1.0, "done"});
    }
    return result;
  }

 private:
  [[nodiscard]] static bool insideRoom(const Vec3& position,
                                       const RectRoom& room) {
    return position.x >= 0.0 && position.x <= room.w && position.y >= 0.0 &&
           position.y <= room.h && position.z >= 0.0 && position.z <= room.d;
  }
};

REGISTER_ENGINE(ImageSourceEngine);

}  // namespace

void forceLinkImageSourceEngine() {}

}  // namespace av
