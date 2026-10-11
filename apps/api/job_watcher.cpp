#include "job_watcher.hpp"

#include <chrono>
#include <map>
#include <utility>

#include <av/io/io.hpp>

namespace av::api {

namespace {

using namespace std::chrono_literals;

}  // namespace

JobWatcher::JobWatcher(JobManager& jobs, EventHub& hub, RunsStore& runs)
    : m_jobs(jobs), m_hub(hub), m_runs(runs) {}

JobWatcher::~JobWatcher() { stop(); }

void JobWatcher::start() {
  m_thread = std::thread([this] { loop(); });
}

void JobWatcher::stop() {
  m_stop.store(true, std::memory_order_relaxed);
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void JobWatcher::loop() {
  std::map<JobId, Seen> seen;
  while (!m_stop.load(std::memory_order_relaxed)) {
    for (const JobStatus& status : m_jobs.list()) {
      Seen& prev = seen[status.id];
      const bool stateChanged = prev.state != status.state;
      const bool progressChanged =
          prev.fraction != status.progress.fraction ||
          prev.stage != status.progress.stage;

      if (stateChanged && isTerminal(status.state)) {
        // History is append-on-terminal (P2e); GET /v1/runs reconciles
        // again so a fast job is visible even before this poll tick.
        m_runs.noteTerminal(
            status.id, jobStateName(status.state),
            status.finishedAt.value_or(std::chrono::system_clock::now()));
        publishTerminal(status);
      } else if (progressChanged && !isTerminal(status.state)) {
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

void JobWatcher::publishTerminal(const JobStatus& status) {
  switch (status.state) {
    case JobState::Succeeded: {
      nlohmann::json fields;
      // P1 simplification: jobId doubles as the simulation/result handle.
      fields["simulationId"] = status.id;
      fields["summary"] =
          status.result && status.result->metrics
              ? metricsToJson(*status.result->metrics)
              : nlohmann::json::object();
      m_hub.publish("job.succeeded", status.id, fields);
      break;
    }
    case JobState::Failed:
      m_hub.publish("job.failed", status.id, {{"error", status.error}});
      break;
    case JobState::Cancelled:
      m_hub.publish("job.cancelled", status.id, nlohmann::json::object());
      break;
    case JobState::Queued:
    case JobState::Running:
      break;
  }
}

void reconcileRuns(const JobManager& jobs, RunsStore& runs) {
  for (const JobStatus& status : jobs.list()) {
    if (isTerminal(status.state)) {
      runs.noteTerminal(
          status.id, jobStateName(status.state),
          status.finishedAt.value_or(std::chrono::system_clock::now()));
    }
  }
}

}  // namespace av::api
