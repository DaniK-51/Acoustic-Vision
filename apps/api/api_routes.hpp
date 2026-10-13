#pragma once

#include <crow.h>

#include <nlohmann/json.hpp>

#include <av/io/source_catalog.hpp>
#include <av/sim/job_manager.hpp>

#include <string>
#include <utility>
#include <vector>

#include "api_support.hpp"
#include "editor_scene_store.hpp"
#include "runs_store.hpp"
#include "scene_store.hpp"

#ifndef AV_VERSION
#define AV_VERSION "0.0.0"
#endif

namespace av::api {

// Crow response helpers shared by the route TUs. Kept here (not in
// api_support) so the Crow-free stores stay testable without Crow.
inline crow::response jsonRes(int code, const nlohmann::json& body) {
  crow::response res(code);
  res.set_header("Content-Type", "application/json");
  res.body = body.dump();
  return res;
}

// RFC7807-ish problem body via av::api::problemJson — the one place that
// builds the error wire shape.
inline crow::response problemRes(int code, std::string title,
                                 std::string detail) {
  return jsonRes(code, problemJson(code, std::move(title), std::move(detail)));
}

inline bool wantsSync(const crow::request& req) {
  const char* sync = req.url_params.get("sync");
  return sync != nullptr &&
         (std::string(sync) == "1" || std::string(sync) == "true");
}

inline std::string joinErrors(const std::vector<std::string>& errors) {
  std::string out;
  for (const std::string& error : errors) {
    if (!out.empty()) {
      out += "; ";
    }
    out += error;
  }
  return out;
}

// Route registration, one TU per domain. Paths, JSON shapes and event names
// are frozen by tests/integration/api_smoke.py — move only, no redesign.
void registerHealthRoutes(crow::SimpleApp& app,
                          const SourceCatalog& sourceCatalog);
void registerScenarioRoutes(crow::SimpleApp& app, SceneStore& scenes);
void registerSimulationRoutes(crow::SimpleApp& app, JobManager& jobs,
                              RunsStore& runs, SceneStore& scenes);
void registerRunRoutes(crow::SimpleApp& app, JobManager& jobs, RunsStore& runs);
void registerJobRoutes(crow::SimpleApp& app, JobManager& jobs);
void registerEventRoutes(crow::SimpleApp& app, EventHub& hub, SceneStore& scenes);
// Room-editor Scene documents (/v1/scenes + mesh + to-scenario), slice R9.
void registerEditorSceneRoutes(crow::SimpleApp& app, EditorSceneStore& scenes);

// Single entry for main: registers every /v1 route.
void registerRoutes(crow::SimpleApp& app, JobManager& jobs, EventHub& hub,
                    RunsStore& runs, SceneStore& scenes,
                    EditorSceneStore& editorScenes,
                    const SourceCatalog& sourceCatalog);

}  // namespace av::api
