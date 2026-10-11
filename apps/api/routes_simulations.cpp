#include "api_routes.hpp"

#include <av/acoustics/engine.hpp>
#include <av/io/io.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace av::api {
namespace {

// SimParams fields a {"sceneId"} submit may override without sending the
// whole scenario. Top-level "engine"/"methodParams" are shorthands for the
// same keys inside "params".
bool isParamsOverlayKey(const std::string& key) {
  static const std::set<std::string> kKeys = {
      "engine",        "methodParams", "maxReflectionOrder", "sampleRate",
      "durationSec",   "bandsHz",      "speedOfSound",       "capturePaths",
      "airAbsorption", "signedIr",     "fieldGrid"};
  return kKeys.count(key) != 0;
}

}  // namespace

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

        // Scene-based submit: the stored snapshot (with its sceneVersion) is
        // the run's scenario; {"sceneId", engine?, methodParams?, params?}
        // overlays SimParams onto that snapshot before validate/submit. A
        // full inline scenario (no sceneId) is unchanged.
        std::optional<std::uint64_t> sceneVersion;
        if (body.contains("sceneId")) {
          if (!body.at("sceneId").is_string()) {
            return problemRes(400, "invalid submit body",
                              "sceneId must be a string");
          }
          for (auto it = body.begin(); it != body.end(); ++it) {
            const std::string& key = it.key();
            if (key == "sceneId" || key == "engine" || key == "methodParams" ||
                key == "params") {
              continue;
            }
            return problemRes(
                400, "invalid submit body",
                "key '" + key +
                    "' is not accepted with sceneId; use the params overlay "
                    "or submit a full scenario without sceneId");
          }
          const std::string sceneId = body.at("sceneId").get<std::string>();
          const std::optional<SceneSnapshot> snapshot =
              scenes.get(sceneId);
          if (!snapshot.has_value()) {
            return problemRes(404, "scene not found", sceneId);
          }
          sceneVersion = snapshot->sceneVersion;

          nlohmann::json overlay = nlohmann::json::object();
          if (body.contains("engine")) {
            overlay["engine"] = body.at("engine");
          }
          if (body.contains("methodParams")) {
            overlay["methodParams"] = body.at("methodParams");
          }
          if (body.contains("params")) {
            if (!body.at("params").is_object()) {
              return problemRes(400, "invalid submit body",
                                "params must be an object");
            }
            for (auto it = body.at("params").begin();
                 it != body.at("params").end(); ++it) {
              overlay[it.key()] = it.value();
            }
          }
          for (auto it = overlay.begin(); it != overlay.end(); ++it) {
            const std::string& key = it.key();
            if (!isParamsOverlayKey(key)) {
              return problemRes(400, "invalid submit body",
                                "unknown params override '" + key + "'");
            }
            if ((key == "methodParams" || key == "fieldGrid") &&
                !it.value().is_object()) {
              return problemRes(400, "invalid submit body",
                                key + " must be an object");
            }
          }
          body = scenarioToJson(snapshot->scenario);
          for (auto it = overlay.begin(); it != overlay.end(); ++it) {
            body.at("params")[it.key()] = it.value();
          }
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
