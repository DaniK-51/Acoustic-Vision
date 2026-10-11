#include "api_routes.hpp"

#include <av/io/io.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace av::api {

void registerScenarioRoutes(crow::SimpleApp& app, SceneStore& scenes) {
  // Scene Store (interactive mode, docs/idea.md): full scenario lives on the
  // backend; WS scene.patch applies mover deltas against it. In-memory only.
  CROW_ROUTE(app, "/v1/scenarios").methods(crow::HTTPMethod::Post)(
      [&scenes](const crow::request& req) {
        nlohmann::json body;
        try {
          body = nlohmann::json::parse(req.body);
        } catch (const std::exception& error) {
          return problemRes(400, "invalid JSON body", error.what());
        }
        Scenario scenario;
        try {
          scenario = scenarioFromJson(body);
        } catch (const std::exception& error) {
          return problemRes(400, "invalid scenario", error.what());
        }
        const SceneSnapshot snapshot = scenes.create(std::move(scenario));
        nlohmann::json out;
        out["sceneId"] = snapshot.sceneId;
        out["sceneVersion"] = snapshot.sceneVersion;
        return jsonRes(201, out);
      });

  CROW_ROUTE(app, "/v1/scenarios/<str>").methods(crow::HTTPMethod::Get)(
      [&scenes](const std::string& sceneId) {
        const std::optional<SceneSnapshot> snapshot = scenes.get(sceneId);
        if (!snapshot.has_value()) {
          return problemRes(404, "scene not found", sceneId);
        }
        nlohmann::json out;
        out["sceneId"] = snapshot->sceneId;
        out["sceneVersion"] = snapshot->sceneVersion;
        out["scenario"] = scenarioToJson(snapshot->scenario);
        return jsonRes(200, out);
      });
}

}  // namespace av::api
