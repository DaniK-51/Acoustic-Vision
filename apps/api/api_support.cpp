#include "api_support.hpp"

#include <crow/websocket.h>

namespace av::api {

std::string jobStateName(JobState state) {
  switch (state) {
    case JobState::Queued:
      return "queued";
    case JobState::Running:
      return "running";
    case JobState::Succeeded:
      return "succeeded";
    case JobState::Failed:
      return "failed";
    case JobState::Cancelled:
      return "cancelled";
  }
  return "unknown";
}

nlohmann::json descriptorToJson(const EngineDescriptor& desc) {
  nlohmann::json caps;
  caps["supportsRect"] = desc.caps.supportsRect;
  caps["supportsMesh"] = desc.caps.supportsMesh;
  caps["supportsRayPaths"] = desc.caps.supportsRayPaths;
  caps["supportsBands"] = desc.caps.supportsBands;
  caps["maxOrder"] = desc.caps.maxOrder;

  nlohmann::json json;
  json["id"] = desc.id;
  json["name"] = desc.name;
  json["version"] = desc.version;
  json["caps"] = std::move(caps);
  json["paramsSchema"] = desc.paramsSchema.is_null()
                            ? nlohmann::json::object()
                            : desc.paramsSchema;
  json["artifacts"] = desc.artifacts;
  return json;
}

nlohmann::json jobStatusToJson(const JobStatus& status) {
  nlohmann::json progress;
  progress["fraction"] = status.progress.fraction;
  progress["stage"] = status.progress.stage;

  nlohmann::json json;
  json["id"] = status.id;
  json["state"] = jobStateName(status.state);
  json["progress"] = std::move(progress);
  json["error"] = status.error;
  return json;
}

nlohmann::json problemJson(int status, std::string title, std::string detail,
                           std::string type) {
  nlohmann::json json;
  json["type"] = std::move(type);
  json["title"] = std::move(title);
  json["status"] = status;
  json["detail"] = std::move(detail);
  return json;
}

void EventHub::add(Connection& conn) {
  std::lock_guard lock(m_mutex);
  m_subs.emplace(&conn, std::set<std::string>{});
  m_scene_subs.emplace(&conn, std::set<std::string>{});
}

void EventHub::remove(Connection& conn) {
  std::lock_guard lock(m_mutex);
  m_subs.erase(&conn);
  m_scene_subs.erase(&conn);
}

void EventHub::subscribe(Connection& conn, const std::string& jobId) {
  std::lock_guard lock(m_mutex);
  const auto it = m_subs.find(&conn);
  if (it != m_subs.end() && !jobId.empty()) {
    it->second.insert(jobId);
  }
}

void EventHub::subscribeScene(Connection& conn, const std::string& sceneId) {
  std::lock_guard lock(m_mutex);
  const auto it = m_scene_subs.find(&conn);
  if (it != m_scene_subs.end() && !sceneId.empty()) {
    it->second.insert(sceneId);
  }
}

void EventHub::publish(const std::string& event, const std::string& jobId,
                       const nlohmann::json& fields) {
  nlohmann::json payload = fields;
  payload["event"] = event;
  payload["jobId"] = jobId;
  const std::string text = payload.dump();

  std::lock_guard lock(m_mutex);
  for (auto& [conn, jobs] : m_subs) {
    if (jobs.contains("*") || jobs.contains(jobId)) {
      conn->send_text(text);
    }
  }
}

void EventHub::publishScene(const std::string& event, const std::string& sceneId,
                            const nlohmann::json& fields,
                            const Connection* exclude) {
  if (sceneId.empty()) {
    return;
  }
  nlohmann::json payload = fields;
  payload["event"] = event;
  payload["sceneId"] = sceneId;
  const std::string text = payload.dump();

  std::lock_guard lock(m_mutex);
  for (auto& [conn, scenes] : m_scene_subs) {
    if (conn == exclude) {
      continue;
    }
    if (scenes.contains(sceneId)) {
      conn->send_text(text);
    }
  }
}

}  // namespace av::api
