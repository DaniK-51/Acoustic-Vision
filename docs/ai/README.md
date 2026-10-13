# AI agent documentation pack

English, code-grounded reference for AI agents working in the Acoustic-Vision
repository. Everything here describes **what is implemented today** in the
tree under `libs/`, `apps/`, `schemas/`, and `tests/`. Nothing is aspirational.

## How to use this pack

1. Read `architecture.md` first — layers, dependencies, Scene vs Scenario,
   coordinate frames, stores, and id namespaces.
2. When touching data shapes or wire formats, check `contracts.md` against
   `schemas/*.json` and the JSON helpers in `libs/io/`.
3. When touching acoustics, read `engines.md` **before** changing any formula.
   The ISM/RT conventions there are golden-test invariants.
4. For room-editor / mesher work use `mesh-scene.md`.
5. For HTTP/WS behavior use `api.md` — it mirrors `apps/api/routes_*.cpp`
   literally. Route shapes are frozen by `tests/integration/api_smoke.py`.
6. Before implementing anything, check `workflows.md` (how to add an engine,
   add a field, test, build-verify) and `gotchas.md` (known traps).
7. Project-level rules (what not to do, commit policy, sources of truth) stay
   in `AGENTS.md` at the repo root. This pack does not replace them.

## Files

| File | Contents |
|------|----------|
| `architecture.md` | Layers `av_core` / `av_io` / `av_sim` / apps, dependency rules, Scene vs Scenario, Z-up vs Y-up, in-memory stores, `meshjob-` vs `JobManager` |
| `contracts.md` | Scenario, Scene, Result, EngineDescriptor, MeshReport, and their wire JSON |
| `engines.md` | ISM/RT physics conventions, caps/artifacts, paramsSchema, `rt_core`, `powerDb` semantics |
| `mesh-scene.md` | Scene mesher, openings/voids, quality presets, to-scenario rules |
| `api.md` | Every `/v1` route and the WS protocol as implemented in `routes_*.cpp` |
| `workflows.md` | Add engine, add field, tests, build verify, commit policy |
| `gotchas.md` | `powerDb`, isotropic directivity, runId/jobId, CORS, `mesh.uri` CWD, `REGISTER_ENGINE`, FetchContent, and more |

## Grounding rules for agents editing these docs

- Cite the file you checked (`path:line` or at least the file).
- If code and docs disagree, code wins — then fix the docs in the same change.
- Do not invent endpoints, fields, flags, or engines. If it is not in
  `apps/api/routes_*.cpp`, `libs/`, or `schemas/`, it does not exist.
- Docs in this directory are **English only**. Human-facing docs under
  `docs/` and `docs-site/` stay Russian as before.

## Related documents (human-facing, Russian)

| Document | Role |
|----------|------|
| `AGENTS.md` | Agent role, fixed decisions, dev rules, commit policy |
| `ARCHITECTURE.md` | Module map, stack, phases |
| `docs/engines.md` | Engine API contract and ISM conventions (authoritative physics) |
| `docs/room-editor-mesh.md` | Scene editor + mesher design notes |
| `schemas/*.json` | Formal JSON contracts |
| `README.md` | Short human overview and build commands |
