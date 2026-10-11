#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/io/io.hpp>
#include <av/io/source_catalog.hpp>
#include <av/sim/job_manager.hpp>
#include <av/units.hpp>

#include <crow.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "api_support.hpp"
#include "runs_store.hpp"
#include "scene_store.hpp"

#ifndef AV_VERSION
#define AV_VERSION "0.0.0"
#endif

namespace {

using namespace std::chrono_literals;

crow::response jsonRes(int code, const nlohmann::json& body) {
  crow::response res(code);
  res.set_header("Content-Type", "application/json");
  res.body = body.dump();
  return res;
}

// Directory of the running executable, used to find source_catalog.json
// beside the binary when AV_SOURCE_CATALOG is unset.
std::filesystem::path exeDirOf(const char* argv0) {
#if defined(_WIN32)
  char buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    return std::filesystem::path(buf).parent_path();
  }
#else
  std::error_code ec;
  const std::filesystem::path self =
      std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    return self.parent_path();
  }
#endif
  return std::filesystem::absolute(argv0).parent_path();
}

crow::response problemRes(int code, std::string title, std::string detail) {
  return jsonRes(code, av::api::problemJson(code, std::move(title),
                                            std::move(detail)));
}

bool wantsSync(const crow::request& req) {
  const char* sync = req.url_params.get("sync");
  return sync != nullptr && (std::string(sync) == "1" || std::string(sync) == "true");
}

// Polls JobManager and fans out job.* events to /v1/events subscribers.
// Polling keeps av_sim free of transport concerns; 100 ms matches the
// "ProgressFn throttled to ≤10 Hz → WS broadcasts" contract in ARCHITECTURE.
// Also appends a Run record on terminal so the history matches WS-visible
// events (P2e); GET /v1/runs reconciles again to cover the submit/terminal race.
class JobWatcher {
 public:
  JobWatcher(av::JobManager& jobs, av::api::EventHub& hub, av::api::RunsStore& runs)
      : m_jobs(jobs), m_hub(hub), m_runs(runs) {}

  ~JobWatcher() { stop(); }

  void start() {
    m_thread = std::thread([this] { loop(); });
  }

  void stop() {
    m_stop.store(true, std::memory_order_relaxed);
    if (m_thread.joinable()) {
      m_thread.join();
    }
  }

 private:
  struct Seen {
    av::JobState state = av::JobState::Queued;
    double fraction = -1.0;
    std::string stage;
  };

  void loop() {
    std::map<av::JobId, Seen> seen;
    while (!m_stop.load(std::memory_order_relaxed)) {
      for (const av::JobStatus& status : m_jobs.list()) {
        Seen& prev = seen[status.id];
        const bool stateChanged = prev.state != status.state;
        const bool progressChanged =
            prev.fraction != status.progress.fraction ||
            prev.stage != status.progress.stage;

        if (stateChanged && av::isTerminal(status.state)) {
          // History is append-on-terminal (P2e); GET /v1/runs reconciles
          // again so a fast job is visible even before this poll tick.
          m_runs.noteTerminal(
              status.id, av::api::jobStateName(status.state),
              status.finishedAt.value_or(std::chrono::system_clock::now()));
          publishTerminal(status);
        } else if (progressChanged && !av::isTerminal(status.state)) {
          m_hub.publish("job.progress", status.id,
                        {{"fraction", status.progress.fraction},
                         {"stage", status.progress.stage}});
        }
        prev.state = status.state;
        prev.fraction = status.progress.fraction;
        prev.stage = status.progress.stage;
      }
      std::this_thread::sleep_for(100ms);
    }
  }

  void publishTerminal(const av::JobStatus& status) {
    switch (status.state) {
      case av::JobState::Succeeded: {
        nlohmann::json fields;
        // P1 simplification: jobId doubles as the simulation/result handle.
        fields["simulationId"] = status.id;
        fields["summary"] =
            status.result && status.result->metrics
                ? av::metricsToJson(*status.result->metrics)
                : nlohmann::json::object();
        m_hub.publish("job.succeeded", status.id, fields);
        break;
      }
      case av::JobState::Failed:
        m_hub.publish("job.failed", status.id, {{"error", status.error}});
        break;
      case av::JobState::Cancelled:
        m_hub.publish("job.cancelled", status.id, nlohmann::json::object());
        break;
      case av::JobState::Queued:
      case av::JobState::Running:
        break;
    }
  }

  av::JobManager& m_jobs;
  av::api::EventHub& m_hub;
  av::api::RunsStore& m_runs;
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

// Folds terminal jobs into the runs store (idempotent). Called from the
// /v1/runs handlers so history is complete even if a job finished between
// the watcher's polls or the watcher is not the one that saw the transition.
void reconcileRuns(const av::JobManager& jobs, av::api::RunsStore& runs) {
  for (const av::JobStatus& status : jobs.list()) {
    if (av::isTerminal(status.state)) {
      runs.noteTerminal(
          status.id, av::api::jobStateName(status.state),
          status.finishedAt.value_or(std::chrono::system_clock::now()));
    }
  }
}

std::string joinErrors(const std::vector<std::string>& errors) {
  std::string out;
  for (const std::string& error : errors) {
    if (!out.empty()) {
      out += "; ";
    }
    out += error;
  }
  return out;
}

void registerRoutes(crow::SimpleApp& app, av::JobManager& jobs,
                    av::api::EventHub& hub, av::api::RunsStore& runs,
                    av::api::SceneStore& scenes,
                    const av::SourceCatalog& sourceCatalog) {
  CROW_ROUTE(app, "/v1/health").methods(crow::HTTPMethod::Get)([] {
    nlohmann::json engines = nlohmann::json::array();
    for (const av::EngineDescriptor& desc :
         av::EngineRegistry::instance().descriptors()) {
      engines.push_back(desc.id);
    }
    nlohmann::json body;
    body["status"] = "ok";
    body["version"] = AV_VERSION;
    body["engines"] = std::move(engines);
    return jsonRes(200, body);
  });

  CROW_ROUTE(app, "/v1/engines").methods(crow::HTTPMethod::Get)([] {
    nlohmann::json body = nlohmann::json::array();
    for (const av::EngineDescriptor& desc :
         av::EngineRegistry::instance().descriptors()) {
      body.push_back(av::api::descriptorToJson(desc));
    }
    return jsonRes(200, body);
  });

  CROW_ROUTE(app, "/v1/bands").methods(crow::HTTPMethod::Get)([] {
    nlohmann::json body;
    body["bandsHz"] = std::vector<double>(av::kDefaultBandsHz.begin(),
                                          av::kDefaultBandsHz.end());
    return jsonRes(200, body);
  });

  // Read-only built-in device catalog (docs/idea.md decision 6: data as-is,
  // disclaimer required). Always 200 — empty list when no catalog file was
  // found/loaded at startup.
  CROW_ROUTE(app, "/v1/source-catalog")
      .methods(crow::HTTPMethod::Get)([&sourceCatalog] {
        return jsonRes(200, av::sourceCatalogToJson(sourceCatalog));
      });

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
        av::Scenario scenario;
        try {
          scenario = av::scenarioFromJson(body);
        } catch (const std::exception& error) {
          return problemRes(400, "invalid scenario", error.what());
        }
        const av::api::SceneSnapshot snapshot = scenes.create(std::move(scenario));
        nlohmann::json out;
        out["sceneId"] = snapshot.sceneId;
        out["sceneVersion"] = snapshot.sceneVersion;
        return jsonRes(201, out);
      });

  CROW_ROUTE(app, "/v1/scenarios/<str>").methods(crow::HTTPMethod::Get)(
      [&scenes](const std::string& sceneId) {
        const std::optional<av::api::SceneSnapshot> snapshot = scenes.get(sceneId);
        if (!snapshot.has_value()) {
          return problemRes(404, "scene not found", sceneId);
        }
        nlohmann::json out;
        out["sceneId"] = snapshot->sceneId;
        out["sceneVersion"] = snapshot->sceneVersion;
        out["scenario"] = av::scenarioToJson(snapshot->scenario);
        return jsonRes(200, out);
      });

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
          const std::optional<av::api::SceneSnapshot> snapshot =
              scenes.get(sceneId);
          if (!snapshot.has_value()) {
            return problemRes(404, "scene not found", sceneId);
          }
          sceneVersion = snapshot->sceneVersion;
          body = av::scenarioToJson(snapshot->scenario);
        }

        av::Scenario scenario;
        try {
          scenario = av::scenarioFromJson(body);
        } catch (const std::exception& error) {
          return problemRes(400, "invalid scenario", error.what());
        }

        // Mesh uris resolve against the process CWD (P2c policy). Fail fast
        // here so the job never starts with a silent alpha=0 shell.
        try {
          av::fillLoadedMesh(scenario);
        } catch (const std::exception& error) {
          return problemRes(400, "mesh load failed", error.what());
        }

        // Fail fast at the API boundary (ARCHITECTURE: 400 on engine/
        // capability mismatch) instead of only failing the async job later.
        std::unique_ptr<av::IEngine> engine =
            av::EngineRegistry::instance().create(scenario.params.engine);
        if (engine == nullptr) {
          return problemRes(400, "unknown engine", scenario.params.engine);
        }
        const av::ValidateResult validation = engine->validate(scenario);
        if (!validation.ok) {
          return problemRes(400, "scenario validation failed",
                            joinErrors(validation.errors));
        }

        // Snapshot run metadata before the scenario moves into the job.
        const std::string runEngine = scenario.params.engine;
        const std::string runName = scenario.name;
        nlohmann::json runDigest = av::api::paramsDigestOf(scenario.params);
        if (sceneVersion.has_value()) {
          runDigest["sceneVersion"] = *sceneVersion;
        }
        const av::JobId id = jobs.submit(std::move(scenario));
        runs.noteSubmitted(
            id, runEngine, runName, runDigest,
            std::chrono::system_clock::now(),
            sceneVersion.has_value()
                ? std::optional<std::string>{std::to_string(*sceneVersion)}
                : std::nullopt);
        if (!wantsSync(req)) {
          nlohmann::json out;
          out["jobId"] = id;
          return jsonRes(202, out);
        }

        const std::optional<av::JobStatus> status = jobs.wait(id);
        if (!status.has_value() || status->state != av::JobState::Succeeded ||
            !status->result.has_value()) {
          const std::string detail =
              status.has_value() ? status->error : "job disappeared";
          return problemRes(500, "simulation failed", detail);
        }
        // Sync runs are terminal by the time we answer; fold into history now
        // so GET /v1/runs sees them without waiting for the watcher tick.
        runs.noteTerminal(
            id, av::api::jobStateName(status->state),
            status->finishedAt.value_or(std::chrono::system_clock::now()));
        nlohmann::json out = av::resultToJson(*status->result);
        out["jobId"] = id;
        out["state"] = av::api::jobStateName(status->state);
        return jsonRes(200, out);
      });

  CROW_ROUTE(app, "/v1/runs").methods(crow::HTTPMethod::Get)(
      [&jobs, &runs](const crow::request& req) {
        reconcileRuns(jobs, runs);
        std::optional<std::string> engineFilter;
        if (const char* engine = req.url_params.get("engine");
            engine != nullptr && *engine != '\0') {
          engineFilter = std::string(engine);
        }
        nlohmann::json body = nlohmann::json::array();
        for (const av::api::RunRecord& run : runs.list(engineFilter)) {
          body.push_back(av::api::runSummaryJson(run));
        }
        return jsonRes(200, body);
      });

  CROW_ROUTE(app, "/v1/runs/<str>").methods(crow::HTTPMethod::Get)(
      [&jobs, &runs](const std::string& id) {
        reconcileRuns(jobs, runs);
        const std::optional<av::api::RunRecord> run = runs.get(id);
        if (!run.has_value()) {
          return problemRes(404, "run not found", id);
        }
        return jsonRes(200, av::api::runToJson(*run));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<av::JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        return jsonRes(200, av::api::jobStatusToJson(*status));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/cancel").methods(crow::HTTPMethod::Post)(
      [&jobs](const std::string& id) {
        const std::optional<av::JobStatus> before = jobs.status(id);
        if (!before.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (!jobs.cancel(id)) {
          return problemRes(409, "job already finished",
                            "state=" + av::api::jobStateName(before->state));
        }
        const std::optional<av::JobStatus> after = jobs.status(id);
        return jsonRes(200, av::api::jobStatusToJson(
                                after.has_value() ? *after : *before));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/result").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<av::JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (status->state != av::JobState::Succeeded ||
            !status->result.has_value()) {
          std::string detail = "state=" + av::api::jobStateName(status->state);
          if (!status->error.empty()) {
            detail += "; " + status->error;
          }
          return problemRes(409, "result not available", detail);
        }
        return jsonRes(200, av::resultToJson(*status->result));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/ir.wav").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<av::JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (!status->result.has_value() || !status->result->ir.has_value()) {
          return problemRes(404, "ir not available",
                            "job " + id + " has no ir artifact");
        }
        const std::vector<std::uint8_t> bytes =
            av::encodeWav(*status->result->ir);
        crow::response res(200);
        res.set_header("Content-Type", "audio/wav");
        res.body.assign(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size());
        return res;
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/field").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<av::JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (!status->result.has_value() ||
            !status->result->field.has_value()) {
          return problemRes(404, "field not available",
                            "job " + id + " has no field artifact");
        }
        return jsonRes(200, av::fieldToJson(*status->result->field));
      });

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
        if (op != "scene.patch") {
          // Unknown ops are ignored (same contract as malformed frames).
          return;
        }

        // scene.patch: {"op","sceneId","ops":[...]}. Every op must land or the
        // whole patch is rejected (SceneStore::patch); reply on this connection.
        auto reply = [&conn](const nlohmann::json& body) {
          conn.send_text(body.dump());
        };
        const std::string sceneId = msg.value("sceneId", "");
        if (sceneId.empty()) {
          reply({{"event", "scene.error"}, {"detail", "sceneId is required"}});
          return;
        }
        std::vector<av::api::ScenePatchOp> ops;
        if (const std::string error =
                av::api::parsePatchOps(msg.value("ops", nlohmann::json()), ops);
            !error.empty()) {
          reply({{"event", "scene.error"}, {"detail", error}});
          return;
        }
        const av::api::PatchResult result = scenes.patch(sceneId, ops);
        if (!result.ok) {
          reply({{"event", "scene.error"}, {"detail", result.error}});
          return;
        }
        reply({{"event", "scene.patched"},
               {"sceneId", sceneId},
               {"sceneVersion", result.sceneVersion},
               {"applied", result.applied}});
      })
      .onclose([&hub](crow::websocket::connection& conn,
                      const std::string& /*reason*/) {
        hub.remove(conn);
      });
}

}  // namespace

int main(int argc, char** argv) {
  std::string bindAddr = "127.0.0.1";
  int port = 8787;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--bind" && i + 1 < argc) {
      bindAddr = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: av-api [--port N] [--bind ADDR]\n";
      return 0;
    } else {
      std::cerr << "error: unknown argument '" << arg << "'\n";
      return 1;
    }
  }
  if (port <= 0 || port > 65535) {
    std::cerr << "error: invalid port\n";
    return 1;
  }

  av::linkBuiltinEngines();

  // Source catalog is loaded once at startup (read-only); engines/JobManager
  // never see it.
  const av::SourceCatalog sourceCatalog =
      av::loadDefaultSourceCatalog(exeDirOf(argv[0]));

  av::JobManager jobs;
  av::api::EventHub hub;
  av::api::RunsStore runs;
  av::api::SceneStore scenes;
  JobWatcher watcher(jobs, hub, runs);

  crow::SimpleApp app;
  registerRoutes(app, jobs, hub, runs, scenes, sourceCatalog);
  watcher.start();

  std::cout << "av-api listening on http://" << bindAddr << ':' << port
            << "/v1\n";
  app.bindaddr(bindAddr).port(port).run();

  watcher.stop();
  return 0;
}
