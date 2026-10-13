#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <av/geometry/mesh.hpp>
#include <av/io/io.hpp>
#include <av/sim/job.hpp>
#include <av/scene/scene_model.hpp>

namespace av::api {

// Mesh generation bookkeeping for an editor scene (slice R9). `jobId` uses the
// `meshjob-…` namespace on purpose: JobManager job ids (`job-…`) stay the
// simulation artifact handles, and runs history is simulation-only.
struct MeshJobSnapshot {
  std::string jobId;
  JobState state = JobState::Queued;
  std::string error;
};

struct EditorSceneRecord {
  std::string sceneId;
  std::uint64_t sceneVersion = 1;
  Scene scene;
  // Last successful mesh generation (docs/room-editor-mesh.md §3.4). Dropped
  // on every document replace — artifacts belong to the document they were
  // generated from.
  std::optional<Mesh> mesh;
  std::optional<MeshReport> report;
  std::optional<MeshJobSnapshot> meshJob;
};

// In-memory store for room-editor Scene documents (schemas/scene.schema.json).
// Separate from SceneStore, which holds Scenario snapshots for interactive
// scene.patch (different document type, different id space). Thread-safe.
// No persistence (v1).
class EditorSceneStore {
 public:
  EditorSceneStore() = default;
  ~EditorSceneStore();

  EditorSceneStore(const EditorSceneStore&) = delete;
  EditorSceneStore& operator=(const EditorSceneStore&) = delete;

  // Registers the document with a fresh sceneId and sceneVersion = 1.
  [[nodiscard]] EditorSceneRecord create(Scene scene);

  // Copy of the current state; the caller may mutate it freely.
  [[nodiscard]] std::optional<EditorSceneRecord> get(
      const std::string& sceneId) const;

  // Full-document replace (v1 PUT semantics): bumps sceneVersion once and
  // drops any generated mesh/report. New version on success, nullopt for an
  // unknown sceneId.
  [[nodiscard]] std::optional<std::uint64_t> replace(const std::string& sceneId,
                                                     Scene scene);

  // Mesh job lifecycle. begin() marks `jobId` Running (last submit wins);
  // finish()/fail() store the outcome only if `jobId` is still the current
  // job, so a stale worker cannot clobber a newer generation. finish/fail
  // report whether the outcome landed — safe to ignore from worker threads.
  [[nodiscard]] bool beginMeshJob(const std::string& sceneId,
                                  const std::string& jobId);
  bool finishMeshJob(const std::string& sceneId, const std::string& jobId,
                     Mesh mesh, MeshReport report);
  bool failMeshJob(const std::string& sceneId, const std::string& jobId,
                   std::string error);

  // Runs `work` on a detached worker thread; the destructor blocks until
  // in-flight work finishes so workers never outlive the store.
  void runDetached(std::function<void()> work);

 private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, EditorSceneRecord> m_scenes;
  std::mutex m_waitMutex;
  std::condition_variable m_waitCv;
  int m_inflight = 0;
};

// uuid-lite for meshjob- ids (same scheme as JobManager's job ids).
[[nodiscard]] std::string makeMeshJobId();

}  // namespace av::api
