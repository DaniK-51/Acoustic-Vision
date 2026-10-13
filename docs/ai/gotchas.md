# Gotchas

Traps verified against the code. Read before touching engines, API, or mesh
IO. Each item names the file where the behavior lives.

## powerDb is a level, not energy

`Source::powerDb` is a **dB power level**. Engines convert
`energy = 10^(powerDb/10)` (`libs/core/src/acoustics/rt_core.cpp`). It is not
watts and not an amplitude. Doubling power is `+3.01 dB`, not `×2` in the
energy budget before the conversion. Scene `SourceRef.powerDb` uses the same
semantics after to-scenario copies it.

## Isotropic directivity — modeled, not applied

`Source.directivity` (and catalog `directivity`) is parsed, stored, and
serialized (`scenario_json.cpp`, `source_catalog.cpp`), but the RT/ISM engines
launch rays with `sampleDirection` = **uniform sphere** and never multiply in
polar gains. `orientation` (yaw/pitch) is likewise stored only. Do not claim
directive sources change the field; wiring directivity into emission is a
future feature, not a bugfix.

## IR is an unsigned energy envelope

`IrBuffer.samples` store **energy** (`energy = amplitude²`), not signed
amplitude. `params.signedIr = true` does **not** produce a signed IR — engines
add a validate **warning** and still output the envelope
(`image_source_engine.cpp`, `rt_core.cpp`). WAV export is IEEE float32 of
those values.

## runId ≠ jobId ≠ meshjobId

| Id | Prefix | Meaning |
|----|--------|---------|
| runId | `run-…` | runs-history key (`id` in `/v1/runs` items) |
| jobId | `job-…` | simulation artifact handle (`/v1/jobs/{jobId}/…`, WS `simulationId`) |
| mesh job | `meshjob-…` | `EditorSceneStore` mesh generation bookkeeping |

`GET /v1/runs/{id}` accepts runId **or** jobId. `/v1/jobs/*` never serves
mesh jobs. `job.succeeded` events carry `simulationId` = jobId, not runId
(`job_watcher.cpp`).

## No CORS headers

av-api sends **no** `Access-Control-*` headers. The Electron shell injects
them in `onHeadersReceived` and posts JSON as `text/plain` to avoid preflight.
Do not "fix" this by adding CORS middleware without an explicit decision —
the desktop contract is intentional (`docs-site/guide/gui.md`, `gui/README.md`).

## mesh.uri resolves against CWD (API) / scenario dir (CLI)

`fillLoadedMesh` uses `baseDir` when given, otherwise the **process CWD**
(`libs/io/include/av/io/io.hpp`):

- `av-cli run` passes `scenarioPath.parent_path()`.
- `av-api` passes nothing → **CWD of the av-api process**. README tells you to
  start av-api from the repo root for this reason.
- `POST /v1/simulations` fails fast with `400 "mesh load failed"` instead of
  letting a job run on an empty shell (silent α=0 disaster).

`to-scenario` returns an **absolute** temp-file uri for the same reason.

## REGISTER_ENGINE + static library force-link

`REGISTER_ENGINE(Type)` runs at static-init of the engine TU. Those TUs live
in the `av_core` **static** library, so a registry-only reference does not
pull them in — the engine silently vanishes from `GET /v1/engines`. Mitigation
already in tree: `forceLink*Engine()` symbols called from
`linkBuiltinEngines()` (`builtin_engines.cpp`). **New engines must add both
the `REGISTER_ENGINE` line and a force-link hook.** `av-cli`/`av-api` call
`linkBuiltinEngines()` before using the registry.

The `REGISTER_ENGINE` macro pastes `__LINE__` via a two-level `AV_CONCAT` —
do not "simplify" that or multiple registrations in one TU collide.

## FetchContent fallbacks (network at configure time)

`find_package` first (vcpkg/system), then FetchContent with **pinned tags**:

| Package | Tag | Declared in |
|---------|-----|-------------|
| nlohmann_json | v3.11.3 | root `CMakeLists.txt` |
| tinyobjloader | v2.0.0rc13 | `libs/io/CMakeLists.txt` |
| tinygltf | v2.9.3 | `libs/io/CMakeLists.txt` |
| asio | asio-1-30-2 | `apps/api/CMakeLists.txt` |
| Crow | v1.2.0 | `apps/api/CMakeLists.txt` |
| googletest | v1.14.0 | `tests/CMakeLists.txt` |

Crow is configured `CROW_USE_BOOST=OFF` and needs asio. Offline/CI builds
must pre-provide the packages or a populated FetchContent cache.

## Scene vs Scenario stores are different id spaces

`/v1/scenarios/*` → `SceneStore` (Scenario snapshots, `scene.patch`).
`/v1/scenes/*` → `EditorSceneStore` (editor Scene documents, mesh, to-scenario).
Both mint opaque `sceneId`s independently — a sceneId from one API is not
valid on the other. WS `subscribeScene` / `scene.patch` talk to **SceneStore**
(the interactive Scenario), not the editor scenes.

## PUT/PATCH on /v1/scenes is full replace

Both verbs run the same handler: the body must be the **complete** Scene
document. There is no partial JSON merge in v1. Replace bumps `sceneVersion`
and **drops** the generated mesh/report.

## Mesher ignores targetEdge / maxEdge

`generateSurfaceMesh` emits explicit topology only; `quality` /
`targetEdge` / `maxEdge` are resolved into `settingsUsed` for the report but
are **not applied** to the triangulation (`scene_mesher.hpp`). `advancing-front`
falls back to ear-clip with a warning. Do not document quality presets as
actual density control until a refinement slice lands.

## Openings/voids are CSG-lite

Openings punch simple rectangular holes in inner/outer wall faces (grid
split). No jamb/reveal faces; slab caps are not cut (sill=0 doors leave the
bottom cap). Voids only cut wall faces they actually penetrate; solids ignore
voids (warning). Warnings are expected output, not bugs.

## field values are linear energy

`SpatialField.unit` is `"energy"` — values are linear energy density, **not
dB**. Convert at the API/UI edge if a level view is needed. Grid dims clamp
to `kMaxFieldDim = 64` per axis with a validate warning. Index order is
x-fastest: `i = x + nx*(y + ny*z)`.

## airAbsorption is inert

`params.airAbsorption` is parsed, stored, and shows up in `paramsDigest`, but
**no engine applies atmospheric absorption**. Setting it changes nothing in
the result. (Contrast `signedIr`, which at least warns.)

## findings is a placeholder

`Result.findings` is `struct Findings {};` — the artifact kind exists in the
descriptor vocabulary but no engine produces it and `resultToJson` does not
serialize it.

## Scene Store patches are all-or-nothing

`scene.patch` ops are validated first; a single bad op rejects the **whole**
patch and the scene is left untouched (`scene_store.hpp`). Unknown op kinds
are errors (not ignored) — unlike unknown WS `op` values at the envelope
level, which are silently dropped.

## mesh jobs: last submit wins, stale workers can't clobber

`EditorSceneStore::beginMeshJob` replaces the current job; `finishMeshJob` /
`failMeshJob` only land if the jobId is still current. A slow older worker
cannot overwrite a newer generation. `runDetached` workers are joined in the
store destructor.

## Job cancel semantics

`JobManager::cancel`: queued → `Cancelled` immediately (never runs); running →
sets `CancelToken`, state becomes `Cancelled` once the engine returns or
throws. `POST /v1/jobs/{id}/cancel` answers `409` when the job is already
terminal. Engines that ignore the token will run to completion.

## sync=1 blocks the HTTP handler

`POST /v1/simulations?sync=1` waits for the job (`jobs.wait`); long ray-tracing
runs will hold the request. Default async (`202` + poll/WS) is the supported
path for compute-heavy work. Same pattern on `POST /v1/scenes/{id}/mesh?sync=1`.

## WS silent drops

Malformed client frames, unknown `op` values, and `subscribe` with empty
`jobId` are **ignored** with no error reply (REST is authoritative).
`subscribeScene` with empty `sceneId` is also dropped; `scene.patch` with
missing `sceneId` answers `scene.error`. Only `subscribeScene` sends an ack
(`scene.subscribed`) — job `subscribe` does not.

## api_smoke.py freezes the wire

Paths, JSON keys, and event names asserted in
`tests/integration/api_smoke.py` are a **contract**. Renaming a field without
updating the smoke breaks CI and the GUI. "Move only, no redesign"
(`api_routes.hpp` comment).

## Band key "0" means broadband

Metrics JSON keys are band centers as strings; `"0"` (i.e. `0.0` in C++) is
the broadband slot derived from a broadband IR (`metrics_json.cpp`). RT/ISM
IRs produced today are broadband; `bandsHz` empty means broadband.

## glTF import ignores node transforms

`loadGltf` does not apply node transforms — exporters must bake placement into
vertex positions. Triangle primitives only (mode 4); sparse accessors and
non-triangle primitives throw.

## Default engine depends on the to-scenario branch

Closed-box scenes compile to **rect** + `image-source` (+ `maxReflectionOrder:
10`). Everything else compiles to **mesh** + `ray-tracing`. If you expect ISM
on a non-box scene, you will not get it — pick the engine explicitly in
`params`.
