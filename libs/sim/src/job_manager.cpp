#include <av/sim/job_manager.hpp>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <random>
#include <utility>

namespace av {
namespace {

using Clock = std::chrono::system_clock;

// uuid-lite: process-unique random prefix + monotonic counter; safe to embed
// in REST paths and stable within a process.
[[nodiscard]] JobId makeJobId() {
  static const std::uint32_t prefix = [] {
    std::random_device rd;
    return rd();
  }();
  static std::atomic<std::uint32_t> counter{0};
  const std::uint32_t n = counter.fetch_add(1, std::memory_order_relaxed);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "job-%08x-%04x", prefix,
                static_cast<unsigned>(n & 0xffffu));
  return JobId{buf};
}

[[nodiscard]] std::string joinErrors(const std::vector<std::string>& errors) {
  std::string out;
  for (const std::string& e : errors) {
    if (!out.empty()) {
      out += "; ";
    }
    out += e;
  }
  return out;
}

struct BodyOutcome {
  JobState state = JobState::Succeeded;
  std::string error;
  std::optional<Result> result;
};

// Engine create + validate + run; never throws. Cancellation wins over a
// late exception/result (spec: state becomes Cancelled once run ends).
[[nodiscard]] BodyOutcome runBody(const Scenario& scenario,
                                  const CancelToken& cancel,
                                  ProgressFn progress) {
  BodyOutcome out;
  try {
    auto engine = EngineRegistry::instance().create(scenario.params.engine);
    if (engine == nullptr) {
      out.state = JobState::Failed;
      out.error = "unknown engine: " + scenario.params.engine;
      return out;
    }

    const ValidateResult validation = engine->validate(scenario);
    if (!validation.ok) {
      out.state = JobState::Failed;
      out.error = joinErrors(validation.errors);
      if (out.error.empty()) {
        out.error = "validation failed";
      }
      return out;
    }

    Result result = engine->run(scenario, scenario.params, cancel,
                                std::move(progress));

    if (cancel.cancelled()) {
      out.state = JobState::Cancelled;
      return out;
    }
    out.result = std::move(result);
    return out;
  } catch (const std::exception& ex) {
    if (cancel.cancelled()) {
      out.state = JobState::Cancelled;
      return out;
    }
    out.state = JobState::Failed;
    out.error = ex.what();
    return out;
  } catch (...) {
    if (cancel.cancelled()) {
      out.state = JobState::Cancelled;
      return out;
    }
    out.state = JobState::Failed;
    out.error = "unknown error";
    return out;
  }
}

}  // namespace

JobStatus JobManager::toStatus(const Job& job) {
  JobStatus st;
  st.id = job.id;
  st.state = job.state;
  st.progress = job.progress;
  st.error = job.error;
  st.result = job.result;
  st.submittedAt = job.submittedAt;
  st.startedAt = job.startedAt;
  st.finishedAt = job.finishedAt;
  return st;
}

JobManager::JobManager(std::size_t threadCount) {
  if (threadCount == 0) {
    const unsigned hw = std::thread::hardware_concurrency();
    threadCount = hw > 1 ? hw - 1 : 1;
  }
  m_workers.reserve(threadCount);
  for (std::size_t i = 0; i < threadCount; ++i) {
    m_workers.emplace_back([this] { workerLoop(); });
  }
}

JobManager::~JobManager() {
  {
    std::lock_guard lock(m_mutex);
    m_stop = true;
    for (auto& [id, job] : m_jobs) {
      (void)id;
      if (job->state == JobState::Queued) {
        job->state = JobState::Cancelled;
        job->finishedAt = Clock::now();
      } else if (job->state == JobState::Running) {
        // Best-effort: token-polling engines exit early; join still waits
        // for whatever is already inside engine::run.
        job->token->cancel();
      }
    }
    m_queue.clear();
  }
  m_ready.notify_all();
  for (std::thread& worker : m_workers) {
    worker.join();
  }
}

JobId JobManager::submit(Scenario scenario) {
  auto job = std::make_shared<Job>();
  job->id = makeJobId();
  job->scenario = std::move(scenario);  // snapshot: no shared mutable state
  job->token = std::make_shared<CancelToken>();
  job->submittedAt = Clock::now();

  {
    std::lock_guard lock(m_mutex);
    m_jobs.emplace(job->id, job);
    m_queue.push_back(job);
  }
  m_ready.notify_one();
  return job->id;
}

std::optional<JobStatus> JobManager::status(const JobId& id) const {
  std::lock_guard lock(m_mutex);
  const auto it = m_jobs.find(id);
  if (it == m_jobs.end()) {
    return std::nullopt;
  }
  return toStatus(*it->second);
}

bool JobManager::cancel(const JobId& id) {
  bool queuedCancelled = false;
  {
    std::lock_guard lock(m_mutex);
    const auto it = m_jobs.find(id);
    if (it == m_jobs.end()) {
      return false;
    }
    Job& job = *it->second;
    if (isTerminal(job.state)) {
      return false;
    }
    if (job.state == JobState::Queued) {
      job.state = JobState::Cancelled;
      job.finishedAt = Clock::now();
      queuedCancelled = true;
    } else {
      // Running: engine observes the token; execute() flips the state once
      // run returns or throws.
      job.token->cancel();
    }
  }
  if (queuedCancelled) {
    m_done.notify_all();
  }
  return true;
}

std::vector<JobStatus> JobManager::list() const {
  std::lock_guard lock(m_mutex);
  std::vector<JobStatus> out;
  out.reserve(m_jobs.size());
  for (const auto& [id, job] : m_jobs) {
    (void)id;
    out.push_back(toStatus(*job));
  }
  std::sort(out.begin(), out.end(),
            [](const JobStatus& a, const JobStatus& b) {
              if (a.submittedAt != b.submittedAt) {
                return a.submittedAt < b.submittedAt;
              }
              return a.id < b.id;
            });
  return out;
}

std::optional<JobStatus> JobManager::wait(
    const JobId& id, std::optional<std::chrono::milliseconds> timeout) const {
  std::unique_lock lock(m_mutex);
  const auto done = [this, &id] {
    const auto it = m_jobs.find(id);
    return it == m_jobs.end() || isTerminal(it->second->state);
  };
  if (timeout.has_value()) {
    if (!m_done.wait_for(lock, *timeout, done)) {
      return std::nullopt;
    }
  } else {
    m_done.wait(lock, done);
  }
  const auto it = m_jobs.find(id);
  if (it == m_jobs.end()) {
    return std::nullopt;
  }
  return toStatus(*it->second);
}

void JobManager::workerLoop() {
  for (;;) {
    std::shared_ptr<Job> job;
    {
      std::unique_lock lock(m_mutex);
      m_ready.wait(lock, [this] { return m_stop || !m_queue.empty(); });
      while (!m_queue.empty()) {
        job = m_queue.front();
        m_queue.pop_front();
        if (job->state == JobState::Queued) {
          break;
        }
        job.reset();  // cancelled while queued — never run
      }
      if (job == nullptr) {
        if (m_stop) {
          return;
        }
        continue;
      }
      if (m_stop) {
        job->state = JobState::Cancelled;
        job->finishedAt = Clock::now();
        lock.unlock();
        m_done.notify_all();
        continue;
      }
      job->state = JobState::Running;
      job->startedAt = Clock::now();
    }

    execute(job);
    m_done.notify_all();
  }
}

void JobManager::execute(const std::shared_ptr<Job>& job) {
  BodyOutcome outcome = runBody(
      job->scenario, *job->token, [this, job](const Progress& p) {
        std::lock_guard lock(m_mutex);
        job->progress = p;  // last write wins
      });

  std::lock_guard lock(m_mutex);
  job->state = outcome.state;
  job->error = std::move(outcome.error);
  job->result = std::move(outcome.result);
  job->finishedAt = Clock::now();
}

JobStatus runNow(Scenario scenario) {
  JobStatus st;
  st.id = makeJobId();
  st.state = JobState::Running;
  st.submittedAt = Clock::now();
  st.startedAt = st.submittedAt;

  CancelToken token;
  BodyOutcome outcome = runBody(scenario, token, [&st](const Progress& p) {
    st.progress = p;  // caller-thread helper: no concurrent writers
  });

  st.state = outcome.state;
  st.error = std::move(outcome.error);
  st.result = std::move(outcome.result);
  st.finishedAt = Clock::now();
  return st;
}

}  // namespace av
