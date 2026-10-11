#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include <av/acoustics/engine.hpp>
#include <av/sim/job.hpp>

namespace crow {
namespace websocket {
class connection;
}
}  // namespace crow

namespace av::api {

[[nodiscard]] std::string jobStateName(JobState state);

// Shape of schemas/engine-descriptor.schema.json (id, name, version, caps,
// paramsSchema, artifacts) — the /v1/engines contract for the GUI.
[[nodiscard]] nlohmann::json descriptorToJson(const EngineDescriptor& desc);

// {id, state, progress: {fraction, stage}, error}.
[[nodiscard]] nlohmann::json jobStatusToJson(const JobStatus& status);

// RFC7807-ish problem body: {type, title, status, detail}.
[[nodiscard]] nlohmann::json problemJson(int status, std::string title,
                                         std::string detail,
                                         std::string type = "about:blank");

// Subscriber registry for /v1/events. Thread-safe: publish() runs on the job
// watcher thread while subscribe/onconnect run on Crow's io threads.
class EventHub {
 public:
  using Connection = crow::websocket::connection;

  void add(Connection& conn);
  void remove(Connection& conn);
  // jobId may be "*". Unknown ops / empty jobId are ignored.
  void subscribe(Connection& conn, const std::string& jobId);
  // Scene interest registry (P3c fan-out). Empty sceneId is ignored.
  void subscribeScene(Connection& conn, const std::string& sceneId);

  // Serializes `{event, jobId, ...fields}` and sends to matching subscribers.
  void publish(const std::string& event, const std::string& jobId,
               const nlohmann::json& fields);

  // Serializes `{event, sceneId, ...fields}` and sends to every connection
  // that subscribeScene'd that sceneId. `exclude` skips the patch requester,
  // which already got the direct ack on its own connection.
  void publishScene(const std::string& event, const std::string& sceneId,
                    const nlohmann::json& fields,
                    const Connection* exclude = nullptr);

 private:
  std::mutex m_mutex;
  std::map<Connection*, std::set<std::string>> m_subs;
  std::map<Connection*, std::set<std::string>> m_scene_subs;
};

}  // namespace av::api
