#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/scenario/scenario.hpp>
#include <av/sim/job.hpp>

namespace av::api {

// Snapshot of a finished job for the runs history (P2e).
// runId == jobId: P1-P2 handle collapse, same id in both fields (ARCHITECTURE
// "runs (history)").
struct RunRecord {
  std::string runId;
  std::string jobId;
  std::string engine;
  std::string scenarioName;
  // Set when the run was submitted from the Scene Store (`{"sceneId"}`).
  std::optional<std::string> sceneVersion;
  // jobStateName of the terminal state: "succeeded"|"failed"|"cancelled".
  std::string state;
  std::chrono::system_clock::time_point submittedAt{};
  std::chrono::system_clock::time_point finishedAt{};
  // engine + key SimParams — what distinguishes one run of a scene from another.
  nlohmann::json paramsDigest;
};

[[nodiscard]] nlohmann::json paramsDigestOf(const SimParams& params);

// Array item for GET /v1/runs: the record without artifact links.
[[nodiscard]] nlohmann::json runSummaryJson(const RunRecord& run);
// GET /v1/runs/{runId}: record + links into the /v1/jobs/{jobId}/... artifacts.
[[nodiscard]] nlohmann::json runToJson(const RunRecord& run);

[[nodiscard]] std::string iso8601(std::chrono::system_clock::time_point tp);

// In-memory append-only runs history. Thread-safe. No disk persistence (v1).
class RunsStore {
 public:
  // Submit-time metadata (JobStatus alone cannot rebuild the record later).
  void noteSubmitted(const JobId& jobId, std::string engine,
                     std::string scenarioName, nlohmann::json paramsDigest,
                     std::chrono::system_clock::time_point submittedAt,
                     std::optional<std::string> sceneVersion = std::nullopt);

  // Appends on first terminal note. False for unknown jobId or a duplicate.
  bool noteTerminal(const JobId& jobId, std::string state,
                    std::chrono::system_clock::time_point finishedAt);

  // Newest first (finishedAt desc). engineFilter matches RunRecord::engine.
  [[nodiscard]] std::vector<RunRecord> list(
      const std::optional<std::string>& engineFilter = std::nullopt) const;

  [[nodiscard]] std::optional<RunRecord> get(const JobId& runId) const;

 private:
  struct Pending {
    std::string engine;
    std::string scenarioName;
    std::optional<std::string> sceneVersion;
    std::chrono::system_clock::time_point submittedAt{};
    nlohmann::json paramsDigest;
  };

  // Lock must be held. Moves the pending entry into history.
  void appendLocked(const JobId& jobId, std::string state,
                    std::chrono::system_clock::time_point finishedAt);

  mutable std::mutex m_mutex;
  std::unordered_map<JobId, Pending> m_pending;
  // Terminal note can beat noteSubmitted (fast jobs); remember it so the
  // record is still appended once the metadata arrives.
  std::unordered_map<JobId, std::pair<std::string, std::chrono::system_clock::time_point>>
      m_earlyTerminal;
  std::vector<RunRecord> m_history;
};

}  // namespace av::api
