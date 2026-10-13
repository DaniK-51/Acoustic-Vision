# HTTP + WebSocket API (`/v1`)

Literal mirror of `apps/api/routes_*.cpp`. Wire shapes are frozen by
`tests/integration/api_smoke.py` — move routes only, no redesign. Errors use
the problem body `{type, title, status, detail}`. Success bodies are JSON
unless noted. Default bind `127.0.0.1:8787` (`--port`, `--bind`).

**No CORS headers are sent.** The Electron GUI injects them in
`onHeadersReceived` and posts JSON as `text/plain` to skip preflight. Plain
browser `fetch` to av-api will not work — desktop contract by design.

Query flag: `?sync=1` (or `sync=true`) on the two job-creating POSTs runs the
work synchronously and returns the finished payload instead of `202`.

---

## Health / catalog (routes_health_engines.cpp)

### `GET /v1/health`
`200` → `{ "status": "ok", "version": AV_VERSION, "engines": ["sabine-eyring", ...] }`

### `GET /v1/engines`
`200` → array of EngineDescriptor JSON (`contracts.md` §4): `id`, `name`,
`version`, `caps`, `paramsSchema`, `artifacts`.

### `GET /v1/bands`
`200` → `{ "bandsHz": [125, 250, 500, 1000, 2000, 4000] }` (`kDefaultBandsHz`).

### `GET /v1/source-catalog`
`200` → built-in device catalog JSON (always 200; empty list when no catalog
file was loaded). Read-only; loaded at startup from `$AV_SOURCE_CATALOG` or
`exeDir/source_catalog.json`.

---

## Scenario Store (routes_scenes.cpp) — interactive mode

In-memory `SceneStore` holding **Scenario** snapshots (not editor Scenes).

### `POST /v1/scenarios`
Body: full Scenario JSON. `201` → `{ "sceneId", "sceneVersion": 1 }`.
`400` on invalid JSON / invalid scenario.

### `GET /v1/scenarios/{sceneId}`
`200` → `{ "sceneId", "sceneVersion", "scenario": {...} }`; `404` problem.

---

## Simulations (routes_simulations.cpp)

### `POST /v1/simulations[?sync=1]`

Two body shapes:

1. **Full scenario** (no `sceneId`): Scenario JSON as-is. `params.engine`
   selects the engine.
2. **Scene-based**: `{ "sceneId", "engine"?, "methodParams"?, "params"? }` —
   the stored snapshot (with its `sceneVersion`) is the run's scenario; the
   optional keys overlay `SimParams` before validate/submit. Top-level
   `engine` / `methodParams` are shorthands for the same keys inside `params`.
   Accepted overlay keys only: `engine`, `methodParams`, `maxReflectionOrder`,
   `sampleRate`, `durationSec`, `bandsHz`, `speedOfSound`, `capturePaths`,
   `airAbsorption`, `signedIr`, `fieldGrid`. Any other key with `sceneId` →
   `400`.

Pipeline before accept: parse → `fillLoadedMesh` (mesh uri resolves against
**process CWD**; failure → `400 "mesh load failed"`) → engine lookup
(`400 "unknown engine"`) → `engine->validate` (`400 "scenario validation
failed"` with joined errors) → `jobs.submit` + `runs.noteSubmitted`.

| Response | Body |
|----------|------|
| `202` (async, default) | `{ "jobId": "job-…" }` |
| `200` (`sync=1`, success) | `resultToJson(Result)` + `jobId` + `state: "succeeded"` |
| `500` (`sync=1`, failure) | problem `"simulation failed"` |
| `400` / `404` | problem as above |

---

## Jobs (routes_jobs_runs.cpp)

All job ids are `job-…` (JobManager).

| Method | Path | Success | Notes |
|--------|------|---------|-------|
| GET | `/v1/jobs/{id}` | `200` job status `{id, state, progress:{fraction,stage}, error}` | `404` unknown |
| POST | `/v1/jobs/{id}/cancel` | `200` job status | `409` if already terminal ("job already finished") |
| GET | `/v1/jobs/{id}/result` | `200` `resultToJson` | `409` unless `state == succeeded` (`"result not available"`, detail carries state/error) |
| GET | `/v1/jobs/{id}/ir.wav` | `200` `audio/wav` bytes | `404` if no IR artifact |
| GET | `/v1/jobs/{id}/field` | `200` field JSON | `404` if no field artifact |

## Runs history (routes_jobs_runs.cpp)

| Method | Path | Success | Notes |
|--------|------|---------|-------|
| GET | `/v1/runs?engine={id}` | `200` array of run summaries | optional `engine` filter; newest first; reconciles terminal jobs first |
| GET | `/v1/runs/{id}` | `200` run detail + `links` | `{id}` may be **runId (`run-…`) or jobId (`job-…`)**; `404` otherwise |

Summary item: `{id, jobId, engine, scenarioName, sceneVersion?, state,
submittedAt, finishedAt, paramsDigest}` — see `contracts.md` §6.

---

## Room-editor Scenes (routes_editor_scenes.cpp)

Separate id space and store (`EditorSceneStore`) from `/v1/scenarios`.
Documents are **Scene** (`schemas/scene.schema.json`). All in-memory.

### `POST /v1/scenes`
Body: Scene JSON. Parsed + `validateScene`. `201` →
`{ "sceneId", "sceneVersion": 1 }`; `400` on invalid JSON / scene / validation.

### `GET /v1/scenes/{sceneId}`
`200` → `{ "sceneId", "sceneVersion", "scene": {...} }`; `404`.

### `PUT /v1/scenes/{sceneId}` and `PATCH /v1/scenes/{sceneId}`
**Both are full-document replace** (v1 semantics): body is the complete Scene;
`200` → `{ "sceneId", "sceneVersion": <bumped> }`. Replace drops any
previously generated mesh/report. `404` unknown id; `400` invalid body.

### `POST /v1/scenes/{sceneId}/mesh[?sync=1]`
Validates the stored scene, mints a `meshjob-…` id, runs surface meshing
inline (`sync=1`) or on a detached worker (default).

| Response | Body |
|----------|------|
| `202` (async) | `{ "sceneId", "jobId": "meshjob-…", "state": "running" }` |
| `200` (`sync=1`) | mesh status object (below) |
| `400` | scene validation failed |
| `404` | scene not found |

### `GET /v1/scenes/{sceneId}/mesh`
Mesh status object:

```json
{
  "sceneId": "...", "sceneVersion": 3,
  "jobId": "meshjob-…", "state": "queued|running|succeeded|failed|cancelled",
  "error": "...",              // only when failed
  "report": { MeshReport }     // only when a generation succeeded
}
```

`404 "mesh not generated"` when `POST .../mesh` has never been called.

### `GET /v1/scenes/{sceneId}/mesh.obj`
`200` `model/obj` text (Wavefront OBJ, one `g`+`usemtl` per material slot).
`404` when mesh not generated.

### `POST /v1/scenes/{sceneId}/to-scenario`
Compiles Scene → Scenario (`mesh-scene.md` §5). `200` → full Scenario JSON
(mesh branch writes a temp OBJ and returns an **absolute** `room.mesh.uri`).
`400` scene validation failed; `500` `"to-scenario failed"`.

---

## WebSocket `/v1/events` (routes_events.cpp)

One endpoint for job events **and** interactive scene patches. Text frames
only (binary ignored). Malformed JSON and unknown `op` values are **silently
ignored** — REST stays authoritative.

### Client → server ops

| op | Payload | Effect |
|----|---------|--------|
| `subscribe` | `{"op":"subscribe","jobId":"job-…"}` | subscribe to that job; `jobId: "*"` = all jobs; empty id ignored |
| `subscribeScene` | `{"op":"subscribeScene","sceneId":"…"}` | subscribe to scene fan-out; ack `{"event":"scene.subscribed","sceneId"}` |
| `scene.patch` | `{"op":"scene.patch","sceneId":"…","ops":[…]}` | apply mover deltas (below) |

### scene.patch ops

Each op: `{"op":"move"|"setPower", "entityType":"source"|"receiver", "id":"…", ...}`

- `move`: `"position": [x,y,z]` (also accepts the ScenePatchOp fields used by
  `parsePatchOps` — see `apps/api/scene_store.cpp`).
- `setPower`: `"powerDb": <number>`; **source only**.
- Unknown op kinds are errors (the whole patch is rejected).

Patch semantics: **all-or-nothing** (`SceneStore::patch`) — every op is
validated against the stored scene first; success bumps `sceneVersion` once
and reports `applied = ops.size()`. Rotate is intentionally not in v1.

Ack on the requester connection:

- success → `{"event":"scene.patched","sceneId","sceneVersion","applied"}`
- failure → `{"event":"scene.error","detail":"..."}`

Fan-out: every connection that `subscribeScene`d the same sceneId receives the
same outcome (`scene.patched` / `scene.error`), **excluding** the requester
(no double delivery).

### Server → client events (job.*)

Published by `JobWatcher` (100 ms poll) to `subscribe`rs matching the jobId
(or `*`):

| event | extra fields |
|-------|--------------|
| `job.progress` | `fraction`, `stage` (non-terminal only, on change) |
| `job.succeeded` | `simulationId` (= jobId), `summary` = metrics JSON (or `{}`) |
| `job.failed` | `error` |
| `job.cancelled` | — |

Every payload also carries `event` and `jobId`. Scene fan-out payloads carry
`event` and `sceneId`.

---

## Quick reference

```
GET    /v1/health
GET    /v1/engines
GET    /v1/bands
GET    /v1/source-catalog

POST   /v1/scenarios
GET    /v1/scenarios/{sceneId}

POST   /v1/simulations[?sync=1]

GET    /v1/jobs/{jobId}
POST   /v1/jobs/{jobId}/cancel
GET    /v1/jobs/{jobId}/result
GET    /v1/jobs/{jobId}/ir.wav
GET    /v1/jobs/{jobId}/field

GET    /v1/runs[?engine=]
GET    /v1/runs/{runId|jobId}

POST   /v1/scenes
GET    /v1/scenes/{sceneId}
PUT    /v1/scenes/{sceneId}          # full replace
PATCH  /v1/scenes/{sceneId}          # full replace (same handler)
POST   /v1/scenes/{sceneId}/mesh[?sync=1]
GET    /v1/scenes/{sceneId}/mesh
GET    /v1/scenes/{sceneId}/mesh.obj
POST   /v1/scenes/{sceneId}/to-scenario

WS     /v1/events                    # subscribe | subscribeScene | scene.patch
```
