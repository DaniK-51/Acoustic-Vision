#include <gtest/gtest.h>

#include "runs_store.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::system_clock;

av::SimParams sampleParams(const std::string& engine = "image-source") {
  av::SimParams params;
  params.engine = engine;
  params.maxReflectionOrder = 3;
  params.sampleRate = 44100;
  params.durationSec = 0.5;
  params.bandsHz = {125.0, 250.0};
  params.methodParams = nlohmann::json{{"rayCount", 1000}};
  return params;
}

TEST(RunsStoreTest, ParamsDigestCarriesEngineAndKeySimParams) {
  const nlohmann::json digest = av::api::paramsDigestOf(sampleParams());
  EXPECT_EQ(digest.at("engine"), "image-source");
  EXPECT_EQ(digest.at("maxReflectionOrder"), 3);
  EXPECT_EQ(digest.at("sampleRate"), 44100);
  EXPECT_EQ(digest.at("durationSec"), 0.5);
  EXPECT_EQ(digest.at("bandsHz"), nlohmann::json({125.0, 250.0}));
  EXPECT_EQ(digest.at("methodParams").at("rayCount"), 1000);
  EXPECT_FALSE(digest.contains("fieldGrid"));  // disabled -> omitted
}

TEST(RunsStoreTest, ParamsDigestIncludesFieldGridWhenEnabled) {
  av::SimParams params = sampleParams("ray-tracing");
  params.fieldGrid.enabled = true;
  params.fieldGrid.nx = 6;
  params.fieldGrid.ny = 4;
  params.fieldGrid.nz = 5;
  const nlohmann::json digest = av::api::paramsDigestOf(params);
  EXPECT_EQ(digest.at("engine"), "ray-tracing");
  EXPECT_EQ(digest.at("fieldGrid").at("nx"), 6);
  EXPECT_EQ(digest.at("fieldGrid").at("ny"), 4);
  EXPECT_EQ(digest.at("fieldGrid").at("nz"), 5);
}

TEST(RunsStoreTest, TerminalRunIsRecordedAndListedNewestFirst) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-a", "image-source", "demo", nlohmann::json{{"k", 1}},
                      t0 - 30s);
  store.noteSubmitted("job-b", "image-source", "demo", nlohmann::json{{"k", 2}},
                      t0 - 20s);
  ASSERT_TRUE(store.noteTerminal("job-a", "succeeded", t0 - 10s));
  ASSERT_TRUE(store.noteTerminal("job-b", "succeeded", t0));

  const std::vector<av::api::RunRecord> runs = store.list();
  ASSERT_EQ(runs.size(), 2u);
  EXPECT_EQ(runs[0].jobId, "job-b");
  EXPECT_EQ(runs[1].jobId, "job-a");
  EXPECT_NE(runs[0].runId, runs[0].jobId);
  EXPECT_TRUE(runs[0].runId.rfind("run-", 0) == 0);
  EXPECT_EQ(runs[0].state, "succeeded");
  EXPECT_EQ(runs[0].engine, "image-source");
  EXPECT_EQ(runs[0].scenarioName, "demo");
  EXPECT_EQ(runs[0].paramsDigest.at("k"), 2);
  EXPECT_FALSE(runs[0].sceneVersion.has_value());
}

TEST(RunsStoreTest, EngineFilterSelectsMatchingRuns) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-a", "image-source", "demo", {}, t0 - 20s);
  store.noteSubmitted("job-b", "ray-tracing", "box", {}, t0 - 10s);
  store.noteTerminal("job-a", "succeeded", t0 - 5s);
  store.noteTerminal("job-b", "failed", t0);

  const std::vector<av::api::RunRecord> onlyRay =
      store.list(std::string("ray-tracing"));
  ASSERT_EQ(onlyRay.size(), 1u);
  EXPECT_EQ(onlyRay[0].jobId, "job-b");
  EXPECT_EQ(onlyRay[0].state, "failed");

  EXPECT_EQ(store.list(std::string("no-such-engine")).size(), 0u);
  EXPECT_EQ(store.list().size(), 2u);
}

TEST(RunsStoreTest, AllTerminalStatesAreInHistory) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-ok", "image-source", "demo", {}, t0 - 30s);
  store.noteSubmitted("job-bad", "image-source", "demo", {}, t0 - 20s);
  store.noteSubmitted("job-stop", "image-source", "demo", {}, t0 - 10s);
  store.noteTerminal("job-ok", "succeeded", t0 - 25s);
  store.noteTerminal("job-bad", "failed", t0 - 15s);
  store.noteTerminal("job-stop", "cancelled", t0 - 5s);

  const std::vector<av::api::RunRecord> runs = store.list();
  ASSERT_EQ(runs.size(), 3u);
  EXPECT_EQ(store.get("job-ok")->state, "succeeded");
  EXPECT_EQ(store.get("job-bad")->state, "failed");
  EXPECT_EQ(store.get("job-stop")->state, "cancelled");
}

TEST(RunsStoreTest, GetReturnsDetailAndUnknownIsNullopt) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-a", "image-source", "demo",
                      nlohmann::json{{"engine", "image-source"}}, t0 - 10s);
  store.noteTerminal("job-a", "succeeded", t0);

  // Backwards lookup by jobId still resolves the record.
  const std::optional<av::api::RunRecord> run = store.get("job-a");
  ASSERT_TRUE(run.has_value());
  EXPECT_NE(run->runId, run->jobId);  // handle split: runId is its own id
  EXPECT_TRUE(run->runId.rfind("run-", 0) == 0);
  EXPECT_EQ(run->jobId, "job-a");
  EXPECT_FALSE(store.get("job-missing").has_value());
}

TEST(RunsStoreTest, GetAcceptsBothRunIdAndJobId) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-x", "image-source", "demo", {}, t0 - 10s);
  ASSERT_TRUE(store.noteTerminal("job-x", "succeeded", t0));

  const std::optional<av::api::RunRecord> byJob = store.get("job-x");
  ASSERT_TRUE(byJob.has_value());
  const std::optional<av::api::RunRecord> byRun = store.get(byJob->runId);
  ASSERT_TRUE(byRun.has_value());
  EXPECT_EQ(byRun->runId, byJob->runId);
  EXPECT_EQ(byRun->jobId, byJob->jobId);
}

TEST(RunsStoreTest, DuplicateTerminalIsIgnored) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-a", "image-source", "demo", {}, t0 - 10s);
  EXPECT_TRUE(store.noteTerminal("job-a", "succeeded", t0));
  EXPECT_FALSE(store.noteTerminal("job-a", "failed", t0 + 1s));
  ASSERT_EQ(store.list().size(), 1u);
  EXPECT_EQ(store.get("job-a")->state, "succeeded");
}

TEST(RunsStoreTest, TerminalBeforeSubmitStillRecords) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  // Fast job: terminal observed before noteSubmitted lands.
  EXPECT_TRUE(store.noteTerminal("job-fast", "succeeded", t0));
  EXPECT_EQ(store.list().size(), 0u);
  store.noteSubmitted("job-fast", "image-source", "demo",
                      nlohmann::json{{"engine", "image-source"}}, t0 - 5s);
  const std::optional<av::api::RunRecord> run = store.get("job-fast");
  ASSERT_TRUE(run.has_value());
  EXPECT_EQ(run->state, "succeeded");
  EXPECT_EQ(run->engine, "image-source");
  EXPECT_EQ(run->finishedAt, t0);
  EXPECT_TRUE(run->runId.rfind("run-", 0) == 0);  // minted at noteSubmitted
  EXPECT_EQ(run->jobId, "job-fast");
}

TEST(RunsStoreTest, RunJsonSummaryAndDetailShape) {
  av::api::RunRecord record;
  record.runId = "run-1";
  record.jobId = "job-1";
  record.engine = "image-source";
  record.scenarioName = "demo";
  record.state = "succeeded";
  record.submittedAt = Clock::now() - 10s;
  record.finishedAt = Clock::now();
  record.paramsDigest = nlohmann::json{{"engine", "image-source"}};

  const nlohmann::json summary = av::api::runSummaryJson(record);
  EXPECT_EQ(summary.at("id"), "run-1");
  EXPECT_EQ(summary.at("jobId"), "job-1");
  EXPECT_EQ(summary.at("engine"), "image-source");
  EXPECT_EQ(summary.at("scenarioName"), "demo");
  EXPECT_EQ(summary.at("state"), "succeeded");
  EXPECT_TRUE(summary.contains("submittedAt"));
  EXPECT_TRUE(summary.contains("finishedAt"));
  EXPECT_FALSE(summary.contains("links"));
  EXPECT_FALSE(summary.contains("sceneVersion"));

  record.sceneVersion = 7;
  const nlohmann::json detail = av::api::runToJson(record);
  // uint64 on the wire (P3c): numeric, not the old string form.
  EXPECT_TRUE(detail.at("sceneVersion").is_number_unsigned());
  EXPECT_EQ(detail.at("sceneVersion"), 7);
  ASSERT_TRUE(detail.contains("links"));
  EXPECT_EQ(detail.at("links").at("result"), "/v1/jobs/job-1/result");
  EXPECT_EQ(detail.at("links").at("ir"), "/v1/jobs/job-1/ir.wav");
  EXPECT_EQ(detail.at("links").at("field"), "/v1/jobs/job-1/field");
}

TEST(RunsStoreTest, SceneVersionRoundTripsAsUint64) {
  av::api::RunsStore store;
  const auto t0 = Clock::now();
  store.noteSubmitted("job-s", "image-source", "demo", nlohmann::json::object(),
                      t0, std::uint64_t{42});
  ASSERT_TRUE(store.noteTerminal("job-s", "succeeded", t0 + 1s));
  const std::optional<av::api::RunRecord> run = store.get("job-s");
  ASSERT_TRUE(run.has_value());
  ASSERT_TRUE(run->sceneVersion.has_value());
  EXPECT_EQ(*run->sceneVersion, 42u);
  const nlohmann::json detail = av::api::runToJson(*run);
  EXPECT_TRUE(detail.at("sceneVersion").is_number_unsigned());
  EXPECT_EQ(detail.at("sceneVersion"), 42);
}

TEST(RunsStoreTest, Iso8601Format) {
  // 2020-01-01T00:00:00.250Z — fixed instant, ms preserved.
  const auto tp = Clock::from_time_t(1577836800) + 250ms;
  EXPECT_EQ(av::api::iso8601(tp), "2020-01-01T00:00:00.250Z");
}

}  // namespace
