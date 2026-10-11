#include "runs_store.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <utility>

namespace av::api {
namespace {

using Clock = std::chrono::system_clock;

}  // namespace

nlohmann::json paramsDigestOf(const SimParams& params) {
  nlohmann::json digest;
  digest["engine"] = params.engine;
  digest["maxReflectionOrder"] = params.maxReflectionOrder;
  digest["sampleRate"] = params.sampleRate;
  digest["durationSec"] = params.durationSec;
  digest["bandsHz"] = params.bandsHz;
  digest["speedOfSound"] = params.speedOfSound;
  digest["capturePaths"] = params.capturePaths;
  digest["airAbsorption"] = params.airAbsorption;
  digest["signedIr"] = params.signedIr;
  if (params.fieldGrid.enabled) {
    digest["fieldGrid"] = {{"nx", params.fieldGrid.nx},
                           {"ny", params.fieldGrid.ny},
                           {"nz", params.fieldGrid.nz}};
  }
  if (!params.methodParams.is_null() && !params.methodParams.empty()) {
    digest["methodParams"] = params.methodParams;
  }
  return digest;
}

std::string iso8601(Clock::time_point tp) {
  const std::time_t secs = Clock::to_time_t(tp);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &secs);
#else
  gmtime_r(&secs, &tm);
#endif
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      tp.time_since_epoch()) %
                  1000;
  char buf[64];  // sized for any tm_year %04d can produce
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
  return std::string{buf};
}

nlohmann::json runSummaryJson(const RunRecord& run) {
  nlohmann::json json;
  // Both fields carry the same handle (P1-P2 collapse); GUI may use either.
  json["id"] = run.runId;
  json["jobId"] = run.jobId;
  json["engine"] = run.engine;
  json["scenarioName"] = run.scenarioName;
  if (run.sceneVersion.has_value()) {
    json["sceneVersion"] = *run.sceneVersion;
  }
  json["state"] = run.state;
  json["submittedAt"] = iso8601(run.submittedAt);
  json["finishedAt"] = iso8601(run.finishedAt);
  json["paramsDigest"] = run.paramsDigest;
  return json;
}

nlohmann::json runToJson(const RunRecord& run) {
  nlohmann::json json = runSummaryJson(run);
  json["links"] = {{"result", "/v1/jobs/" + run.jobId + "/result"},
                   {"ir", "/v1/jobs/" + run.jobId + "/ir.wav"},
                   {"field", "/v1/jobs/" + run.jobId + "/field"}};
  return json;
}

void RunsStore::appendLocked(const JobId& jobId, std::string state,
                             Clock::time_point finishedAt) {
  const auto pendingIt = m_pending.find(jobId);
  RunRecord record;
  record.runId = jobId;
  record.jobId = jobId;
  record.engine = pendingIt->second.engine;
  record.scenarioName = pendingIt->second.scenarioName;
  record.sceneVersion = pendingIt->second.sceneVersion;
  record.state = std::move(state);
  record.submittedAt = pendingIt->second.submittedAt;
  record.finishedAt = finishedAt;
  record.paramsDigest = std::move(pendingIt->second.paramsDigest);
  m_pending.erase(pendingIt);
  m_history.push_back(std::move(record));
}

void RunsStore::noteSubmitted(const JobId& jobId, std::string engine,
                              std::string scenarioName,
                              nlohmann::json paramsDigest,
                              Clock::time_point submittedAt,
                              std::optional<std::uint64_t> sceneVersion) {
  std::lock_guard lock(m_mutex);
  Pending pending;
  pending.engine = std::move(engine);
  pending.scenarioName = std::move(scenarioName);
  pending.sceneVersion = std::move(sceneVersion);
  pending.submittedAt = submittedAt;
  pending.paramsDigest = std::move(paramsDigest);
  m_pending[jobId] = std::move(pending);
  // Terminal beat submit (fast job / sync wait): finish the record now.
  const auto early = m_earlyTerminal.find(jobId);
  if (early != m_earlyTerminal.end()) {
    const std::string state = early->second.first;
    const Clock::time_point finishedAt = early->second.second;
    m_earlyTerminal.erase(early);
    appendLocked(jobId, state, finishedAt);
  }
}

bool RunsStore::noteTerminal(const JobId& jobId, std::string state,
                             Clock::time_point finishedAt) {
  std::lock_guard lock(m_mutex);
  const auto duplicate = std::find_if(
      m_history.begin(), m_history.end(),
      [&jobId](const RunRecord& run) { return run.runId == jobId; });
  if (duplicate != m_history.end()) {
    return false;
  }
  const auto pendingIt = m_pending.find(jobId);
  if (pendingIt == m_pending.end()) {
    // Unknown yet: keep the terminal note until noteSubmitted fills metadata.
    m_earlyTerminal[jobId] = {state, finishedAt};
    return true;
  }
  appendLocked(jobId, std::move(state), finishedAt);
  return true;
}

std::vector<RunRecord> RunsStore::list(
    const std::optional<std::string>& engineFilter) const {
  std::lock_guard lock(m_mutex);
  std::vector<RunRecord> out;
  out.reserve(m_history.size());
  for (const RunRecord& run : m_history) {
    if (!engineFilter.has_value() || run.engine == *engineFilter) {
      out.push_back(run);
    }
  }
  std::sort(out.begin(), out.end(),
            [](const RunRecord& a, const RunRecord& b) {
              if (a.finishedAt != b.finishedAt) {
                return a.finishedAt > b.finishedAt;
              }
              return a.runId > b.runId;
            });
  return out;
}

std::optional<RunRecord> RunsStore::get(const JobId& runId) const {
  std::lock_guard lock(m_mutex);
  const auto it = std::find_if(
      m_history.begin(), m_history.end(),
      [&runId](const RunRecord& run) { return run.runId == runId; });
  if (it == m_history.end()) {
    return std::nullopt;
  }
  return *it;
}

}  // namespace av::api
