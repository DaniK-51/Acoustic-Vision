# Acoustic-Vision — Software Architecture

Open-source room acoustics analysis: 3D scene + sources → geometric acoustics (ISM / ray tracing) → IR, metrics (RT60, EDT), spatial field, reflections. C++ core, REST+WS API, Electron + Vue + Three.js GUI (P3).

**Vision** (voted): 3D scene + sources (custom + catalog «as-is») → multi-algorithm compute runs (may take hours on GPU) with history; interactive editing via WS scene patches (static scene lives on backend). Basic editor first, full DCC later. Artifacts = whatever the engine computes (point / field / rays / IR). No measurement/mic pipeline.

**Stage**: architecture only — no implementation yet.  
**Target OS**: Windows (MSVC). **v1 language**: C++20. **Delivery**: core first, GUI later.

Companion docs: [`docs/idea.md`](docs/idea.md) · [`docs/engines.md`](docs/engines.md) · [`AGENTS.md`](AGENTS.md)

---

## 1. Monorepo layout

```
Acoustic-Vision/
├── CMakeLists.txt                 # superbuild, options: AV_BUILD_API, AV_BUILD_TESTS
├── CMakePresets.json              # windows-msvc-x64-debug/release
├── vcpkg.json                     # manifest deps
├── ARCHITECTURE.md                # this document
├── README.md                      # product overview
├── docs/                          # idea.md, engines.md, …
├── schemas/                       # JSON Schemas, single source of truth for API + GUI
│   ├── scenario.schema.json       # present
│   ├── engine-descriptor.schema.json  # present
│   ├── material.schema.json       # planned (P0)
│   ├── simulation-result.schema.json  # planned (P0)
│   └── job.schema.json            # planned (P1)
├── cmake/                         # helpers, warnings, sanitizers
├── engines/                       # calculation methods + _template (see docs/engines.md)
│   ├── sabine_eyring/
│   ├── image_source/
│   └── _template/
├── libs/
│   ├── core/                      # av_core — pure computation, no HTTP, no file IO deps
│   │   ├── include/av/
│   │   │   ├── geometry/          # Vec3, AABB, Mesh, Surface, Room, Bvh
│   │   │   ├── materials/         # Material, AbsorptionTable, BandFreqs
│   │   │   ├── acoustics/         # Engine API: IEngine, Descriptor, Registry, CancelToken
│   │   │   ├── metrics/           # Rt60, Edt, EarlyReflections, IrBuffer
│   │   │   ├── scenario/          # Scenario, Source, Receiver, SimParams
│   │   │   └── units.hpp
│   │   └── src/...
│   ├── io/                        # av_io — OBJ/glTF import, WAV/CSV export, scenario JSON
│   │   ├── include/av/io/
│   │   └── src/
│   └── sim/                       # av_sim — job queue, progress, cancellation
│       ├── include/av/sim/
│       └── src/
├── apps/
│   ├── cli/                       # av-cli: run scenario JSON headless
│   └── api/                       # av-api: REST + WebSocket server (Crow)
├── tests/
│   ├── unit/                      # GoogleTest, per-module
│   ├── integration/               # API round-trips (spawn server or in-process)
│   └── fixtures/                  # analytic rooms, golden IR/metrics, sample OBJ/glTF
├── tools/                         # scripts (schema validation, golden regen)
└── gui/                           # P3 placeholder — empty, README stub only
    └── README.md
```

**Dependency direction** (strict, enforced by CMake `target_link_libraries`):
`av_core` ← `av_io` ← `av_sim` ← (`av-cli`, `av-api`). GUI (P3) talks only to `av-api` over HTTP/WS — never links the core.

---

## 2. C++ core modules

### geometry
- `Vec3`, `Aabb`, `Triangle`, `Mesh` (indexed, normals optional; compute face normals).
- `Surface`: triangle + material id + outward normal convention (document: normals point *into* the room / toward the air).
- `Room`: collection of surfaces + optional axis-aligned bounds. Two specializations behind one type:
  - `RectRoom(w,h,d, materials per face)` — exact, fast, IMS-native.
  - `MeshRoom(Mesh)` — arbitrary; validated for watertightness (warning, not hard fail).
- `Bvh`: AABB tree over triangles. Built lazily; used by ray tracing and mesh visibility queries. IMS-on-rect does not need it.

### materials
- `Material { id, name, AbsorptionTable, scattering }`.
- `AbsorptionTable`: 6 or 8 octave bands (default ISO: 125, 250, 500, 1k, 2k, 4k Hz; extendable to 63 Hz / 8 kHz). Values α ∈ [0,1] per band. Linear interp only for display/export — simulation uses per-band.
- Scattering coefficient stored now (used by ray tracing later); ignored by v1 IMS.

### sources
- `Source { id, position, orientation, powerDb, spectrum (АЧХ), directivity, catalogRef? }`.
- **Custom** source defined inline in scenario.
- **Built-in catalog** (`GET /v1/source-catalog`): real-world devices as fact-based cards (power, frequency response, polar pattern). No measurement pipeline.
- Catalog is data-only; engines consume the resolved Source model.

### acoustics engines (Engine API)
Extensibility goal: **new method = one module + registration** — no edits to API, GUI, or other engines. Full guide: `docs/engines.md`.

```cpp
struct Capabilities {
  bool supportsRect = true;
  bool supportsMesh = false;
  bool supportsRayPaths = false;
  bool supportsBands = true;
  int  maxOrder = 0;           // 0 = N/A
};

struct EngineDescriptor {
  std::string id;              // "image-source"
  std::string name;            // "Image Source Method"
  std::string version;
  Capabilities caps;
  nlohmann::json paramsSchema; // JSON Schema → GUI auto-form
  std::vector<std::string> artifacts; // subset of ir|metrics|field|rays|findings
};

struct ValidateResult {
  bool ok = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

struct Progress { double fraction; std::string stage; };
using ProgressFn = std::function<void(const Progress&)>;

class CancelToken {
public:
  void cancel();
  bool cancelled() const;
};

class IEngine {
public:
  virtual ~IEngine() = default;
  virtual EngineDescriptor descriptor() const = 0;
  virtual ValidateResult validate(const Scenario&) const = 0;
  virtual Result run(const Scenario&, const SimParams&,
                     const CancelToken&, ProgressFn) = 0;
};
```

Registration: `REGISTER_ENGINE(ImageSourceEngine)` → `EngineRegistry` (factory by id + descriptor list).

| Engine | Role (v1+) | Notes |
|---|---|---|
| `sabine-eyring` | analytic RT60 from V, S, ᾱ(f) | no IR; metrics only; instant |
| `image-source` | IR for **rectangular** rooms, order N | classical mirror-source; per-band IR |
| `ray-tracing` | IR + ray paths for **mesh** rooms | P2; same `IEngine` |
| `hybrid` | ISM early + RT late | P2; full IR on mesh |

`EngineRegistry` selects by `simParams.engine` + room type; API returns `400` if engine/capability mismatch (e.g. IMS + mesh).

**Plugin model (A → B):**
- **P0–P2 (A):** compile-time registry + `REGISTER_ENGINE`. Enough for in-repo methods.
- **P2.5+ (B):** optional dynamic `.dll` via stable C ABI `av_engine_api_v1` (`LoadLibrary`). Same descriptor/Result contract — no redesign.
- Adding a method: `engines/<name>/` → implement `IEngine` → register → optional golden test. API `/v1/engines` and GUI pick it up automatically.

**ISM correctness notes (write tests first; conventions frozen in `docs/engines.md` §5):**
- Path length → delay `t = r / c`, amplitude `∝ 1/r` (optional exp air absorption later).
- α is **energy** absorption: energy reflection `(1-α)`, **amplitude** reflection `√(1-α)` per bounce per band; `energy = amplitude²`.
- Sign convention for odd reflections optional (`k = -1` toggle); default energy-only (no polarity).
- Enumerate images up to order N on a 3D integer lattice (n₁,n₂,n₃), filter by bounce count.
- CancelToken must be polled in engine inner loops (required from P0).

### metrics
- `IrBuffer`: multi-channel broadband (one channel per source×receiver pair, name `srcId:rcvId`); `sampleRate`. Per-band IR packs (if ever) are separate IrBuffers — not 3D in one buffer (ADR).
- `RT60`: Schroeder backward integration + linear fit of −5…−35 dB (T30); also report Sabine & Eyring analytic.
- `EDT`: 0 → −10 dB fit.
- `EarlyReflections`: top-K peaks in first 80 ms with time, amplitude, (optional) path polyline for GUI.
- `IrExport`: WAV (float32 PCM) + CSV (time, amplitude per channel/band).
- `SpatialField`: energy density on a regular cubic 3D grid — `nx,ny,nz`, `origin`, `cellSize`, `bandHz` (0 = broadband), linear `values` (index `x + nx*(y + ny*z)`); for heat-maps and “where sound is strong”. Stored linear (`unit: "energy"`); dB conversion happens at the API/UI edge. Engines advertise the `field` artifact and fill it when `params.fieldGrid` is set (ray-tracing deposits ray energy into the cells); others leave `Result.field` empty.

### runs (history)
- A **run** = scenario snapshot + engine + params + result id.
- v1: append-only history per scene (no heavy Study/diff). UI can show two runs side by side.
- **P1 handle simplification:** `jobId` doubles as simulation/result id (`GET /v1/jobs/{id}/result`, `/ir.wav`); WS `simulationId = jobId`. Split stores later without changing handle format.
- **P2e run record** (in-memory, append on terminal — all terminal states): `id` = `jobId` (same value in both fields), `engine`, `scenarioName`, `sceneVersion` (set when submitted via `{"sceneId"}`), `submittedAt`, `finishedAt`, `state`, `paramsDigest` (engine + key sim params). `GET /v1/runs?engine=` lists newest first; `GET /v1/runs/{runId}` adds `links` to `result` / `ir.wav` / `field`.

### scene store + interactive patches
- Backend owns the **Scene Store** (full scenario state, `sceneVersion`, uint64 from 1). In-memory (`SceneStore`, thread-safe); `sceneId` is uuid-lite.
- REST: `POST /v1/scenarios` (scenario JSON → `{sceneId, sceneVersion}`), `GET /v1/scenarios/{sceneId}` (→ `{sceneId, sceneVersion, scenario}`). `POST /v1/simulations` accepts `{"sceneId"}` instead of an inline scenario; the stored snapshot runs and its `sceneVersion` lands in the run record / `paramsDigest`.
- Interactive edits use WS deltas on `/v1/events`, not full recompute:
  - client → `scene.patch` `{ "op": "scene.patch", sceneId, ops: [...] }`
  - server → `scene.patched` `{ sceneId, sceneVersion, applied }` or `scene.error` `{ detail }` (ack on the requester's connection) **and** the same outcome fanned out to connections that sent `{ "op": "subscribeScene", sceneId }` (requester excluded — no double delivery). `sceneVersion` is uint64 everywhere (run record, `paramsDigest`, wire).
- **v1 patch ops** (all-or-nothing per message; one `sceneVersion` bump per accepted patch):

  | op | fields | effect |
  |---|---|---|
  | `move` | `entityType`: `"source"`\|`"receiver"`, `id`, `position`: `[x,y,z]` | sets entity position |
  | `setPower` | `entityType`: `"source"`, `id`, `powerDb` | sets source power |

  Unknown `op`, unknown entity id, or malformed fields → `scene.error`, scene untouched. `rotate` and material patches are deferred (rejected as unknown ops in v1).
- Static geometry/materials uploaded once (REST); only movers stream over WS.
- Compute runs snapshot the scene at `sceneVersion` for reproducibility.

### artifacts (engine-defined)
Engines declare which artifact kinds they produce (`EngineDescriptor.artifacts`):
`ir` · `metrics` · `field` · `rays` · `findings` (optional analysis flags).
GUI renders only advertised kinds. Long jobs (minutes–hours, GPU/CPU) are first-class: progress, cancel, job timeouts.

### IO
- Import: **OBJ** (tinyobjloader), **glTF/GLB** (tinygltf) → `Mesh` + material slots. Dispatch by extension; glTF importer reads triangle primitives (POSITION required). Scale/unit flag (`meters` default, `mm/cm/in` convert on import).
- Export: scenario JSON (nlohmann), IR WAV/CSV, metrics JSON.
- Scenario (de)serialization ↔ `schemas/*.schema.json` — validate in tests and at API boundary.

### job / orchestration (`av_sim`)
- `JobManager`: thread pool (N = hw−1), `JobId`, states `queued → running → succeeded | failed | cancelled`.
- Snapshot `Scenario` at submit time (no shared mutable state).
- `ProgressFn` throttled to ≤10 Hz → WS broadcasts.
- Cancellation: atomic flag checked in engine inner loops; engines must poll.
- Sync mode: `runNow(scenario)` for CLI and tests (no queue).

---

## 3. API design (REST + WebSocket)

Base: `http://localhost:8787/v1` · WS: `ws://localhost:8787/v1/events` · JSON only · `Content-Type: application/json`.

### Resource model
`material` → referenced by `room` surfaces · `room` · `source` · `receiver` · belong to a `scenario` · `simulation` runs a scenario · `job` wraps an async simulation.

In-memory store v1 (std::unordered_map + mutex). Persistence (SQLite) is a later concern; document IDs as UUIDv4.

### REST endpoints

| Method | Path | Purpose |
|---|---|---|
| GET | `/v1/health` | liveness, version |
| GET | `/v1/engines` | `EngineDescriptor` list (id, caps, paramsSchema) |
| GET/POST | `/v1/materials` | list / create material |
| GET/PATCH/DELETE | `/v1/materials/{id}` | read / update / delete |
| GET/POST | `/v1/rooms` | list / create room (inline mesh or import ref) |
| GET/PATCH/DELETE | `/v1/rooms/{id}` | read / update / delete |
| POST | `/v1/rooms/import` | multipart: OBJ/glTF (+ options: scale, unit, name) → room |
| GET/POST | `/v1/sources` | list / create source (pos, orientation, power, АЧХ, directivity) |
| GET | `/v1/source-catalog` | built-in device catalog (specs as-is, no warranty) |
| GET | `/v1/runs` | run history (newest first; `?engine=` filter) |
| GET | `/v1/runs/{runId}` | one run + links to result/ir/field (P2e: `runId` = `jobId`) |
| GET/POST | `/v1/receivers` | list / create receiver (pos) |
| GET/POST | `/v1/scenarios` | list / create full scenario (refs + params) |
| GET/PATCH/DELETE | `/v1/scenarios/{id}` | read / update / delete |
| GET | `/v1/scenarios/{id}/validate` | dry-run: mesh/material/engine checks |
| POST | `/v1/simulations` | run scenario → `{jobId}` (async) or `?sync=1` → result |
| GET | `/v1/jobs/{id}/result` | result summary + metrics (P1: jobId = simulation handle) |
| GET | `/v1/jobs/{id}/ir.wav` | IR artifact (WAV binary) |
| GET | `/v1/jobs/{id}/field` | spatial field grid JSON |
| GET | `/v1/jobs/{id}` | job status/progress/error |
| POST | `/v1/jobs/{id}/cancel` | request cancellation |
| GET | `/v1/bands` | frequency band convention in use |

**Two modes:**
- **Interactive** — WS `scene.patch` / `scene.patched` for mover deltas; backend Scene Store is source of truth. No promise of full acoustic recompute per drag.
- **Compute** — `POST /simulations` → job (CPU/GPU, minutes–hours OK); WS `job.progress` / `job.succeeded`. Engines may require extra setup (mesh, grid) via `params.methodParams` + `paramsSchema`.

Errors: RFC7807-ish `{ "type", "title", "status", "detail", "fields"? }`.

### WebSocket `/v1/events`
Client sends: `{ "op": "subscribe", "jobId": "..." }` or `"jobId": "*"`; `{ "op": "subscribeScene", "sceneId": "..." }` registers for scene event fan-out (ack `scene.subscribed`).
Server events (JSON):

| event | payload |
|---|---|
| `scene.patch` | client→server: delta ops (v1: `move`, `setPower`) |
| `scene.patched` | server→client: ack + `sceneVersion`, `applied`; also fanned out to `subscribeScene` watchers of that scene |
| `scene.error` | server→client: patch rejected (`detail`); scene unchanged; also fanned out to `subscribeScene` watchers |
| `job.progress` | `jobId`, `fraction` 0..1, `stage` |
| `job.succeeded` | `jobId`, `simulationId`, `summary` (metrics) |
| `job.failed` | `jobId`, `error` |
| `job.cancelled` | `jobId` |
| `simulation.reflection` | optional P3+: batched ray/early-reflection paths for live 3D view |

REST polling (`GET /jobs/{id}`) is the fallback; WS is progressive enhancement.

**Extensibility for 3D GUI**: every result includes `paths` hook (`/simulations/{id}/paths?maxReflections=K` → list of polylines world-space). Empty in IMS v1 unless early-reflection enumeration is on (`simParams.capturePaths=true`).

---

## 4. Data model (scenario JSON sketch)

```json
{
  "schemaVersion": 1,
  "name": "demo-room",
  "room": {
    "type": "rect",                       // "rect" | "mesh"
    "size": { "w": 6.0, "h": 2.8, "d": 4.5 },
    "faces": { "floor": "mat-carpet", "ceiling": "mat-plaster",
               "x+": "mat-brick", "x-": "mat-brick", "z+": "mat-glass", "z-": "mat-brick" },
    "mesh": { "uri": "file:///.../room.glb", "unit": "m", "scale": 1.0,
              "materialOverrides": { "Wall_A": "mat-brick" } }
  },
  "materials": [{
    "id": "mat-carpet", "name": "Carpet on concrete",
    "bandsHz": [250, 500, 1000, 2000, 4000],
    "absorption": [0.10, 0.25, 0.35, 0.40, 0.50],
    "scattering": 0.1
  }],
  "sources": [{ "id": "src-1", "position": [1.2, 1.5, 1.0], "powerDb": 80.0 }],
  "receivers": [{ "id": "rcv-1", "position": [4.0, 1.5, 3.0] }],
  "params": {
    "engine": "image-source",            // "sabine-eyring" | "image-source" | "ray-tracing" | "hybrid"
    "maxReflectionOrder": 12,
    "sampleRate": 48000,
    "durationSec": 1.5,
    "bandsHz": [250, 500, 1000, 2000, 4000],
    "speedOfSound": 343.0,
    "capturePaths": true,
    "airAbsorption": false,
    "fieldGrid": { "enabled": true, "nx": 32, "ny": 16, "nz": 24 }
  }
}
```

Result sketch: `{ simulationId, jobId, metrics: { rt60: { "500": { schroederT30, sabine, eyring }, ... }, edt: {...}, earlyReflections: [{ t, amp, order, path? }] }, ir: { sampleRate, durationSec, channels, urls }, field: { nx, ny, nz, origin, cellSize, bandHz, unit, values } }`.

Canonical schemas live in `schemas/`; C++ structs mirror them 1:1; API validates against schema before enqueue.

---

## 5. Tech stack (coherent, Windows-first)

| Concern | Choice | Why |
|---|---|---|
| Build | **CMake ≥ 3.24** + `CMakePresets.json` | MSVC/VS2022 native, Ninja optional |
| Packages | **vcpkg manifest** (`vcpkg.json`) | official Windows story, reproducible |
| Lang | **C++20** | designated init, `span`, `ranges` sparingly |
| Linear algebra | **Eigen 3.4** (header-only) | vectors/transforms; no heavy dep |
| JSON | **nlohmann/json** | ubiquitous, easy |
| HTTP + WS | **Crow** | one lib, REST + WebSocket, Asio-based, simple routing |
| OBJ | **tinyobjloader** | single-purpose, widely used |
| glTF | **tinygltf** (+ stb) | covers GLB, materials |
| WAV | **dr_wav** (or 50-line custom float32 writer) | trivial IR export |
| CLI | **CLI11** | clean, header-friendly |
| Tests | **GoogleTest** | industry default |
| Schema check | **nlohmann-json-schema-validator** (optional P1) | validate at API edge |
| GUI (P3) | **Electron + Vue + Three.js** | voted stack; not a C++ concern |

Avoid Boost.Beast (build weight) and FDTD/GNU Octave-style deps. P0 engines are CPU; GPU engines are allowed later (job queue is long-run ready).

---

## 6. Build & test strategy

```
cmake --preset windows-msvc-x64-release
cmake --build --preset windows-msvc-x64-release
ctest --preset windows-msvc-x64-release
```

- **Warnings**: `/W4 /permissive- /WX` in CI-config; clang-tidy optional later.
- **Unit (must-pass gates)**
  - RT60: Sabine `T = 0.161 V / (S ᾱ)` and Eyring against hand-computed rooms; empty-room vs highly-absorbing bounds.
  - ISM: single-bounce delay/amplitude analytic check; image count for order N; rectangular shoebox vs Sabine trend (RT60 from IR ≈ analytic within tolerance); reciprocity source↔receiver.
  - Metrics: synthetic IR with known exponential decay → RT60 recovered; EDT on synthetic early-late split.
  - IO: OBJ/glTF round-trip vertex counts; scenario JSON golden files.
- **Integration**: spin `av-api` in-process (`AV_BUILD_API`) or subprocess; REST CRUD + one full simulation job + WS `job.succeeded` receipt.
- **Golden tests**: `tests/fixtures/golden/*.json` regenerated only via `tools/regen_goldens` and reviewed.
- **CI (later)**: GitHub Actions `windows-latest` matrix, ctest + schema validation.

---

## 7. Phased roadmap

| Phase | Scope | Exit criteria |
|---|---|---|
| **P0** Core lib + CLI | `av_core` geometry/materials, Sabine/Eyring, ISM rect rooms, metrics, scenario JSON, WAV/CSV export, `av-cli` | unit tests green; shoebox IR RT60 ≈ analytic; CLI produces IR+metrics from JSON |
| **P1** API server | Crow app, resources above, async jobs, WS progress, in-memory store, schema validation | integration tests green; Electron-ready contract frozen (`schemas/`) |
| **P2** Import + mesh rooms | OBJ/glTF import, MeshRoom validation, BVH, `RayTracingEngine` (early order / hybrid IMS-direct+RT), paths export | glTF room runs; ray-IR metrics sane vs shoebox benchmark |
| **P3** Electron+Vue+Three.js GUI | Базовый 3D-редактор, gizmos, каталог, `scene.patch`, кнопка «Расчёт», history | drag → WS-дельта; run → job |
| **P4** Research + catalog | External algorithms as engines, richer source cards | research engine без правок GUI |

GUI outline only (P3): main process talks REST; renderer subscribes WS; room editor = box/extrude primitives + OBJ/glTF upload; source panel = custom + catalog; heat-map field + reflection rays; run history. No custom mesh sculpting in v1.5.

---

## 8. Risks, open questions, non-goals

**Risks**
- ISM correctness is easy to get subtly wrong (1/r vs 1/r², energy vs pressure α) → golden tests + documented convention first.
- Arbitrary meshes break pure IMS (occlusion, non-convex) → engine capability flags + clear 400s; ray tracing is the real path (P2).
- Open/non-manifold meshes from DCC tools → import validator (non-fatal warnings, optional repair later).
- Crow/Asio + MSVC quirks (Winsock, UTF-8 paths) → pin versions via vcpkg, CI on windows-latest early in P1.
- Long simulations block thread pool → job timeouts + cancellation polling mandatory before P1 ships.
- Frequency-band mismatch between materials and metrics → one global band table in scenario, validated.
- Source catalog data quality/legal → specs as-is, explicit disclaimer; custom sources always work offline.
- Heavy jobs (hours, GPU) → robust job queue, heartbeat, cancel, partial artifacts; interactive path must stay responsive.

**Open questions**
- Which RT60 variant is "the" output: T30 Schroeder only, or always paired with Sabine/Eyring? (Recommend: all three, keyed by band.)
- Energy vs pressure IR (sign of reflections)? Default pressure-less energy envelope for metrics; optional signed IR flag.
- Multi-receiver / multi-source in one run: v1 yes (N×M channels) or sequential? (Recommend: yes, Cartesian product, channel naming `srcId:rcvId`.)
- Units: strictly SI meters; reject other units at API, convert only at import.

**Deliberate non-goals (v1)**
- No measurement / mic / phone / calibration pipeline (explicitly out of product scope).
- No FDTD / wave solvers, no diffraction (beyond scattering placeholder).
- No 60 FPS live auralization DSP (interactive = scene patches; compute = jobs, hours OK).
- No HRTF/binaural rendering (receivers are point probes).
- No multi-room coupling, no outdoors/terrain.
- No distributed compute (single-node job queue first). GPU engines OK when an engine needs them.
- No auth/multi-tenant (localhost tool).
- No heavy Study/diff — run history only.
- No GUI implementation in P0–P2 (contract only).

---

## Implementation order (P0 actionable list)

1. CMake superbuild + vcpkg manifest + GoogleTest harness.
2. `av_core`: `Vec3`, `Room` rect, `Material`/bands, scenario structs + JSON.
3. `SabineEyringEngine` + metrics from analytic → tests.
4. `ImageSourceEngine` + `IrBuffer` + RT60/EDT from IR → golden shoebox test.
5. `av_io`: scenario JSON r/w, WAV/CSV export.
6. `av-cli` end-to-end.
7. Freeze `schemas/`; start P1 (`av_sim` jobs, then `av-api`).
