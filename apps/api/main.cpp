#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/io/io.hpp>
#include <av/sim/job_manager.hpp>
#include <av/units.hpp>

#include <crow.h>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "api_support.hpp"

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
class JobWatcher {
 public:
  JobWatcher(av::JobManager& jobs, av::api::EventHub& hub)
      : m_jobs(jobs), m_hub(hub) {}

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
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

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
                    av::api::EventHub& hub) {
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

  CROW_ROUTE(app, "/v1/simulations").methods(crow::HTTPMethod::Post)(
      [&jobs](const crow::request& req) {
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

        const av::JobId id = jobs.submit(std::move(scenario));
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
        nlohmann::json out = av::resultToJson(*status->result);
        out["jobId"] = id;
        out["state"] = av::api::jobStateName(status->state);
        return jsonRes(200, out);
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

  CROW_WEBSOCKET_ROUTE(app, "/v1/events")
      .onopen([&hub](crow::websocket::connection& conn) { hub.add(conn); })
      .onmessage([&hub](crow::websocket::connection& conn,
                        const std::string& data, bool is_binary) {
        if (is_binary) {
          return;
        }
        try {
          const nlohmann::json msg = nlohmann::json::parse(data);
          if (msg.value("op", "") == "subscribe") {
            hub.subscribe(conn, msg.value("jobId", ""));
          }
        } catch (const std::exception&) {
          // Malformed client frames are ignored; REST stays authoritative.
        }
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

  av::JobManager jobs;
  av::api::EventHub hub;
  JobWatcher watcher(jobs, hub);

  crow::SimpleApp app;
  registerRoutes(app, jobs, hub);
  watcher.start();

  std::cout << "av-api listening on http://" << bindAddr << ':' << port
            << "/v1\n";
  app.bindaddr(bindAddr).port(port).run();

  watcher.stop();
  return 0;
}
