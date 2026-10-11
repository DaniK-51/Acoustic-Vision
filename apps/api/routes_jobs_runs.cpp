#include "api_routes.hpp"

#include <av/io/io.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "job_watcher.hpp"

namespace av::api {

void registerRunRoutes(crow::SimpleApp& app, JobManager& jobs,
                       RunsStore& runs) {
  CROW_ROUTE(app, "/v1/runs").methods(crow::HTTPMethod::Get)(
      [&jobs, &runs](const crow::request& req) {
        reconcileRuns(jobs, runs);
        std::optional<std::string> engineFilter;
        if (const char* engine = req.url_params.get("engine");
            engine != nullptr && *engine != '\0') {
          engineFilter = std::string(engine);
        }
        nlohmann::json body = nlohmann::json::array();
        for (const RunRecord& run : runs.list(engineFilter)) {
          body.push_back(runSummaryJson(run));
        }
        return jsonRes(200, body);
      });

  CROW_ROUTE(app, "/v1/runs/<str>").methods(crow::HTTPMethod::Get)(
      [&jobs, &runs](const std::string& id) {
        reconcileRuns(jobs, runs);
        const std::optional<RunRecord> run = runs.get(id);
        if (!run.has_value()) {
          return problemRes(404, "run not found", id);
        }
        return jsonRes(200, runToJson(*run));
      });
}

void registerJobRoutes(crow::SimpleApp& app, JobManager& jobs) {
  CROW_ROUTE(app, "/v1/jobs/<str>").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        return jsonRes(200, jobStatusToJson(*status));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/cancel").methods(crow::HTTPMethod::Post)(
      [&jobs](const std::string& id) {
        const std::optional<JobStatus> before = jobs.status(id);
        if (!before.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (!jobs.cancel(id)) {
          return problemRes(409, "job already finished",
                            "state=" + jobStateName(before->state));
        }
        const std::optional<JobStatus> after = jobs.status(id);
        return jsonRes(200, jobStatusToJson(
                                after.has_value() ? *after : *before));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/result").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (status->state != JobState::Succeeded ||
            !status->result.has_value()) {
          std::string detail = "state=" + jobStateName(status->state);
          if (!status->error.empty()) {
            detail += "; " + status->error;
          }
          return problemRes(409, "result not available", detail);
        }
        return jsonRes(200, resultToJson(*status->result));
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/ir.wav").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (!status->result.has_value() || !status->result->ir.has_value()) {
          return problemRes(404, "ir not available",
                            "job " + id + " has no ir artifact");
        }
        const std::vector<std::uint8_t> bytes =
            encodeWav(*status->result->ir);
        crow::response res(200);
        res.set_header("Content-Type", "audio/wav");
        res.body.assign(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size());
        return res;
      });

  CROW_ROUTE(app, "/v1/jobs/<str>/field").methods(crow::HTTPMethod::Get)(
      [&jobs](const std::string& id) {
        const std::optional<JobStatus> status = jobs.status(id);
        if (!status.has_value()) {
          return problemRes(404, "job not found", id);
        }
        if (!status->result.has_value() ||
            !status->result->field.has_value()) {
          return problemRes(404, "field not available",
                            "job " + id + " has no field artifact");
        }
        return jsonRes(200, fieldToJson(*status->result->field));
      });
}

}  // namespace av::api
