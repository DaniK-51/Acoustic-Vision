#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include <av/acoustics/engine.hpp>
#include <av/metrics/metrics.hpp>
#include <av/scenario/scenario.hpp>

namespace av {

// Scenario JSON <-> Scenario, fields of schemas/scenario.schema.json.
// Throws std::runtime_error on structural errors (missing/ill-typed fields).
[[nodiscard]] nlohmann::json scenarioToJson(const Scenario& scenario);
[[nodiscard]] Scenario scenarioFromJson(const nlohmann::json& json);

// File helpers; throw std::runtime_error on IO or parse failure.
[[nodiscard]] Scenario loadScenario(const std::filesystem::path& path);
void saveScenario(const Scenario& scenario, const std::filesystem::path& path);

// IEEE float32 PCM WAV (format tag 3). IrBuffer is planar [channel][sample];
// samples are interleaved on write. Channel count and sample rate must be
// positive; throws std::runtime_error otherwise or on IO failure.
void writeWav(const std::filesystem::path& path, const IrBuffer& ir);

// Metrics JSON: rt60 {schroederT30, sabine, eyring} per band, edt per band,
// earlyReflections list. Band 0 is broadband (Metrics convention).
[[nodiscard]] nlohmann::json metricsToJson(const Metrics& metrics);
void writeMetricsJson(const std::filesystem::path& path, const Result& result);

// metricsToJson + IR summary (sampleRate, durationSec, channels, channelNames).
[[nodiscard]] nlohmann::json resultToJson(const Result& result);
void writeResultJson(const std::filesystem::path& path, const Result& result);

}  // namespace av
