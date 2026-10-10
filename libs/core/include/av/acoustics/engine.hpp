#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/metrics/metrics.hpp>
#include <av/scenario/scenario.hpp>

namespace av {

struct Capabilities {
  bool supportsRect = true;
  bool supportsMesh = false;
  bool supportsRayPaths = false;
  bool supportsBands = true;
  int maxOrder = 0;  // 0 = N/A
};

struct EngineDescriptor {
  std::string id;
  std::string name;
  std::string version;
  Capabilities caps;
  nlohmann::json paramsSchema;
  // Subset of {"ir","metrics","field","rays","findings"}; GUI renders only
  // advertised kinds (see docs/engines.md section 4).
  std::vector<std::string> artifacts;
};

struct ValidateResult {
  bool ok = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

struct Progress {
  double fraction = 0.0;
  std::string stage;
};

using ProgressFn = std::function<void(const Progress&)>;

class CancelToken {
 public:
  CancelToken() = default;
  CancelToken(const CancelToken&) = delete;
  CancelToken& operator=(const CancelToken&) = delete;

  void cancel() { m_cancelled.store(true, std::memory_order_relaxed); }
  [[nodiscard]] bool cancelled() const {
    return m_cancelled.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> m_cancelled{false};
};

// Unified engine output; populated kinds follow EngineDescriptor caps.
struct Result {
  std::optional<Metrics> metrics;
  std::optional<IrBuffer> ir;
  std::optional<SpatialField> field;
  std::optional<RayPaths> rays;
  std::optional<Findings> findings;
};

class IEngine {
 public:
  virtual ~IEngine() = default;

  [[nodiscard]] virtual EngineDescriptor descriptor() const = 0;
  [[nodiscard]] virtual ValidateResult validate(const Scenario& scenario) const = 0;
  virtual Result run(const Scenario& scenario, const SimParams& params,
                     const CancelToken& cancel, ProgressFn progress) = 0;
};

class EngineRegistry {
 public:
  using Factory = std::function<std::unique_ptr<IEngine>()>;

  static EngineRegistry& instance();

  // Returns false on duplicate id or empty descriptor id.
  bool add(Factory factory);
  [[nodiscard]] std::unique_ptr<IEngine> create(const std::string& id) const;
  [[nodiscard]] std::vector<EngineDescriptor> descriptors() const;
  [[nodiscard]] bool contains(const std::string& id) const;

 private:
  EngineRegistry() = default;

  std::map<std::string, Factory> m_factories;
  std::map<std::string, EngineDescriptor> m_descriptors;
};

}  // namespace av

#define AV_REGISTER_ENGINE_IMPL(EngineType, N)                              \
  static const bool avRegisteredEngine_##N = [] {                           \
    ::av::EngineRegistry::instance().add(                                   \
        [] { return std::make_unique<EngineType>(); });                     \
    return true;                                                            \
  }()

#define REGISTER_ENGINE(EngineType) AV_REGISTER_ENGINE_IMPL(EngineType, __LINE__)
