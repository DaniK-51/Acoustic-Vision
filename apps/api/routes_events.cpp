#include "api_routes.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace av::api {

void registerEventRoutes(crow::SimpleApp& app, EventHub& hub,
                         SceneStore& scenes) {
  CROW_WEBSOCKET_ROUTE(app, "/v1/events")
      .onopen([&hub](crow::websocket::connection& conn) { hub.add(conn); })
      .onmessage([&hub, &scenes](crow::websocket::connection& conn,
                                 const std::string& data, bool is_binary) {
        if (is_binary) {
          return;
        }
        nlohmann::json msg;
        try {
          msg = nlohmann::json::parse(data);
        } catch (const std::exception&) {
          // Malformed client frames are ignored; REST stays authoritative.
          return;
        }
        const std::string op = msg.value("op", "");
        if (op == "subscribe") {
          hub.subscribe(conn, msg.value("jobId", ""));
          return;
        }
        if (op == "subscribeScene") {
          const std::string sceneId = msg.value("sceneId", "");
          if (sceneId.empty()) {
            return;  // same silent drop as job subscribe with empty id
          }
          hub.subscribeScene(conn, sceneId);
          // Ack so a client (or test) knows the registration landed before
          // it starts patching on another connection.
          conn.send_text(nlohmann::json{{"event", "scene.subscribed"},
                                        {"sceneId", sceneId}}
                             .dump());
          return;
        }
        if (op != "scene.patch") {
          // Unknown ops are ignored (same contract as malformed frames).
          return;
        }

        // scene.patch: {"op","sceneId","ops":[...]}. Every op must land or the
        // whole patch is rejected (SceneStore::patch); the requester gets an
        // ack on this connection and subscribeScene watchers get the same
        // outcome fanned out (P3c) minus the requester (no double delivery).
        auto reply = [&conn](const nlohmann::json& body) {
          conn.send_text(body.dump());
        };
        auto fanOut = [&hub, &conn](const std::string& event,
                                    const std::string& sceneId,
                                    const nlohmann::json& fields) {
          hub.publishScene(event, sceneId, fields, &conn);
        };
        const std::string sceneId = msg.value("sceneId", "");
        if (sceneId.empty()) {
          reply({{"event", "scene.error"}, {"detail", "sceneId is required"}});
          return;
        }
        std::vector<ScenePatchOp> ops;
        if (const std::string error =
                parsePatchOps(msg.value("ops", nlohmann::json()), ops);
            !error.empty()) {
          reply({{"event", "scene.error"}, {"detail", error}});
          fanOut("scene.error", sceneId, {{"detail", error}});
          return;
        }
        const PatchResult result = scenes.patch(sceneId, ops);
        if (!result.ok) {
          reply({{"event", "scene.error"}, {"detail", result.error}});
          fanOut("scene.error", sceneId, {{"detail", result.error}});
          return;
        }
        reply({{"event", "scene.patched"},
               {"sceneId", sceneId},
               {"sceneVersion", result.sceneVersion},
               {"applied", result.applied}});
        fanOut("scene.patched", sceneId,
               {{"sceneVersion", result.sceneVersion},
                {"applied", result.applied}});
      })
      .onclose([&hub](crow::websocket::connection& conn,
                      const std::string& /*reason*/) {
        hub.remove(conn);
      });
}

}  // namespace av::api
