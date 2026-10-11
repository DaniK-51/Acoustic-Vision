#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <av/acoustics/engine.hpp>

namespace av {

using JobId = std::string;

enum class JobState {
  Queued,
  Running,
  Succeeded,
  Failed,
  Cancelled,
};

[[nodiscard]] constexpr bool isTerminal(JobState state) {
  return state == JobState::Succeeded || state == JobState::Failed ||
         state == JobState::Cancelled;
}

struct JobStatus {
  JobId id;
  JobState state = JobState::Queued;
  // Last write wins; throttling is a presentation concern (API/WS).
  Progress progress;
  // Set iff state == Failed.
  std::string error;
  // Set iff state == Succeeded.
  std::optional<Result> result;

  std::chrono::system_clock::time_point submittedAt{};
  std::optional<std::chrono::system_clock::time_point> startedAt;
  std::optional<std::chrono::system_clock::time_point> finishedAt;
};

}  // namespace av
