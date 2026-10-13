#include "editor_scene_store.hpp"

#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <utility>

namespace av::api {
namespace {

// uuid-lite, same scheme as JobManager ids: process-unique random prefix +
// monotonic counter; safe to embed in REST paths and stable within a process.
[[nodiscard]] std::string makeSceneId() {
  static const std::uint32_t prefix = [] {
    std::random_device rd;
    return rd();
  }();
  static std::atomic<std::uint32_t> counter{0};
  const std::uint32_t n = counter.fetch_add(1, std::memory_order_relaxed);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "scene-%08x-%04x", prefix,
                static_cast<unsigned>(n & 0xffffu));
  return std::string{buf};
}

}  // namespace

std::string makeMeshJobId() {
  static const std::uint32_t prefix = [] {
    std::random_device rd;
    return rd();
  }();
  static std::atomic<std::uint32_t> counter{0};
  const std::uint32_t n = counter.fetch_add(1, std::memory_order_relaxed);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "meshjob-%08x-%04x", prefix,
                static_cast<unsigned>(n & 0xffffu));
  return std::string{buf};
}

EditorSceneStore::~EditorSceneStore() {
  std::unique_lock lock(m_waitMutex);
  m_waitCv.wait(lock, [this] { return m_inflight == 0; });
}

EditorSceneRecord EditorSceneStore::create(Scene scene) {
  EditorSceneRecord record;
  record.sceneId = makeSceneId();
  record.sceneVersion = 1;
  record.scene = std::move(scene);

  std::lock_guard lock(m_mutex);
  const std::string sceneId = record.sceneId;
  m_scenes.emplace(sceneId, record);
  return record;
}

std::optional<EditorSceneRecord> EditorSceneStore::get(
    const std::string& sceneId) const {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<std::uint64_t> EditorSceneStore::replace(
    const std::string& sceneId, Scene scene) {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return std::nullopt;
  }
  EditorSceneRecord& record = it->second;
  record.scene = std::move(scene);
  record.mesh.reset();
  record.report.reset();
  record.meshJob.reset();
  ++record.sceneVersion;
  return record.sceneVersion;
}

bool EditorSceneStore::beginMeshJob(const std::string& sceneId,
                                    const std::string& jobId) {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return false;
  }
  it->second.meshJob = MeshJobSnapshot{jobId, JobState::Running, {}};
  return true;
}

bool EditorSceneStore::finishMeshJob(const std::string& sceneId,
                                     const std::string& jobId, Mesh mesh,
                                     MeshReport report) {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return false;
  }
  EditorSceneRecord& record = it->second;
  if (!record.meshJob.has_value() || record.meshJob->jobId != jobId) {
    return false;  // superseded by a newer generation
  }
  record.mesh = std::move(mesh);
  record.report = std::move(report);
  record.meshJob->state = JobState::Succeeded;
  record.meshJob->error.clear();
  return true;
}

bool EditorSceneStore::failMeshJob(const std::string& sceneId,
                                   const std::string& jobId,
                                   std::string error) {
  std::lock_guard lock(m_mutex);
  const auto it = m_scenes.find(sceneId);
  if (it == m_scenes.end()) {
    return false;
  }
  EditorSceneRecord& record = it->second;
  if (!record.meshJob.has_value() || record.meshJob->jobId != jobId) {
    return false;
  }
  record.meshJob->state = JobState::Failed;
  record.meshJob->error = std::move(error);
  return true;
}

void EditorSceneStore::runDetached(std::function<void()> work) {
  {
    std::lock_guard lock(m_waitMutex);
    ++m_inflight;
  }
  std::thread([this, work = std::move(work)] {
    work();
    std::lock_guard lock(m_waitMutex);
    --m_inflight;
    m_waitCv.notify_all();
  }).detach();
}

}  // namespace av::api
