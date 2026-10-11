#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh.hpp>
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

// Wavefront OBJ import. `unit` is "m"|"mm"|"cm"|"in"; vertex positions are
// converted to meters at import (SI convention, AGENTS.md decision 14) and
// multiplied by `scale`. Polygons are triangulated. Throws
// std::runtime_error on IO/parse errors or unknown unit.
[[nodiscard]] Mesh loadObj(const std::filesystem::path& path,
                           const std::string& unit = "m", double scale = 1.0);

// Applies scenario materialOverrides (mesh slot/zone name -> scenario
// material id) to a loaded mesh: matching materialSlots entries are replaced
// by the override value. Unmatched slots keep their names.
void applyMaterialOverrides(Mesh& mesh,
                            const std::map<std::string, std::string>& overrides);

// MeshRoom fields already present in schemas/scenario.schema.json -> Mesh.
// `room.uri` is a filesystem path (file:// prefix stripped). JobManager and
// engines stay IO-free; disk loading happens at the CLI/API boundary via
// fillLoadedMesh below.
[[nodiscard]] Mesh loadMeshRoom(const MeshRoom& room);

// Fills scenario.loadedMesh from room.mesh.uri (P2c boundary helper). No-op
// unless room.type == Mesh, the uri is non-empty and loadedMesh is not set
// yet (idempotent). Relative uris resolve against `baseDir` when non-empty,
// otherwise against the process CWD — CLI passes the scenario file's parent
// directory, the API leaves it empty. Throws std::runtime_error when the
// mesh cannot be loaded or has no geometry; callers must fail the run
// instead of letting engines see an empty shell (silent alpha=0).
void fillLoadedMesh(Scenario& scenario,
                    const std::filesystem::path& baseDir = {});

// IEEE float32 PCM WAV (format tag 3). IrBuffer is planar [channel][sample];
// samples are interleaved on write. Channel count and sample rate must be
// positive; throws std::runtime_error otherwise or on IO failure.
void writeWav(const std::filesystem::path& path, const IrBuffer& ir);

// Same container as writeWav, encoded to memory (API ir.wav endpoint).
[[nodiscard]] std::vector<std::uint8_t> encodeWav(const IrBuffer& ir);

// Metrics JSON: rt60 {schroederT30, sabine, eyring} per band, edt per band,
// earlyReflections list. Band 0 is broadband (Metrics convention).
[[nodiscard]] nlohmann::json metricsToJson(const Metrics& metrics);
void writeMetricsJson(const std::filesystem::path& path, const Result& result);

// metricsToJson + IR summary (sampleRate, durationSec, channels, channelNames).
[[nodiscard]] nlohmann::json resultToJson(const Result& result);
void writeResultJson(const std::filesystem::path& path, const Result& result);

}  // namespace av
