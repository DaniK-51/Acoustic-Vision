#include "scene_store.hpp"

#include <atomic>
#include <cstdio>
#include <random>
#include <stdexcept>

namespace av::api {
namespace {

// uuid-lite, same scheme as JobManager ids: process-unique random prefix +
// monotonic counter; safe to embed in REST paths and stable within a process.
[[nodiscard]] std::string makeSceneId() {
  static const std::uint32_t prefix = [] {
    std::random_device rd;
    return rd();
  }();
  static std::atomic<std::uint32_t> counter{0};
  const std::uint32_t n = counter.fetch_add(1, std::memory_order_relaxed);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "scene-%08x-%04x", prefix,
                static_cast<unsigned>(n & 0xffffu));
  return std::string{buf};
}

[[nodiscard]] std::string parsePatchOp(const nlohmann::json& json,
                                       ScenePatchOp& out) {
  if (!json.is_object()) {
    return "op entry must be an object";
  }
  const std::string op = json.value("op", std::string{});
  out.entityType = json.value("entityType", std::string{});
  out.id = json.value("id", std::string{});
  if (out.id.empty()) {
    return "op entry is missing 'id'";
  }

  if (op == "move") {
    out.kind = ScenePatchOp::Kind::Move;
    if (out.entityType != "source" && out.entityType != "receiver") {
      return "move entityType must be 'source' or 'receiver'";
    }
    if (!json.contains("position") || !json.at("position").is_array() ||
        json.at("position").size() != 3 ||
        !json.at("position").at(0).is_number() ||
        !json.at("position").at(1).is_number() ||
        !json.at("position").at(2).is_number()) {
      return "move position must be a [x, y, z] number array";
    }
    out.position = Vec3{json.at("position").at(0).get<double>(),
                        json.at("position").at(1).get<double>(),
                        json.at("position").at(2).get<double>()};
    return {};
  }

  if (op == "setPower") {
    out.kind = ScenePatchOp::Kind::SetPower;
    if (out.entityType != "source") {
      return "setPower entityType must be 'source'";
    }
    if (!json.contains("powerDb") || !json.at("powerDb").is_number()) {
      return "setPower powerDb must be a number";
    }
    out.powerDb = json.at("powerDb").get<double>();
    return {};
  }

  return "unknown op '" + op + "'";
}

}  // namespace

std::string parsePatchOps(const nlohmann::json& opsJson,
                          std::vector<ScenePatchOp>& out) {
  if (!opsJson.is_array()) {
    return "ops must be an array";
  }
  out.clear();
  out.reserve(opsJson.size());
  for (const nlohmann::json& entry : opsJson) {
    ScenePatchOp op;
    if (std::string error = parsePatchOp(entry, op); !error.empty()) {
      out.clear();
      return error;
    }
    out.push_back(std::move(op));
  }
  return {};
}

SceneSnapshot SceneStore::create(Scenario scenario) {
  std::lock_guard lock(m_mutex);
  SceneSnapshot snapshot;
  snapshot.sceneId = makeSceneId();
  snapshot.sceneVersion = 1;
  snapshot.scenario = std::move(scenario);
  m_scenes.emplace(snapshot.sceneId, snapshot);
  return snapshot;
}

std::optional<SceneSnapshot> SceneStore::get(const std::string& sceneId) const {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return std::nullopt;
  }
  return it->second;
}

PatchResult SceneStore::patch(const std::string& sceneId,
                              const std::vector<ScenePatchOp>& ops) {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return PatchResult{false, "unknown sceneId '" + sceneId + "'", 0, 0};
  }

  // Mutate a copy and commit only when every op lands — a failed op must not
  // leave half-applied movers behind an un-bumped version.
  Scenario scenario = it->second.scenario;
  for (const ScenePatchOp& op : ops) {
    switch (op.kind) {
      case ScenePatchOp::Kind::Move: {
        if (op.entityType == "source") {
          bool found = false;
          for (Source& source : scenario.sources) {
            if (source.id == op.id) {
              source.position = op.position;
              found = true;
              break;
            }
          }
          if (!found) {
            return PatchResult{false, "unknown source '" + op.id + "'", 0, 0};
          }
        } else {
          bool found = false;
          for (Receiver& receiver : scenario.receivers) {
            if (receiver.id == op.id) {
              receiver.position = op.position;
              found = true;
              break;
            }
          }
          if (!found) {
            return PatchResult{false, "unknown receiver '" + op.id + "'", 0, 0};
          }
        }
        break;
      }
      case ScenePatchOp::Kind::SetPower: {
        bool found = false;
        for (Source& source : scenario.sources) {
          if (source.id == op.id) {
            source.powerDb = op.powerDb;
            found = true;
            break;
          }
        }
        if (!found) {
          return PatchResult{false, "unknown source '" + op.id + "'", 0, 0};
        }
        break;
      }
    }
  }

  ++it->second.sceneVersion;
  it->second.scenario = std::move(scenario);
  return PatchResult{true, {}, it->second.sceneVersion, ops.size()};
}

}  // namespace av::api
