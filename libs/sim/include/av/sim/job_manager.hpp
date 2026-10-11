#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include <av/scenario/scenario.hpp>
#include <av/sim/job.hpp>

namespace av {

// In-memory job queue over EngineRegistry. Thread-safe.
// Jobs hold a scenario snapshot (by value) taken at submit.
class JobManager {
 public:
  // threadCount == 0 -> max(1, hardware_concurrency - 1)
  explicit JobManager(std::size_t threadCount = 0);
  ~JobManager();

  JobManager(const JobManager&) = delete;
  JobManager& operator=(const JobManager&) = delete;

  [[nodiscard]] JobId submit(Scenario scenario);

  [[nodiscard]] std::optional<JobStatus> status(const JobId& id) const;

  // True if cancellation was requested: queued -> Cancelled immediately
  // (never runs); running -> token set, state becomes Cancelled once the
  // engine returns or throws. False for unknown or already-terminal jobs.
  bool cancel(const JobId& id);

  [[nodiscard]] std::vector<JobStatus> list() const;

  // Blocks until the job is terminal. nullopt for unknown id or on timeout
  // (infinite wait when timeout is omitted).
  [[nodiscard]] std::optional<JobStatus> wait(
      const JobId& id,
      std::optional<std::chrono::milliseconds> timeout = std::nullopt) const;

 private:
  struct Job {
    JobId id;
    Scenario scenario;
    std::shared_ptr<CancelToken> token;
    JobState state = JobState::Queued;
    Progress progress;
    std::string error;
    std::optional<Result> result;
    std::chrono::system_clock::time_point submittedAt{};
    std::optional<std::chrono::system_clock::time_point> startedAt;
    std::optional<std::chrono::system_clock::time_point> finishedAt;
  };

  void workerLoop();
  void execute(const std::shared_ptr<Job>& job);
  [[nodiscard]] static JobStatus toStatus(const Job& job);

  mutable std::mutex m_mutex;
  std::condition_variable m_ready;
  mutable std::condition_variable m_done;
  std::deque<std::shared_ptr<Job>> m_queue;
  std::unordered_map<JobId, std::shared_ptr<Job>> m_jobs;
  std::vector<std::thread> m_workers;
  bool m_stop = false;
};

// Synchronous run on the caller thread (CLI/tests): no queue, no pool.
[[nodiscard]] JobStatus runNow(Scenario scenario);

}  // namespace av
