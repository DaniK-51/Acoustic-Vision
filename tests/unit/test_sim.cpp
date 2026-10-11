#include <gtest/gtest.h>

#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/scenario/scenario.hpp>
#include <av/sim/job.hpp>
#include <av/sim/job_manager.hpp>
#include <av/units.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::mutex g_mutex;
std::condition_variable g_cv;

// Cross-thread observations from the test-only engines.
struct EngineProbe {
  bool blockerReleased = false;
  int dummyRuns = 0;
  std::string lastScenarioName;
  bool sleepSawCancel = false;
  int sleepSteps = 0;
};

EngineProbe g_probe;

void resetProbe() {
  std::lock_guard lock(g_mutex);
  g_probe = EngineProbe{};
}

// Polls until pred() or timeout; keeps tests deterministic without races on
// "cancel before the worker picks the job up".
[[nodiscard]] bool waitUntil(const std::function<bool()>& pred,
                             std::chrono::milliseconds timeout = 2000ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (pred()) {
      return true;
    }
    std::this_thread::sleep_for(2ms);
  }
  return pred();
}

[[nodiscard]] av::EngineDescriptor makeDesc(const std::string& id) {
  av::EngineDescriptor desc;
  desc.id = id;
  desc.name = id;
  desc.version = "0.0.0";
  desc.paramsSchema = nlohmann::json::object();
  return desc;
}

// Instant success; records the scenario it actually ran with.
class TestDummyEngine : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    return makeDesc("test-dummy");
  }

  [[nodiscard]] av::ValidateResult validate(const av::Scenario&) const override {
    return {};
  }

  av::Result run(const av::Scenario& scenario, const av::SimParams&,
                 const av::CancelToken&, av::ProgressFn) override {
    {
      std::lock_guard lock(g_mutex);
      ++g_probe.dummyRuns;
      g_probe.lastScenarioName = scenario.name;
    }
    av::Result result;
    av::Metrics metrics;
    metrics.rt60[500.0] = av::Rt60{0.5, 0.5, 0.5};
    result.metrics = std::move(metrics);
    return result;
  }
};

// Polls the cancel token in small steps; reports progress.
class TestSleepEngine : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    return makeDesc("test-sleep");
  }

  [[nodiscard]] av::ValidateResult validate(const av::Scenario&) const override {
    return {};
  }

  av::Result run(const av::Scenario&, const av::SimParams&,
                 const av::CancelToken& cancel, av::ProgressFn progress) override {
    for (int i = 0; i < 400; ++i) {
      if (cancel.cancelled()) {
        std::lock_guard lock(g_mutex);
        g_probe.sleepSawCancel = true;
        return {};
      }
      if (progress) {
        av::Progress p;
        p.fraction = static_cast<double>(i) / 400.0;
        p.stage = "step";
        progress(p);
      }
      {
        std::lock_guard lock(g_mutex);
        g_probe.sleepSteps = i + 1;
      }
      std::this_thread::sleep_for(5ms);
    }
    return {};
  }
};

// Occupies the single worker until released (or cancelled).
class TestBlockerEngine : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    return makeDesc("test-blocker");
  }

  [[nodiscard]] av::ValidateResult validate(const av::Scenario&) const override {
    return {};
  }

  av::Result run(const av::Scenario&, const av::SimParams&,
                 const av::CancelToken& cancel, av::ProgressFn) override {
    std::unique_lock lock(g_mutex);
    g_cv.wait_for(lock, 5000ms,
                  [&cancel] { return g_probe.blockerReleased || cancel.cancelled(); });
    return {};
  }
};

class TestRejectEngine : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    return makeDesc("test-reject");
  }

  [[nodiscard]] av::ValidateResult validate(const av::Scenario&) const override {
    av::ValidateResult out;
    out.ok = false;
    out.errors.push_back("bad room");
    return out;
  }

  av::Result run(const av::Scenario&, const av::SimParams&, const av::CancelToken&,
                 av::ProgressFn) override {
    return {};
  }
};

class TestThrowEngine : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    return makeDesc("test-throw");
  }

  [[nodiscard]] av::ValidateResult validate(const av::Scenario&) const override {
    return {};
  }

  av::Result run(const av::Scenario&, const av::SimParams&, const av::CancelToken&,
                 av::ProgressFn) override {
    throw std::runtime_error("boom");
  }
};

// REGISTER_ENGINE works once per TU (macro pastes unexpanded __LINE__ via
// ##); register these test-only engines explicitly instead.
const bool g_registered = [] {
  auto& registry = av::EngineRegistry::instance();
  registry.add([] { return std::make_unique<TestDummyEngine>(); });
  registry.add([] { return std::make_unique<TestSleepEngine>(); });
  registry.add([] { return std::make_unique<TestBlockerEngine>(); });
  registry.add([] { return std::make_unique<TestRejectEngine>(); });
  registry.add([] { return std::make_unique<TestThrowEngine>(); });
  return true;
}();

[[nodiscard]] av::Scenario makeScenario(const std::string& engineId) {
  av::Scenario scenario;
  scenario.name = "test-scenario";
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.rect.w = 10.0;
  scenario.room.rect.h = 3.0;
  scenario.room.rect.d = 8.0;
  for (const char* face : {"floor", "ceiling", "x+", "x-", "z+", "z-"}) {
    scenario.room.rect.faces[face] = "mat";
  }
  av::Material material;
  material.id = "mat";
  material.name = "mat";
  material.bandsHz.assign(av::kDefaultBandsHz.begin(),
                          av::kDefaultBandsHz.end());
  material.absorption = {0.2, 0.2, 0.2, 0.2, 0.2, 0.2};
  scenario.materials.push_back(material);

  av::Source source;
  source.id = "src-1";
  source.position = {1.0, 1.5, 1.0};
  scenario.sources.push_back(source);

  av::Receiver receiver;
  receiver.id = "rcv-1";
  receiver.position = {8.0, 1.5, 6.0};
  scenario.receivers.push_back(receiver);

  scenario.params.engine = engineId;
  scenario.params.bandsHz.assign(av::kDefaultBandsHz.begin(),
                                 av::kDefaultBandsHz.end());
  return scenario;
}

class SimTest : public ::testing::Test {
 protected:
  void SetUp() override { resetProbe(); }
};

}  // namespace

TEST_F(SimTest, SubmitWaitSucceedsWithResult) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("test-dummy"));

  const std::optional<av::JobStatus> done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->id, id);
  EXPECT_EQ(done->state, av::JobState::Succeeded);
  ASSERT_TRUE(done->result.has_value());
  ASSERT_TRUE(done->result->metrics.has_value());
  EXPECT_TRUE(done->error.empty());
  EXPECT_TRUE(done->startedAt.has_value());
  EXPECT_TRUE(done->finishedAt.has_value());
}

TEST_F(SimTest, SubmitSabineEyringSucceeds) {
  av::linkBuiltinEngines();
  av::Scenario scenario = makeScenario("sabine-eyring");

  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(scenario);
  const std::optional<av::JobStatus> done = mgr.wait(id, 5000ms);

  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Succeeded);
  ASSERT_TRUE(done->result.has_value());
  ASSERT_TRUE(done->result->metrics.has_value());
  EXPECT_TRUE(done->result->metrics->rt60.contains(500.0));
}

TEST_F(SimTest, UnknownEngineFails) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("no-such-engine"));

  const std::optional<av::JobStatus> done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Failed);
  EXPECT_FALSE(done->error.empty());
  EXPECT_FALSE(done->result.has_value());
}

TEST_F(SimTest, ValidateFailureMarksFailed) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("test-reject"));

  const std::optional<av::JobStatus> done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Failed);
  EXPECT_NE(done->error.find("bad room"), std::string::npos);
}

TEST_F(SimTest, ExceptionInEngineMarksFailed) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("test-throw"));

  const std::optional<av::JobStatus> done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Failed);
  EXPECT_NE(done->error.find("boom"), std::string::npos);
}

TEST_F(SimTest, CancelQueuedJobNeverRuns) {
  av::JobManager mgr(1);
  const av::JobId blockerId = mgr.submit(makeScenario("test-blocker"));
  ASSERT_TRUE(waitUntil([&] {
    const auto st = mgr.status(blockerId);
    return st.has_value() && st->state == av::JobState::Running;
  }));

  const av::JobId dummyId = mgr.submit(makeScenario("test-dummy"));
  const auto queued = mgr.status(dummyId);
  ASSERT_TRUE(queued.has_value());
  EXPECT_EQ(queued->state, av::JobState::Queued);

  EXPECT_TRUE(mgr.cancel(dummyId));
  const auto cancelled = mgr.status(dummyId);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_EQ(cancelled->state, av::JobState::Cancelled);
  EXPECT_TRUE(cancelled->finishedAt.has_value());

  {
    std::lock_guard lock(g_mutex);
    g_probe.blockerReleased = true;
  }
  g_cv.notify_all();

  const auto blockerDone = mgr.wait(blockerId, 2000ms);
  ASSERT_TRUE(blockerDone.has_value());
  EXPECT_EQ(blockerDone->state, av::JobState::Succeeded);

  std::lock_guard lock(g_mutex);
  EXPECT_EQ(g_probe.dummyRuns, 0);
}

TEST_F(SimTest, CancelRunningJobMarksCancelled) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("test-sleep"));
  ASSERT_TRUE(waitUntil([&] {
    const auto st = mgr.status(id);
    return st.has_value() && st->state == av::JobState::Running;
  }));

  EXPECT_TRUE(mgr.cancel(id));
  const std::optional<av::JobStatus> done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Cancelled);
  EXPECT_FALSE(done->result.has_value());

  std::lock_guard lock(g_mutex);
  EXPECT_TRUE(g_probe.sleepSawCancel);
}

TEST_F(SimTest, CancelUnknownJobReturnsFalse) {
  av::JobManager mgr(1);
  EXPECT_FALSE(mgr.cancel("job-nope"));
  EXPECT_FALSE(mgr.status("job-nope").has_value());
}

TEST_F(SimTest, CancelAfterTerminalReturnsFalse) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("test-dummy"));
  const auto done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Succeeded);
  EXPECT_FALSE(mgr.cancel(id));
}

TEST_F(SimTest, TwoJobsBothComplete) {
  av::JobManager mgr(1);  // sequential pool still finishes both
  const av::JobId a = mgr.submit(makeScenario("test-dummy"));
  const av::JobId b = mgr.submit(makeScenario("test-dummy"));
  EXPECT_NE(a, b);

  const auto doneA = mgr.wait(a, 2000ms);
  const auto doneB = mgr.wait(b, 2000ms);
  ASSERT_TRUE(doneA.has_value());
  ASSERT_TRUE(doneB.has_value());
  EXPECT_EQ(doneA->state, av::JobState::Succeeded);
  EXPECT_EQ(doneB->state, av::JobState::Succeeded);

  const auto all = mgr.list();
  EXPECT_EQ(all.size(), 2u);
}

TEST_F(SimTest, ScenarioIsSnapshotAtSubmit) {
  av::JobManager mgr(1);
  av::Scenario scenario = makeScenario("test-dummy");
  scenario.name = "before-submit";
  const av::JobId id = mgr.submit(scenario);
  scenario.name = "after-submit";  // must not leak into the job

  const auto done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Succeeded);

  std::lock_guard lock(g_mutex);
  EXPECT_EQ(g_probe.lastScenarioName, "before-submit");
}

TEST_F(SimTest, ProgressIsRecorded) {
  av::JobManager mgr(1);
  const av::JobId id = mgr.submit(makeScenario("test-sleep"));
  EXPECT_TRUE(waitUntil([&] {
    const auto st = mgr.status(id);
    return st.has_value() && !st->progress.stage.empty();
  }));

  EXPECT_TRUE(mgr.cancel(id));
  const auto done = mgr.wait(id, 2000ms);
  ASSERT_TRUE(done.has_value());
  EXPECT_EQ(done->state, av::JobState::Cancelled);
  EXPECT_EQ(done->progress.stage, "step");
}

TEST_F(SimTest, RunNowSucceeds) {
  const av::JobStatus st = av::runNow(makeScenario("test-dummy"));
  EXPECT_EQ(st.state, av::JobState::Succeeded);
  ASSERT_TRUE(st.result.has_value());
  EXPECT_TRUE(st.startedAt.has_value());
  EXPECT_TRUE(st.finishedAt.has_value());

  std::lock_guard lock(g_mutex);
  EXPECT_EQ(g_probe.dummyRuns, 1);
}

TEST_F(SimTest, RunNowUnknownEngineFails) {
  const av::JobStatus st = av::runNow(makeScenario("no-such-engine"));
  EXPECT_EQ(st.state, av::JobState::Failed);
  EXPECT_FALSE(st.error.empty());
}
