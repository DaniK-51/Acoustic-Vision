# Workflows

How to extend the codebase without breaking contracts. Project rules (what
not to build, commit policy) remain in `AGENTS.md`; this file is the
operational checklist.

## 0. Ground rules before any code change

1. New scope goes to **docs first, then code** (AGENTS.md).
2. If you change a contract (JSON shape, Engine API fields, ISM conventions),
   update **all** affected docs + `schemas/` in the same change:
   `docs/ai/*`, `docs/engines.md`, `ARCHITECTURE.md`, `schemas/*.json`.
3. Never change ISM physics conventions silently (`engines.md` §1) — golden
   tests depend on them.
4. GUI slices and dynamic DLL (P2.5) are out of scope unless explicitly
   requested.

## 1. Add an engine

1. Implement `IEngine` in `libs/core/src/acoustics/<name>_engine.cpp` (or a
   new `engines/<name>/` module if it should live outside core).
2. Fill `descriptor()`:
   - `id` (registry key, stable), `name`, `version`;
   - `caps` honestly (`supportsRect` / `supportsMesh` / `supportsRayPaths` /
     `supportsBands` / `maxOrder`);
   - `paramsSchema` as JSON Schema with `"additionalProperties": false`;
   - `artifacts` — only what `run()` really produces
     (subset of `ir|metrics|field|rays|findings`).
3. Implement `validate()` with `ValidateResult` (errors/warnings, no
   exceptions). Reject unsupported room types and capability mismatches.
4. Implement `run()`:
   - poll `CancelToken` in any loop that can exceed ~100 ms;
   - report `Progress` via the callback;
   - follow `engines.md` §1 physics exactly;
   - return only the artifact kinds advertised.
5. Register with `REGISTER_ENGINE(MyEngine);` in the same TU and add a
   `void forceLinkMyEngine() {}` symbol referenced from
   `builtin_engines.cpp` (`linkBuiltinEngines`) — otherwise the static-library
   linker drops the TU (`gotchas.md`).
6. Tests in `tests/unit/`:
   - input/output smoke;
   - capability mismatch → `validate()` fails;
   - analytic case vs hand calculation / golden within tolerance, if the
   method has an analytic reference.
7. Fixture scenario under `tests/fixtures/` if needed.
8. Row in the engine table of `docs/engines.md` §4 and in `docs/ai/engines.md` §4.
9. **Do not** touch GUI/API — the contract is self-sufficient; the engine
   appears in `GET /v1/engines` automatically.

DoD checklist (AGENTS.md): builds, ctest green, golden within tolerance,
`validate()` honest, CancelToken polled, descriptor complete, docs not lying,
manual smoke `av-cli run tests/fixtures/demo-scenario.json`.

## 2. Add a Result field / artifact kind

Today `Result` is fixed: `metrics`, `ir`, `field`, `rays`, `findings`
(`engine.hpp`). Adding a **new artifact kind** is a contract change:

1. Extend `Result` + `EngineDescriptor::artifacts` vocabulary in
   `libs/core/include/av/acoustics/engine.hpp`.
2. Serialize in `resultToJson` / `writeResultJson` (`libs/io`) and, if it is a
   downloadable file, add the CLI writer and the `/v1/jobs/{id}/…` route.
3. Update `schemas/engine-descriptor.schema.json` (artifacts enum if it lists
   kinds), `docs/ai/contracts.md`, `docs/engines.md`, `ARCHITECTURE.md`.
4. Extend `tests/unit/test_io.cpp` and, for wire shapes,
   `tests/integration/api_smoke.py`.

Adding a **field inside an existing artifact** (e.g. a new metric) is lighter:
C++ struct + `metricsToJson` (or the relevant to-Json) + tests + doc table
rows. Keep band-key convention: stringified Hz, `"0"` = broadband.

Adding a **field to Scenario/SimParams** also touches `scenario_json.cpp`,
`schemas/scenario.schema.json`, the `paramsDigestOf` snapshot (if it should
distinguish runs), the `isParamsOverlayKey` allowlist in
`routes_simulations.cpp` (if scene-based submit may override it), and
`tests/unit/test_io.cpp` / `api_smoke.py`.

## 3. Tests

| Layer | Where | Runner |
|-------|-------|--------|
| Unit | `tests/unit/*.cpp` | GoogleTest via `ctest` |
| Integration | `tests/integration/api_smoke.py`, `cli_field_smoke.py` | Python3; wired in `tests/CMakeLists.txt` when Python3 is found |
| Fixtures | `tests/fixtures/*.json`, `tests/fixtures/scene.json` | used by unit + CLI smoke |

Conventions:

- Golden/analytic comparisons for engines with closed-form results.
- `api_smoke.py` freezes HTTP paths, JSON keys, and WS event names — if you
  change a wire shape, update the smoke in the same commit.
- New test files need a `tests/CMakeLists.txt` entry.
- Baseline is ≈237 tests; do not delete tests to go green.

## 4. Build and verify

```bash
# Linux (CI-ish)
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug

# manual smokes
build/linux-debug/apps/cli/av-cli engines
build/linux-debug/apps/cli/av-cli run tests/fixtures/demo-scenario.json -o out/
build/linux-debug/apps/cli/av-cli run tests/fixtures/box-scenario.json -o out-mesh/
build/linux-debug/apps/api/av-api --port 8787   # run from repo root (mesh CWD policy)
```

Windows: `cmake --preset windows-msvc-x64-release` (vcpkg). Warnings are
`/W4 /permissive-` (MSVC) or `-Wall -Wextra`; keep the tree clean.

Definition of Done for a feature/engine is in `AGENTS.md` (build, ctest,
golden, validate honesty, CancelToken, descriptor, docs, smoke).

## 5. Commit policy

**Commit and push only when the user explicitly asks** (AGENTS.md Git
section). When asked:

- Message: `feat|fix|docs|test|build|refactor(scope): …` — imperative mood,
  why > what.
- Never commit `build/`, `.mimocode/`, `.wav` artifacts, or editor/OS junk.
- Keep history clean; no drive-by refactors inside a focused commit.

## 6. Docs-only changes

- `docs/ai/*` is **English only** and must stay grounded in code (cite files).
- Human docs (`docs/*.md`, `docs-site/*`, `README.md`) stay Russian.
- After contract changes run the consistency checklist in `AGENTS.md`
  (roadmap sync, Engine API field parity, schemas cover documented fields,
  no references to non-existent code).
