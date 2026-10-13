# Plan V0.2 — редактор помещений + mesh

Детали: [`room-editor-mesh.md`](room-editor-mesh.md). Реализация субагентами, архитектор контролирует.

## Слайсы

### R6 — Scene model (core/io)
- [x] `SceneEntity` types: wall, solid_box, solid_extrude, void, source, receiver
- [x] `schemas/scene.schema.json`
- [x] `sceneFromJson` / `sceneToJson` (av_io)
- [x] unit tests round-trip + validation errors

### R7 — Primitives → SurfaceMesh
- [x] wall slab (start/end/height/thickness) → quads→tris, zones inner/outer/side
- [x] openings (door/window) clip wall face (grid split; jamb-граней нет — warning)
- [x] solid_box + void subtract (simple: void режет грани стен, solids игнорирует)
- [x] entity→triangle map for picking

### R8 — Mesh settings + report + CLI
- [x] `meshSettings.surface` presets draft/standard/fine
- [x] `mesh-report.json` (stats, warnings, entityTriangleCount)
- [x] `av-cli mesh <scene.json> -o out.obj --quality standard`
- [x] volume mesh stub (hex-box)

### R9 — API
- [x] GET/POST `/v1/scenes`, PATCH entities (v1: PUT/PATCH full document)
- [x] POST `/v1/scenes/{id}/mesh` (job)
- [x] GET mesh.obj + mesh-report
- [x] POST `/v1/scenes/{id}/to-scenario`

### R10 — Docs + ru-check
- [x] README / idea / room-editor-mesh sync
- [x] RU typography pass («», —, – в диапазонах)
- [x] EN pass ARCHITECTURE (Scene vs Scenario)

## DoD V0.2
- [x] scene.json редактируется валидно, роунд-трип
- [x] wall+opening → OBJ, который грузит fillLoadedMesh
- [x] mesh-report показывает warnings
- [x] to-scenario → av-cli run / API submit
- [x] ctest green (237); документы вычитаны

## Residuals (осознанно не в V0.2)

| Что | Статус | Куда |
|-----|--------|------|
| `targetEdge` / `maxEdge` реально измельчают топологию (advancing front / Delaunay) | settings резолвятся в `settingsUsed`, mesh — явные quads/tris | v1.1 |
| Jamb/reveal-грани проёмов; кап под `sill=0` дверью | grid split + warning | дальше |
| Void-вычитание из solids (не только граней стен) | limitation + warning per void | дальше |
| Volume mesh: tets (TetGen/Netgen-подобное), `targetSize` | hex-box stub (1 ячейка на `solid_box`) | дальше |
| WS entity-ops `moveEntity` / `resizeEntity` / `setFaceMaterial` / `addOpening` | v1: PUT/PATCH всего документа | GUI-слайс |
| Outline-полигон стены; `transform`/`visible`/`locked`/`tags` | не в schema v1 | GUI-слайс |
| GET списка `/v1/scenes`, persistence `EditorSceneStore` | in-memory, create/get/replace | дальше |
| GUI Mesh panel (read-only) | не в scope R10 (docs-only) | GUI-слайс |
