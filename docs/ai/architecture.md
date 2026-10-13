# Architecture

Code-grounded map of layers, dependencies, documents, coordinate frames, and
runtime stores. Sources: `libs/**/CMakeLists.txt`, `apps/**`, headers under
`libs/*/include/av/`, `ARCHITECTURE.md`.

## 1. Layers and layout

```
libs/core     # target av_core  — acoustics, geometry, materials, metrics,
              #                    scenario/scene models, mesher. No HTTP, no files.
libs/io       # target av_io    — scenario/scene JSON, OBJ/glTF import+export,
              #                    WAV, metrics/field/result JSON, mesh report,
              #                    source catalog, scenarioFromScene.
libs/sim      # target av_sim   — job queue (JobManager) and runNow. No IO.
apps/cli      # av-cli          — run / engines / mesh / gen-mesh
apps/api      # av-api          — Crow REST + WS /v1, in-memory stores
engines/      # reserved for future standalone engines; today only README
schemas/      # scenario, scene, engine-descriptor, source-catalog JSON Schemas
tests/unit    # GoogleTest
tests/integration  # api_smoke.py, cli_field_smoke.py
```

### Dependency rules (enforced by link direction)

| Target | Depends on | Must NOT depend on |
|--------|------------|--------------------|
| `av_core` | (nlohmann_json only) | `av_io`, `av_sim`, Crow |
| `av_io` | `av_core` | `av_sim` |
| `av_sim` | `av_core` | `av_io` (jobs stay IO-free) |
| `av-cli` | `av_io`, `av_core`, `av_sim` | Crow |
| `av-api` | `av_sim`, `av_io`, `av_core`, Crow | — |

Disk loading of `room.mesh.uri` happens at the CLI/API boundary via
`av_io::fillLoadedMesh` — `JobManager` and engines never open files
(`libs/core/include/av/scenario/scenario.hpp`, `libs/io/include/av/io/io.hpp`).

## 2. Two documents: Scenario vs Scene

These are **different types with different id spaces**. Do not conflate them.

| | Scenario | Scene (room editor) |
|---|----------|---------------------|
| Schema | `schemas/scenario.schema.json` | `schemas/scene.schema.json` |
| C++ type | `av::Scenario` (`scenario.hpp`) | `av::Scene` (`scene_model.hpp`) |
| Purpose | what a **simulation** runs | what the **editor** authors |
| Room | `rect` (w/h/d + 6 faces) or `mesh` (uri/unit/scale/overrides) | entities: walls, solids, extrudes, voids, sources, receivers |
| Params | `SimParams` (engine, bands, methodParams, fieldGrid, …) | `MeshSettings` only (mesher input, never copied into a scenario) |
| API | `/v1/scenarios`, `/v1/simulations` | `/v1/scenes`, `/mesh`, `/to-scenario` |
| Store | `SceneStore` (Scenario snapshots) | `EditorSceneStore` |
| Frames | rect: **Y-up**; mesh: as loaded (scene meshes are Z-up) | **Z-up** |

`SceneMaterial` is an alias of `Material` so to-scenario can copy materials
as-is. `scenarioFromScene` (`libs/io/src/scene_to_scenario.cpp`) compiles a
Scene into a Scenario; it does not mutate the Scene.

## 3. Coordinate frames (Z-up vs Y-up)

| Context | Up axis | Notes |
|---------|---------|-------|
| Scene editor document | **+Z** | wall `height` extrudes along +Z; `SolidExtrude` polygon is in XY |
| Mesh rooms / RT engines | **+Z** | `generateSurfaceMesh` output; ray-tracing and hybrid run in this frame |
| Rect rooms (sabine-eyring, image-source) | **Y-up** | `RectRoom.h` along +y, `d` along +z; face names `floor/ceiling/x+/x-/z+/z-` |

to-scenario rect compile swaps `(x, z, y)` and translates so the room min
corner is the origin (`toRectPosition` in `scene_to_scenario.cpp`). Mesh
compile keeps the scene Z-up frame unchanged.

## 4. Runtime stores (all in-memory, no persistence in v1)

| Store | File | Holds | Id prefix |
|-------|------|-------|-----------|
| `JobManager` | `libs/sim/…/job_manager.hpp` | simulation jobs (scenario snapshot by value) | `job-…` |
| `RunsStore` | `apps/api/runs_store.hpp` | finished-run history + submit metadata | `run-…` (record key), keeps `jobId` |
| `SceneStore` | `apps/api/scene_store.hpp` | Scenario snapshots for interactive `scene.patch` | `sceneId` (opaque string) |
| `EditorSceneStore` | `apps/api/editor_scene_store.hpp` | Scene documents + last mesh/report | `sceneId` (separate space) |

### meshjob vs JobManager

- **Simulation jobs** go through `JobManager` (`av_sim`): worker pool,
  `CancelToken`, progress, `JobStatus`, artifacts under `/v1/jobs/{jobId}/…`.
  Ids look like `job-%08x-%04x`.
- **Mesh generation jobs** are bookkeeping on `EditorSceneStore`
  (`MeshJobSnapshot`), id prefix **`meshjob-`** on purpose so they can never
  collide with simulation artifact handles. Mesh runs on a detached thread via
  `EditorSceneStore::runDetached`; there is no queue, no runs-history entry,
  and `/v1/jobs/*` does not serve them. `replace()` of a scene drops the
  generated mesh/report because artifacts belong to the document version they
  were built from.

### runId ≠ jobId

`RunRecord` splits handles: `runId` (`run-…`) is the history key returned as
`id` in `/v1/runs` items; `jobId` (`job-…`) stays the artifact handle for
`/v1/jobs/{jobId}/…`. `GET /v1/runs/{id}` accepts **either** handle
(`RunsStore::get`). `job.succeeded` WS events carry `simulationId` = jobId.

## 5. Threading

- `JobManager`: N workers, `threadCount == 0` → `max(1, hardware_concurrency - 1)`.
- `JobWatcher` (`apps/api/job_watcher.*`): 100 ms poll loop, publishes
  `job.*` WS events and appends terminal runs.
- `EventHub`: mutex-guarded subscriber maps; publish from watcher thread,
  subscribe from Crow io threads.
- `EditorSceneStore::runDetached`: destructor blocks until in-flight mesh work
  finishes.

## 6. Apps

### av-cli (`apps/cli/main.cpp`)

| Command | Behavior |
|---------|----------|
| `av-cli engines` | lists registered `EngineDescriptor`s (id, name, version, artifacts) |
| `av-cli run <scenario.json> [-o outDir]` | load scenario, `fillLoadedMesh` with **scenario parent dir** as base, `engine->run` synchronously, write `metrics.json` / `ir.wav` / `field.json` per produced artifact |
| `av-cli mesh <scene.json> [-o out.obj] [--report r.json] [--quality draft\|standard\|fine]` | surface mesh + mesh-report; `--quality` fully overrides density settings |
| `av-cli gen-mesh box\|corridor\|lroom …` | generates fixture meshes + optional scenario |

### av-api (`apps/api/main.cpp`)

Crow `SimpleApp`, default `127.0.0.1:8787`, flags `--port/-p`, `--bind`.
Startup: `linkBuiltinEngines()`, load source catalog
(`$AV_SOURCE_CATALOG` or `exeDir/source_catalog.json`), construct stores +
`JobWatcher`, `registerRoutes(...)`. **No CORS headers** (desktop contract;
see `gotchas.md`).

## 7. Registration / linkage

Engines register themselves with `REGISTER_ENGINE(Type)` at static-init time.
Because the engine TUs live in the `av_core` **static** library, a mere
registry lookup does not pull those TUs in — `builtin_engines.cpp` provides
`linkBuiltinEngines()` which calls empty `forceLink*Engine()` symbols defined
in each engine TU. `av-cli` and `av-api` must call `linkBuiltinEngines()`
before touching the registry (both do).

## 8. Build

- CMake ≥ 3.24, C++20, `CXX_EXTENSIONS OFF`.
- Presets: `linux-debug`, `windows-msvc-x64-*` (`CMakePresets.json`).
- Packages: vcpkg manifest (`vcpkg.json`) where available; `find_package`
  first, **FetchContent fallback** for nlohmann_json, tinyobjloader, tinygltf,
  asio, Crow, GoogleTest (pinned tags — see `gotchas.md`).
- Tests: `ctest` via presets; baseline ≈ 237.
