#include "api_routes.hpp"

#include <av/io/io.hpp>
#include <av/scene/scene_mesher.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "editor_scene_store.hpp"

namespace av::api {
namespace {

// Parses + semantically validates a scene document body. On success fills
// `out` and returns an empty problem body; otherwise the 400 response.
std::optional<crow::response> sceneFromBody(const std::string& raw,
                                            Scene& out) {
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(raw);
  } catch (const std::exception& error) {
    return problemRes(400, "invalid JSON body", error.what());
  }
  try {
    out = sceneFromJson(body);
  } catch (const std::exception& error) {
    return problemRes(400, "invalid scene", error.what());
  }
  const std::vector<std::string> errors = validateScene(out);
  if (!errors.empty()) {
    return problemRes(400, "scene validation failed", joinErrors(errors));
  }
  return std::nullopt;
}

// Surface mesh + report for the stored document (slice R8 pipeline).
void generateMesh(EditorSceneStore& scenes, const std::string& sceneId,
                  const std::string& jobId) {
  try {
    const std::optional<EditorSceneRecord> record = scenes.get(sceneId);
    if (!record.has_value()) {
      scenes.failMeshJob(sceneId, jobId, "scene not found");
      return;
    }
    const ResolvedSurfaceMeshSettings resolved =
        resolveSurfaceMeshSettings(record->scene.meshSettings);
    const SurfaceMeshResult result =
        generateSurfaceMesh(record->scene, record->scene.meshSettings);
    scenes.finishMeshJob(sceneId, jobId, result.mesh,
                         meshReportFrom(result, resolved));
  } catch (const std::exception& error) {
    scenes.failMeshJob(sceneId, jobId, error.what());
  } catch (...) {
    scenes.failMeshJob(sceneId, jobId, "unknown mesh generation error");
  }
}

crow::response meshStatusRes(const EditorSceneRecord& record) {
  nlohmann::json out;
  out["sceneId"] = record.sceneId;
  out["sceneVersion"] = record.sceneVersion;
  if (!record.meshJob.has_value()) {
    return problemRes(404, "mesh not generated",
                      "POST /v1/scenes/" + record.sceneId + "/mesh first");
  }
  const MeshJobSnapshot& job = *record.meshJob;
  out["jobId"] = job.jobId;
  out["state"] = jobStateName(job.state);
  if (!job.error.empty()) {
    out["error"] = job.error;
  }
  if (record.report.has_value()) {
    out["report"] = meshReportToJson(*record.report);
  }
  return jsonRes(200, out);
}

}  // namespace

void registerEditorSceneRoutes(crow::SimpleApp& app,
                               EditorSceneStore& scenes) {
  // Room-editor Scene documents (schemas/scene.schema.json) — a separate id
  // space from the Scenario snapshots on /v1/scenarios (docs/room-editor-mesh.md
  // §4). In-memory only.
  CROW_ROUTE(app, "/v1/scenes").methods(crow::HTTPMethod::Post)(
      [&scenes](const crow::request& req) {
        Scene scene;
        if (auto problem = sceneFromBody(req.body, scene)) {
          return std::move(*problem);
        }
        const EditorSceneRecord record = scenes.create(std::move(scene));
        nlohmann::json out;
        out["sceneId"] = record.sceneId;
        out["sceneVersion"] = record.sceneVersion;
        return jsonRes(201, out);
      });

  CROW_ROUTE(app, "/v1/scenes/<str>").methods(crow::HTTPMethod::Get)(
      [&scenes](const std::string& sceneId) {
        const std::optional<EditorSceneRecord> record = scenes.get(sceneId);
        if (!record.has_value()) {
          return problemRes(404, "scene not found", sceneId);
        }
        nlohmann::json out;
        out["sceneId"] = record->sceneId;
        out["sceneVersion"] = record->sceneVersion;
        out["scene"] = sceneToJson(record->scene);
        return jsonRes(200, out);
      });

  // v1 update = full document replace (architect decision: PUT whole scene,
  // bump version). PATCH is the docs table verb for the same operation.
  const auto replaceHandler = [&scenes](const crow::request& req,
                                        const std::string& sceneId) {
    Scene scene;
    if (auto problem = sceneFromBody(req.body, scene)) {
      return std::move(*problem);
    }
    const std::optional<std::uint64_t> version =
        scenes.replace(sceneId, std::move(scene));
    if (!version.has_value()) {
      return problemRes(404, "scene not found", sceneId);
    }
    nlohmann::json out;
    out["sceneId"] = sceneId;
    out["sceneVersion"] = *version;
    return jsonRes(200, out);
  };
  CROW_ROUTE(app, "/v1/scenes/<str>")
      .methods(crow::HTTPMethod::Put, crow::HTTPMethod::Patch)(replaceHandler);

  // Mesh generation as a job: sync=1 answers with the finished report, the
  // default accepts and generates on a worker (long meshing is a later slice,
  // the wire shape is already the job one).
  CROW_ROUTE(app, "/v1/scenes/<str>/mesh").methods(crow::HTTPMethod::Post)(
      [&scenes](const crow::request& req, const std::string& sceneId) {
        const std::optional<EditorSceneRecord> record = scenes.get(sceneId);
        if (!record.has_value()) {
          return problemRes(404, "scene not found", sceneId);
        }
        const std::vector<std::string> errors = validateScene(record->scene);
        if (!errors.empty()) {
          return problemRes(400, "scene validation failed", joinErrors(errors));
        }
        const std::string jobId = makeMeshJobId();
        if (!scenes.beginMeshJob(sceneId, jobId)) {
          return problemRes(404, "scene not found", sceneId);
        }
        if (wantsSync(req)) {
          generateMesh(scenes, sceneId, jobId);
          const std::optional<EditorSceneRecord> after = scenes.get(sceneId);
          return meshStatusRes(after.value_or(*record));
        }
        scenes.runDetached([&scenes, sceneId, jobId] {
          generateMesh(scenes, sceneId, jobId);
        });
        nlohmann::json out;
        out["sceneId"] = sceneId;
        out["jobId"] = jobId;
        out["state"] = jobStateName(JobState::Running);
        return jsonRes(202, out);
      });

  CROW_ROUTE(app, "/v1/scenes/<str>/mesh").methods(crow::HTTPMethod::Get)(
      [&scenes](const std::string& sceneId) {
        const std::optional<EditorSceneRecord> record = scenes.get(sceneId);
        if (!record.has_value()) {
          return problemRes(404, "scene not found", sceneId);
        }
        return meshStatusRes(*record);
      });

  CROW_ROUTE(app, "/v1/scenes/<str>/mesh.obj").methods(crow::HTTPMethod::Get)(
      [&scenes](const std::string& sceneId) {
        const std::optional<EditorSceneRecord> record = scenes.get(sceneId);
        if (!record.has_value()) {
          return problemRes(404, "scene not found", sceneId);
        }
        if (!record->mesh.has_value()) {
          return problemRes(404, "mesh not generated",
                            "POST /v1/scenes/" + sceneId + "/mesh first");
        }
        const std::string obj = encodeObj(*record->mesh);
        crow::response res(200);
        res.set_header("Content-Type", "model/obj");
        res.body = obj;
        return res;
      });

  // Scene -> Scenario compile (to-scenario). The mesh branch writes the
  // surface mesh to a temp OBJ so the returned mesh room is immediately
  // submittable: av-api resolves relative uris against its CWD, so the uri is
  // absolute.
  CROW_ROUTE(app, "/v1/scenes/<str>/to-scenario")
      .methods(crow::HTTPMethod::Post)(
          [&scenes](const std::string& sceneId) {
            const std::optional<EditorSceneRecord> record = scenes.get(sceneId);
            if (!record.has_value()) {
              return problemRes(404, "scene not found", sceneId);
            }
            const std::vector<std::string> errors =
                validateScene(record->scene);
            if (!errors.empty()) {
              return problemRes(400, "scene validation failed",
                                joinErrors(errors));
            }
            const std::filesystem::path meshPath =
                std::filesystem::temp_directory_path() /
                ("av-scene-" + sceneId + ".obj");
            try {
              const Scenario scenario =
                  scenarioFromScene(record->scene, meshPath);
              return jsonRes(200, scenarioToJson(scenario));
            } catch (const std::exception& error) {
              return problemRes(500, "to-scenario failed", error.what());
            }
          });
}

}  // namespace av::api
