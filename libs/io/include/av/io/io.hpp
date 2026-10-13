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
#include <av/scene/scene_mesher.hpp>
#include <av/scene/scene_model.hpp>

namespace av {

// Scenario JSON <-> Scenario, fields of schemas/scenario.schema.json.
// Throws std::runtime_error on structural errors (missing/ill-typed fields).
[[nodiscard]] nlohmann::json scenarioToJson(const Scenario& scenario);
[[nodiscard]] Scenario scenarioFromJson(const nlohmann::json& json);

// File helpers; throw std::runtime_error on IO or parse failure.
[[nodiscard]] Scenario loadScenario(const std::filesystem::path& path);
void saveScenario(const Scenario& scenario, const std::filesystem::path& path);

// Scene JSON <-> Scene, fields of schemas/scene.schema.json (room-editor
// document, V0.2). Parallel to scenario JSON — does not replace it. Source and
// receiver entities serialize into top-level `sources` / `receivers`; geometry
// entities stay in `entities`. Throws std::runtime_error on structural errors.
[[nodiscard]] nlohmann::json sceneToJson(const Scene& scene);
[[nodiscard]] Scene sceneFromJson(const nlohmann::json& json);

// File helpers; throw std::runtime_error on IO or parse failure.
[[nodiscard]] Scene loadScene(const std::filesystem::path& path);
void saveScene(const Scene& scene, const std::filesystem::path& path);

// to-scenario compile (docs/room-editor-mesh.md §4). Copies materials and
// sources/receivers; meshSettings are a mesher input and never land in the
// scenario. Two shapes:
// - Rect — the scene is a closed box: exactly 4 axis-aligned walls (equal
//   height, baselines on z=0 forming one rectangle, no openings) plus a floor
//   and a ceiling SolidBox slab whose XY footprint is exactly that rectangle
//   and which touch z=0 / z=height. Wall faces.inner map to x+/x-/z+/z- by
//   wall position; the slab materialIds map to floor/ceiling. Scene is Z-up
//   while RectRoom is Y-up (h along +y): positions are swapped (x,z,y) and
//   translated so the room min corner is the origin. Default engine
//   "image-source".
// - Mesh — anything else: generateSurfaceMesh over the scene is written to
//   `meshPath` (required here; throws std::runtime_error when empty) and
//   referenced via room.mesh.uri (unit "m", identity materialOverrides —
//   mesh slots already carry scene material ids). Positions keep the scene
//   Z-up frame. Default engine "ray-tracing".
// The generated mesh is not attached to the returned scenario; disk loading
// stays at the CLI/API boundary via fillLoadedMesh.
[[nodiscard]] Scenario scenarioFromScene(
    const Scene& scene, const std::filesystem::path& meshPath = {});

// mesh-report.json (docs/room-editor-mesh.md §3.4, slice R8): generation
// stats for the GUI/API bottom sheet and CLI --report.
struct MeshReport {
  std::string kind = "surface";  // "surface" | "volume"
  struct Stats {
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    // Zone / material-slot names present on the mesh.
    std::vector<std::string> zones;
  } stats;
  // [[min], [max]] of all positions; zero box at origin for an empty mesh.
  Aabb bbox{};
  std::vector<std::string> warnings;
  ResolvedSurfaceMeshSettings settingsUsed{};
  // entity id -> contributed triangles (picking trace).
  std::map<std::string, std::size_t> entityTriangleCount;
};

// Fills MeshReport from generateSurfaceMesh output + the resolved settings
// that were (or would be) handed to the mesher.
[[nodiscard]] MeshReport meshReportFrom(
    const SurfaceMeshResult& result,
    const ResolvedSurfaceMeshSettings& settingsUsed);

[[nodiscard]] nlohmann::json meshReportToJson(const MeshReport& report);
void writeMeshReport(const std::filesystem::path& path,
                     const MeshReport& report);

// Wavefront OBJ export (gen-mesh). Writes v / g / usemtl / f lines, one
// `g`+`usemtl` pair per materialSlots entry (floor, ceiling, walls for
// generateBoxMesh shapes). Units meters; triangle winding is preserved as-is
// (box.obj convention). Throws std::runtime_error on IO failure.
void saveObj(const std::filesystem::path& path, const Mesh& mesh);

// Same container as saveObj, encoded to memory (API mesh.obj endpoint).
[[nodiscard]] std::string encodeObj(const Mesh& mesh);

// Wavefront OBJ import. `unit` is "m"|"mm"|"cm"|"in"; vertex positions are
// converted to meters at import (SI convention, AGENTS.md decision 14) and
// multiplied by `scale`. Polygons are triangulated. Throws
// std::runtime_error on IO/parse errors or unknown unit.
[[nodiscard]] Mesh loadObj(const std::filesystem::path& path,
                           const std::string& unit = "m", double scale = 1.0);

// glTF/GLB import (tinygltf). Same unit/scale/material-slot semantics as
// loadObj (material name -> mesh name -> "default"). Triangle primitives
// only (glTF mode 4); non-triangle primitives and sparse accessors throw.
// Node transforms are not applied — exporters must bake placement into
// vertex positions. Throws std::runtime_error on IO/parse errors or
// unknown unit.
[[nodiscard]] Mesh loadGltf(const std::filesystem::path& path,
                            const std::string& unit = "m", double scale = 1.0);

// Dispatches by file extension (case-insensitive): ".obj" -> loadObj,
// ".gltf"/".glb" -> loadGltf. Other extensions throw std::runtime_error.
[[nodiscard]] Mesh loadMesh(const std::filesystem::path& path,
                            const std::string& unit = "m", double scale = 1.0);

// Applies scenario materialOverrides (mesh slot/zone name -> scenario
// material id) to a loaded mesh: matching materialSlots entries are replaced
// by the override value. Unmatched slots keep their names.
void applyMaterialOverrides(Mesh& mesh,
                            const std::map<std::string, std::string>& overrides);

// MeshRoom fields already present in schemas/scenario.schema.json -> Mesh.
// `room.uri` is a filesystem path (file:// prefix stripped); format is
// dispatched by extension via loadMesh. JobManager and
// engines stay IO-free; disk loading happens at the CLI/API boundary via
// fillLoadedMesh below.
[[nodiscard]] Mesh loadMeshRoom(const MeshRoom& room);

// Fills scenario.loadedMesh from room.mesh.uri (P2c boundary helper). No-op
// unless room.type == Mesh, the uri is non-empty and loadedMesh is not set
// yet (idempotent). Relative uris resolve against `baseDir` when non-empty,
// otherwise against the process CWD — CLI passes the scenario file's parent
// directory, the API leaves it empty. Format dispatch matches loadMesh
// (.obj / .gltf / .glb). Throws std::runtime_error when the
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

// Spatial field JSON: {nx, ny, nz, origin, cellSize, bandHz, unit, values}.
// values are linear energy density (unit == "energy"); the dB conversion
// happens at the API/UI edge. Field artifact JSON (CLI field.json,
// GET /v1/jobs/{id}/field) uses this exact shape.
[[nodiscard]] nlohmann::json fieldToJson(const SpatialField& field);
void writeFieldJson(const std::filesystem::path& path, const Result& result);

// metricsToJson + IR summary (sampleRate, durationSec, channels, channelNames)
// + full field object when present (grids are capped at kMaxFieldDim^3)
// + "rays": {"paths": [{points, amplitude, order}]} when the engine produced
// RayPaths (empty "paths" when capturePaths was off but the engine supports
// rays). Key omitted entirely when Result.rays is nullopt.
[[nodiscard]] nlohmann::json resultToJson(const Result& result);
void writeResultJson(const std::filesystem::path& path, const Result& result);

}  // namespace av
