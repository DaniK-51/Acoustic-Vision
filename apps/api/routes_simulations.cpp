#include "api_routes.hpp"

#include <av/acoustics/engine.hpp>
#include <av/io/io.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace av::api {

void registerSimulationRoutes(crow::SimpleApp& app, JobManager& jobs,
                              RunsStore& runs, SceneStore& scenes) {
  CROW_ROUTE(app, "/v1/simulations").methods(crow::HTTPMethod::Post)(
      [&jobs, &runs, &scenes](const crow::request& req) {
        nlohmann::json body;
        try {
          body = nlohmann::json::parse(req.body);
        } catch (const std::exception& error) {
          return problemRes(400, "invalid JSON body", error.what());
        }

        // Scene-based submit: {"sceneId"} replaces an inline scenario; the
        // stored snapshot (with its sceneVersion) is the run's scenario.
        std::optional<std::uint64_t> sceneVersion;
        if (body.contains("sceneId")) {
          const std::string sceneId = body.at("sceneId").get<std::string>();
          const std::optional<SceneSnapshot> snapshot =
              scenes.get(sceneId);
          if (!snapshot.has_value()) {
            return problemRes(404, "scene not found", sceneId);
          }
          sceneVersion = snapshot->sceneVersion;
          body = scenarioToJson(snapshot->scenario);
        }

        Scenario scenario;
        try {
          scenario = scenarioFromJson(body);
        } catch (const std::exception& error) {
          return problemRes(400, "invalid scenario", error.what());
        }

        // Mesh uris resolve against the process CWD (P2c policy). Fail fast
        // here so the job never starts with a silent alpha=0 shell.
        try {
          fillLoadedMesh(scenario);
        } catch (const std::exception& error) {
          return problemRes(400, "mesh load failed", error.what());
        }

        // Fail fast at the API boundary (ARCHITECTURE: 400 on engine/
        // capability mismatch) instead of only failing the async job later.
        std::unique_ptr<IEngine> engine =
            EngineRegistry::instance().create(scenario.params.engine);
        if (engine == nullptr) {
          return problemRes(400, "unknown engine", scenario.params.engine);
        }
        const ValidateResult validation = engine->validate(scenario);
        if (!validation.ok) {
          return problemRes(400, "scenario validation failed",
                            joinErrors(validation.errors));
        }

        // Snapshot run metadata before the scenario moves into the job.
        const std::string runEngine = scenario.params.engine;
        const std::string runName = scenario.name;
        nlohmann::json runDigest = paramsDigestOf(scenario.params);
        if (sceneVersion.has_value()) {
          runDigest["sceneVersion"] = *sceneVersion;
        }
        const JobId id = jobs.submit(std::move(scenario));
        runs.noteSubmitted(id, runEngine, runName, runDigest,
                           std::chrono::system_clock::now(), sceneVersion);
        if (!wantsSync(req)) {
          nlohmann::json out;
          out["jobId"] = id;
          return jsonRes(202, out);
        }

        const std::optional<JobStatus> status = jobs.wait(id);
        if (!status.has_value() || status->state != JobState::Succeeded ||
            !status->result.has_value()) {
          const std::string detail =
              status.has_value() ? status->error : "job disappeared";
          return problemRes(500, "simulation failed", detail);
        }
        // Sync runs are terminal by the time we answer; fold into history now
        // so GET /v1/runs sees them without waiting for the watcher tick.
        runs.noteTerminal(
            id, jobStateName(status->state),
            status->finishedAt.value_or(std::chrono::system_clock::now()));
        nlohmann::json out = resultToJson(*status->result);
        out["jobId"] = id;
        out["state"] = jobStateName(status->state);
        return jsonRes(200, out);
      });
}

}  // namespace av::api
