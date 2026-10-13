# Plan V0.2 — редактор помещений + mesh

Детали: [`room-editor-mesh.md`](room-editor-mesh.md). Реализация субагентами, архитектор контролирует.

## Слайсы

### R6 — Scene model (core/io)
- [x] `SceneEntity` types: wall, solid_box, solid_extrude, void, source, receiver
- [x] `schemas/scene.schema.json`
- [x] `sceneFromJson` / `sceneToJson` (av_io)
- [x] unit tests round-trip + validation errors

### R7 — Primitives → SurfaceMesh
- [ ] wall slab (start/end/height/thickness) → quads→tris, zones inner/outer/side
- [ ] openings (door/window) clip wall face
- [ ] solid_box + void subtract (simple)
- [ ] entity→triangle map for picking

### R8 — Mesh settings + report + CLI
- [ ] `meshSettings.surface` presets draft/standard/fine
- [ ] `mesh-report.json` (stats, warnings, entityTriangleCount)
- [ ] `av-cli mesh <scene.json> -o out.obj --quality standard`
- [ ] volume mesh stub (hex-box)

### R9 — API
- [ ] GET/POST `/v1/scenes`, PATCH entities
- [ ] POST `/v1/scenes/{id}/mesh` (job)
- [ ] GET mesh.obj + mesh-report
- [ ] POST `/v1/scenes/{id}/to-scenario`

### R10 — Docs + ru-check
- [ ] README / idea / room-editor-mesh sync
- [ ] ru-check на русские тексты
- [ ] EN pass для ARCHITECTURE (technical)

## DoD V0.2
- [ ] scene.json редактируется валидно, роунд-трип
- [ ] wall+opening → OBJ, который грузит fillLoadedMesh
- [ ] mesh-report показывает warnings
- [ ] to-scenario → av-cli run / API submit
- [ ] ctest green; документы вычитаны
