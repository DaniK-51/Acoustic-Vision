# Contracts

C++ types and their wire JSON. Formal schemas live in `schemas/*.json`; the
serialization code is `libs/io/src/scenario_json.cpp`, `scene_json.cpp`,
`metrics_json.cpp`, `mesh_report.cpp`, and `apps/api/api_support.cpp`. When
schema and code disagree, code wins and the schema must be fixed.

## 1. Scenario

C++: `av::Scenario` in `libs/core/include/av/scenario/scenario.hpp`.
Schema: `schemas/scenario.schema.json`. IO: `scenarioToJson` / `scenarioFromJson`
(throw `std::runtime_error` on structural errors).

### Wire shape (top level)

```json
{
  "schemaVersion": 1,
  "name": "demo",
  "room": { "type": "rect" | "mesh", ... },
  "materials": [ { "id", "name"?, "bandsHz"?, "absorption", "scattering"? } ],
  "sources": [ { "id", "position": [x,y,z], "powerDb"?, "orientation"?, "directivity"? } ],
  "receivers": [ { "id", "position": [x,y,z] } ],
  "params": { ... }
}
```

Required by the parser: `room`, `materials`, `params` (and `params.engine`).
`schemaVersion` defaults to 1.

### Room

| `room.type` | Fields |
|-------------|--------|
| `rect` | `size: {w,h,d}` (>0), `faces: {floor, ceiling, x+, x-, z+, z-}` → material ids |
| `mesh` | `mesh: {uri, unit ("m"\|"mm"\|"cm"\|"in"), scale, materialOverrides}` |

`unit`/`scale` convert to meters at import (SI-only runtime). `materialOverrides`
maps mesh slot names → scenario material ids (applied by
`applyMaterialOverrides`).

### Materials

`id` + `absorption` (array of α in [0,1] per `bandsHz` entry) required;
`scattering` optional in [0,1]. `meanAbsorption` is the arithmetic mean across
bands. Lookup is by id (`findMaterial`).

### Sources

| Field | Type | Meaning |
|-------|------|---------|
| `id` | string | unique |
| `position` | `[x,y,z]` | meters, engine-frame |
| `powerDb` | number, default 0 | **power level in dB**; engines convert `energy = 10^(powerDb/10)` (see `engines.md`) |
| `orientation` | `{yawDeg, pitchDeg}`, default 0/0 | additive aim; 0 = facing +Z |
| `directivity` | object (optional) | `type` (`omni`\|`dipole`\|`cardioid`\|`custom`), optional `horizontal`/`vertical` `PolarCurve`s. Absent = omnidirectional |

PolarCurve: `{freqsHz[], anglesDeg[], gainDb[][]}` with `gainDb[i]` sampled at
`anglesDeg` for `freqsHz[i]`; curves are **relative** (0 dB = on-axis).

### SimParams (`params` object)

| Key | Type | Default | Notes |
|-----|------|---------|-------|
| `engine` | string | — (required) | registry id |
| `maxReflectionOrder` | int | 0 | ISM order; RT falls back to it when `methodParams.maxBounces` is absent |
| `sampleRate` | int | 48000 | Hz |
| `durationSec` | number | 1.0 | IR length |
| `bandsHz` | number[] | empty → `kDefaultBandsHz` = 125…4000 (6 octaves) | |
| `speedOfSound` | number | 343.0 | m/s |
| `capturePaths` | bool | false | ray polylines when engine supports them |
| `airAbsorption` | bool | false | stored and reported in run digest; **no engine applies it today** |
| `signedIr` | bool | false | engines **warn and ignore** — output is an unsigned energy envelope |
| `methodParams` | object | `{}` | engine-specific, validated against `paramsSchema` |
| `fieldGrid` | object | off | `{enabled, nx, ny, nz}`; dims clamped to `kMaxFieldDim = 64` |

### Runtime-only field

`Scenario::loadedMesh` (`shared_ptr<const Mesh>`) is **not serialized**. The
CLI/API boundary fills it from `room.mesh.uri` via `fillLoadedMesh` before
submit/run. Engines see the mesh only through this field.

## 2. Scene (room-editor document)

C++: `av::Scene` in `libs/core/include/av/scene/scene_model.hpp`.
Schema: `schemas/scene.schema.json`. IO: `sceneToJson` / `sceneFromJson`.
Units are **meters only**. Semantic checks: `validateScene` (positive sizes,
unique entity ids, material refs resolve, openings fit their wall).

```json
{
  "schemaVersion": 1,
  "id": "scene-1",
  "units": "m",
  "materials": [ { "id", "name"?, "bandsHz"?, "absorption", "scattering"? } ],
  "entities": [ /* geometry + placements */ ],
  "meshSettings": { "surface": {...}, "volume": {...} },
  "sources": [ ... ],
  "receivers": [ ... ]
}
```

Sources/receivers serialize into top-level `sources`/`receivers` arrays;
geometry stays in `entities`. Entity discriminator strings: `wall`,
`solid_box`, `solid_extrude`, `void`, `source`, `receiver`.

### Entity shapes (C++)

| Entity | Fields |
|--------|--------|
| `Wall` | `id,name,start,end,height,thickness,faces{inner,outer,side},openings[]` |
| `WallOpening` | `type` (door/window), `offset`, `width`, `height`, `sill` |
| `SolidBox` | `id,name,origin,size,materialId` |
| `SolidExtrude` | `id,name,polygon[] (Vec2 in XY),height,materialId` |
| `Void` | `id,name,origin,size` (subtractive volume) |
| `SourceRef` | `id,name,position,powerDb` |
| `ReceiverRef` | `id,name,position` |

### MeshSettings

```json
{
  "surface": { "algorithm": "auto"|"earclip"|"advancing-front",
               "targetEdge"?, "maxEdge"?, "quality": "draft"|"standard"|"fine" },
  "volume":  { "algorithm": "tet"|"hex-box", "targetSize": 0.15, "enabled": false }
}
```

Quality presets (see `mesh-scene.md`): draft 0.5/1.0, standard 0.2/0.5,
fine 0.08/0.2 (targetEdge/maxEdge). Explicit `targetEdge`/`maxEdge` win over
the preset per field.

## 3. Result

C++: `av::Result` in `libs/core/include/av/acoustics/engine.hpp`:

```cpp
struct Result {
  std::optional<Metrics> metrics;
  std::optional<IrBuffer> ir;
  std::optional<SpatialField> field;
  std::optional<RayPaths> rays;
  std::optional<Findings> findings;  // empty struct today; no engine fills it
};
```

Populated kinds follow the engine's `artifacts` list.

### Metrics

```json
{
  "rt60": {
    "125": { "schroederT30": 0.0, "sabine": 1.2, "eyring": 1.1 },
    "0":   { ... }
  },
  "edt": { "0": 0.05 },
  "earlyReflections": [ { "t": 0.012, "amp": 0.4, "order": 1 } ]
}
```

Band keys are band center Hz as strings; **key `"0"` is broadband** (derived
from a broadband IR). `schroederT30` is 0 when there is no IR (analytic
engines fill only `sabine`/`eyring`).

### IR (`IrBuffer`)

C++ holds planar samples `[channel][sampleCount]`, **unsigned energy
envelope** (`energy = amplitude²`), `bandsHz` empty ⇒ broadband,
`channelNames` = `"srcId:rcvId"` per channel.

Wire (`resultToJson`) is a **summary**, not the samples:

```json
"ir": { "sampleRate": 48000, "durationSec": 1.0, "channels": 2,
        "channelNames": ["s1:r1", "s1:r2"], "bandsHz": [] }
```

Samples travel as IEEE float32 WAV (`writeWav` / `encodeWav`, format tag 3).

### Spatial field

```json
"field": {
  "nx": 16, "ny": 16, "nz": 16,
  "origin": [x, y, z],
  "cellSize": 0.25,
  "bandHz": 0.0,
  "unit": "energy",
  "values": [ ... nx*ny*nz floats ... ]
}
```

`values` are **linear energy density**, not dB. Index order is x-fastest:
`i = x + nx*(y + ny*z)`. Grid dims clamp to `kMaxFieldDim = 64` per axis.

### Rays

```json
"rays": {
  "paths": [
    { "points": [[x,y,z], ...], "amplitude": 0.3, "order": 2 }
  ]
}
```

`amplitude` is the bounce-factor amplitude (product of `√(1-α)`, **without**
geometric 1/r). `order` is wall-hit count. Key present when `Result.rays` is
set (possibly empty `paths` when capture was off).

## 4. EngineDescriptor

C++: `av::EngineDescriptor` (`engine.hpp`). Wire via `descriptorToJson`
(matches `schemas/engine-descriptor.schema.json`), served by `GET /v1/engines`:

```json
{
  "id": "ray-tracing",
  "name": "Stochastic Ray Tracing",
  "version": "0.1.0",
  "caps": {
    "supportsRect": false,
    "supportsMesh": true,
    "supportsRayPaths": true,
    "supportsBands": true,
    "maxOrder": 0
  },
  "paramsSchema": { "type": "object", "properties": { ... }, "additionalProperties": false },
  "artifacts": ["metrics", "ir", "rays", "field"]
}
```

`artifacts` ⊆ `{ir, metrics, field, rays, findings}`. `paramsSchema` is a JSON
Schema for `params.methodParams`; null becomes `{}` on the wire.

## 5. MeshReport

C++: `av::MeshReport` (`libs/io/include/av/io/io.hpp`). Wire via
`meshReportToJson`:

```json
{
  "kind": "surface",
  "stats": { "vertices": 24, "triangles": 36, "zones": ["floor", "ceiling"] },
  "bbox": [[minX, minY, minZ], [maxX, maxY, maxZ]],
  "warnings": ["opening 'door-1' leaves slab caps uncut"],
  "settingsUsed": { "algorithm": "auto", "targetEdge": 0.5, "maxEdge": 1.0 },
  "entityTriangleCount": { "wall-1": 12 }
}
```

`kind` is `"surface"` or `"volume"`. `zones` are material-slot names present
on the mesh. `entityTriangleCount` maps entity id → contributed triangles
(absent for entities that produced no geometry).

## 6. Job / run / problem wire shapes

Job status (`jobStatusToJson`):

```json
{ "id": "job-…", "state": "queued|running|succeeded|failed|cancelled",
  "progress": { "fraction": 0.5, "stage": "ray-tracing" },
  "error": "" }
```

Run summary (`runSummaryJson`) and detail (`runToJson`):

```json
{
  "id": "run-…",          // history key
  "jobId": "job-…",       // artifact handle
  "engine": "ray-tracing",
  "scenarioName": "demo",
  "sceneVersion": 3,      // only when submitted via {"sceneId"}
  "state": "succeeded",
  "submittedAt": "2026-01-01T00:00:00.000Z",
  "finishedAt": "2026-01-01T00:00:01.500Z",
  "paramsDigest": { "engine": "...", "maxReflectionOrder": 10, ... },
  "links": {                // detail only
    "result": "/v1/jobs/job-…/result",
    "ir": "/v1/jobs/job-…/ir.wav",
    "field": "/v1/jobs/job-…/field"
  }
}
```

Errors are RFC7807-ish (`problemJson`):

```json
{ "type": "about:blank", "title": "invalid scenario", "status": 400, "detail": "..." }
```
