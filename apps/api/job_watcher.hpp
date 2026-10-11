#pragma once

#include <atomic>
#include <thread>

#include <av/sim/job_manager.hpp>

#include "api_support.hpp"
#include "runs_store.hpp"

namespace av::api {

// Polls JobManager and fans out job.* events to /v1/events subscribers.
// Polling keeps av_sim free of transport concerns; 100 ms matches the
// "ProgressFn throttled to ≤10 Hz → WS broadcasts" contract in ARCHITECTURE.
// Also appends a Run record on terminal so the history matches WS-visible
// events (P2e); GET /v1/runs reconciles again to cover the submit/terminal race.
class JobWatcher {
 public:
  JobWatcher(JobManager& jobs, EventHub& hub, RunsStore& runs);
  ~JobWatcher();

  void start();
  void stop();

 private:
  struct Seen {
    JobState state = JobState::Queued;
    double fraction = -1.0;
    std::string stage;
  };

  void loop();
  void publishTerminal(const JobStatus& status);

  JobManager& m_jobs;
  EventHub& m_hub;
  RunsStore& m_runs;
  std::atomic<bool> m_stop{false};
  std::thread m_thread;
};

// Folds terminal jobs into the runs store (idempotent). Called from the
// /v1/runs handlers so history is complete even if a job finished between
// the watcher's polls or the watcher is not the one that saw the transition.
void reconcileRuns(const JobManager& jobs, RunsStore& runs);

}  // namespace av::api
