# Mesh and scene-to-scenario

Room-editor mesher and Scene → Scenario compile. Sources:
`libs/core/include/av/scene/scene_mesher.hpp`, `scene_model.hpp`,
`libs/core/src/scene/scene_mesher.cpp`, `libs/io/src/scene_to_scenario.cpp`,
`libs/io/src/mesh_report.cpp`, `docs/room-editor-mesh.md`.

## 1. Scene mesher (generateSurfaceMesh)

Input: `av::Scene` + `MeshSettings`. Output: `SurfaceMeshResult { mesh,
entityTriangleCount, warnings }`. Triangles only; `materialSlots` hold
**material ids** (from `faces.inner` / `faces.outer` / `faces.side` /
`materialId`), so ray-tracing resolves them via `materialIdForSlot` without a
zone table. Empty ids fall back to face-kind names (`inner`/`outer`/`side`/
`solid`).

v1 is explicit topology: quads → triangles, **no CSG boolean, no edge
refinement**. Degenerate entities are skipped with warnings, never thrown —
call `validateScene()` first for strict checks.

### Entity geometry

| Entity | Output |
|--------|--------|
| `Wall` | slab along `start→end`, height along **+Z**, thickness extruded to the **right** of `start→end` (viewed from +Z). `start`/`end` is the **inner-face bottom edge**: inner face on the baseline, body outward, so a CCW wall loop bounds the room at exact baseline dimensions. Without openings: 6 quads / 12 triangles. |
| `Void` | box subtracted from wall faces it overlaps (CSG-lite). Footprint on a face becomes a simple hole **only where the void penetrates the slab** (through thickness). Zero-volume contact does not cut. Solids (`SolidBox`/`SolidExtrude`) ignore voids — one warning per void. |
| `SolidBox` | 8 vertices / 12 triangles, all faces `materialId`. |
| `SolidExtrude` | prism over XY polygon (base z=0, height +Z): side quads + ear-clipped caps. CW polygons reversed to CCW first. |
| `SourceRef` / `ReceiverRef` | no geometry. |

### Openings (doors / windows)

`WallOpening {type, offset, width, height, sill}`: rectangular holes cut in
the inner and outer faces via a **grid split** around each hole (robust for
several openings). Limitations of v1 (each emits a warning):

- reveal/jamb faces are **not** generated;
- the four slab caps are **not** cut — a `sill = 0` door leaves the bottom
  cap running under the hole.

### Winding / normals

Every face is CCW seen from the **air side**, so `faceNormal()` points out of
the solid material and toward adjacent air: wall inner face into the room,
outer face away, caps out of the slab, solid faces outward. A room shell of
CCW wall loops therefore matches `generateBoxMesh` floors.

### Settings actually honored

`algorithm` selects ear-clip vs advancing-front for extrude caps;
`AdvancingFront` falls back with a warning. **`targetEdge` / `maxEdge` /
`quality` are NOT applied in v1** (explicit topology only) — they still appear
in `MeshReport.settingsUsed` for the GUI.

## 2. Volume mesh (stub)

`generateVolumeMesh` is a **stub**, not a real volume mesher. With
`algorithm = "hex-box"` it emits one hex cell per `SolidBox` (8 corner nodes,
zone = materialId); everything else is skipped with a warning and
`targetSize` is ignored. `algorithm = "tet"` produces no cells and warns.
Callers gate this on `meshSettings.volume.enabled` (default false).

## 3. Quality presets

`resolveSurfaceMeshSettings` expands `surface.quality` into concrete edges;
an explicitly set `targetEdge`/`maxEdge` in `MeshSettings::Surface` wins for
that field, the preset fills the rest.

| quality | targetEdge | maxEdge |
|---------|------------|---------|
| `draft` (default) | 0.5 | 1.0 |
| `standard` | 0.2 | 0.5 |
| `fine` | 0.08 | 0.2 |

`av-cli mesh --quality draft|standard|fine` is a **full density override** —
it replaces the scene's density settings entirely.

## 4. MeshReport

Built by `meshReportFrom(SurfaceMeshResult, ResolvedSurfaceMeshSettings)`:
`kind` ("surface"|"volume"), `stats` (vertices, triangles, zones),
`bbox` `[[min],[max]]`, `warnings`, `settingsUsed`, `entityTriangleCount`.
Wire shape in `contracts.md` §5. CLI writes it via `--report`; API returns it
from `GET /v1/scenes/{id}/mesh`.

## 5. to-scenario (scenarioFromScene)

`av_io::scenarioFromScene(scene, meshPath)` compiles a Scene into a Scenario
(`libs/io/src/scene_to_scenario.cpp`). Materials and sources/receivers are
copied; `meshSettings` are a mesher input and never land in the scenario.
Two mutually exclusive shapes:

### Rect branch (closed shoebox)

Detected when the scene is exactly:

- 4 axis-aligned `Wall`s: equal height, baselines on z=0 forming one
  rectangle, **no openings**;
- floor and ceiling `SolidBox` slabs whose XY footprint is exactly that
  rectangle and which touch z=0 / z=height (no full-height solids);
- nothing else geometric (no void / extrude). Placement entities are ignored
  in the shell test.

Exact match only — tolerance `kBoxEps = 1e-9 m`. "Almost a box" falls back to
the mesh branch rather than guessing dimensions.

Result:

- `room.type = "rect"`, `w/h/d` from the box;
- wall `faces.inner` map to `x+ / x- / z+ / z-` by wall position;
  slab `materialId`s map to `floor` / `ceiling`;
- **frame swap** (Scene Z-up → RectRoom Y-up): positions become
  `(x − minX, z, y − minY)` so the room min corner is the origin;
- default `params.engine = "image-source"`, `maxReflectionOrder = 10`.

### Mesh branch (anything else)

- `generateSurfaceMesh(scene, scene.meshSettings)` is written to `meshPath`
  (**required**; throws `std::runtime_error` when empty);
- `room.type = "mesh"`, `room.mesh.uri = meshPath`, `unit = "m"`,
  `scale = 1.0`, identity `materialOverrides` (mesh slots already carry scene
  material ids);
- positions keep the **scene Z-up frame**;
- default `params.engine = "ray-tracing"`.

The generated mesh is **not** attached to the returned scenario's
`loadedMesh` — disk loading stays at the CLI/API boundary via
`fillLoadedMesh`. The API's `POST /v1/scenes/{id}/to-scenario` writes the OBJ
to a temp file and returns an absolute `uri` so the scenario is immediately
submittable (av-api resolves relative mesh uris against its CWD).

## 6. Mesh loading side (for completeness)

| Helper | Role |
|--------|------|
| `loadObj` / `loadGltf` / `loadMesh` | import; `unit` in m/mm/cm/in converted to meters, times `scale`; triangles only |
| `applyMaterialOverrides` | rewrite mesh `materialSlots` from `mesh.materialOverrides` |
| `loadMeshRoom` | `MeshRoom` fields → `Mesh` (strips `file://`) |
| `fillLoadedMesh` | fills `scenario.loadedMesh` from `room.mesh.uri`; idempotent; baseDir: CLI passes scenario parent dir, API leaves empty (**CWD**). Throws when the mesh cannot load or has no geometry — fail the run rather than let engines see an empty shell |
| `saveObj` / `encodeObj` | v / g / usemtl / f, one `g`+`usemtl` per material slot |
