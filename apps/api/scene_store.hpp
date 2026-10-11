#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/geometry/vec3.hpp>
#include <av/scenario/scenario.hpp>

namespace av::api {

// v1 scene.patch ops (ARCHITECTURE "scene store + interactive patches").
// Wire form lives on /v1/events; rotate is intentionally not in v1.
struct ScenePatchOp {
  enum class Kind { Move, SetPower };

  Kind kind = Kind::Move;
  // "source" | "receiver". setPower accepts "source" only.
  std::string entityType;
  std::string id;
  Vec3 position;         // move
  double powerDb = 0.0;  // setPower
};

// Parses the `ops` array of a scene.patch message. Returns a human-readable
// error, or empty string on success. Unknown op kinds are errors (policy).
[[nodiscard]] std::string parsePatchOps(const nlohmann::json& opsJson,
                                        std::vector<ScenePatchOp>& out);

struct SceneSnapshot {
  std::string sceneId;
  std::uint64_t sceneVersion = 1;
  Scenario scenario;
};

struct PatchResult {
  bool ok = false;
  std::string error;
  // Version after a successful patch; 0 when ok == false.
  std::uint64_t sceneVersion = 0;
  std::size_t applied = 0;
};

// In-memory Scene Store — backend is the source of truth for interactive
// edits (docs/idea.md interactive mode). Thread-safe. No persistence (v1).
class SceneStore {
 public:
  // Registers the scenario with a fresh sceneId and sceneVersion = 1.
  [[nodiscard]] SceneSnapshot create(Scenario scenario);

  // Copy of the current state; the caller may mutate it freely.
  [[nodiscard]] std::optional<SceneSnapshot> get(
      const std::string& sceneId) const;

  // All-or-nothing: every op is validated against the stored scene first, so
  // a rejected patch never leaves a half-mutated scene behind a stale version.
  // Success bumps sceneVersion once and reports applied = ops.size().
  [[nodiscard]] PatchResult patch(const std::string& sceneId,
                                  const std::vector<ScenePatchOp>& ops);

 private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, SceneSnapshot> m_scenes;
};

}  // namespace av::api
