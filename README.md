# Acoustic-Vision

Open-source инструмент акустического анализа помещений: 3D-сцена → источники → алгоритмы → поле звука, отражения, артефакты.

**Стадия: P0–P3 backend + V0.2 core редактора сцен/mesh** (engines, field, runs, catalog, scene.patch, `/v1/scenes`, gen-mesh, `av-cli mesh`). GUI — прототип (`gui/` GUI1–4 + V0.1.1); полный 3D-DCC и dynamic DLL — по запросу. Сборка: CMake presets `linux-debug` / `windows-msvc-x64-*`; `ctest` (~237 tests).

## Видение

Полноценный 3D-редактор помещения, источники (custom + каталог приборов), запуск разных алгоритмов расчёта — и **видимость** результата: куда и с какой силой приходит звук, где отражения, где артефакты.

| Характеристика | Решение |
|----------------|---------|
| Interactive | WS-дельты позиций; сцена целиком живёт на бэке |
| Compute | Кнопка «Расчёт»; job может идти часы (CPU/GPU) |
| GUI | Electron + Vue + Three.js; базовый 3D-редактор, полный DCC позже |
| Источники | Custom + catalog «как есть» (без гарантии качества спеков) |
| Замеры | Не делаем (без mic/phone pipeline) |
| Алгоритмы | Engine определяет артефакты: IR / field / rays / findings |
| Сравнение | История запусков (runs) |
| Ядро | C++ engines (+ combinator позже), REST/WS |

| Документ | Содержание |
|----------|------------|
| [`docs/idea.md`](docs/idea.md) | Видение, принципы, сценарии, roadmap |
| [`docs/engines.md`](docs/engines.md) | Engine API, как добавлять методы |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Модули, API, данные, стек, риски |
| [`AGENTS.md`](AGENTS.md) | Правила работы агентов в этом репо |
| [`schemas/`](schemas/) | JSON Schema — контракты Scenario / EngineDescriptor |

## Принципы

| Принцип | Смысл |
|---------|--------|
| Ядро отделено от UI | C++ считает, GUI — Electron+Vue+Three.js, REST/WS |
| Методы — плагины | Новый алгоритм не трогает API, GUI и чужие движки |
| Один контракт результата | Любой engine → Result (metrics / IR / field / rays — по caps engine) |
| Interactive first | WS-дельты сцены; тяжёлый расчёт — job |
| Open research | Алгоритмы из работ; golden-бенчи |
| Без measurement | Не наш pipeline |

## Быстрый старт

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug

build/linux-debug/apps/cli/av-cli engines
build/linux-debug/apps/cli/av-cli run tests/fixtures/demo-scenario.json -o out/
build/linux-debug/apps/cli/av-cli run tests/fixtures/box-scenario.json -o out-mesh/

build/linux-debug/apps/api/av-api --port 8787
# GET  /v1/health | /v1/engines | /v1/source-catalog | /v1/runs
# POST /v1/simulations   (body=scenario | {"sceneId"})  ?sync=1
# WS   /v1/events        job.* · scene.patch
```

Windows: `cmake --preset windows-msvc-x64-release` (vcpkg).

## Продуктовый цикл

1. Поднять API **из корня репозитория**: `build/linux-debug/apps/api/av-api --port 8787`.
2. Открыть GUI: `cd gui && npm run dev` (или `npm run build && npm start`).
3. Загрузить demo-сценарий (кнопка **Demo** → **Load**) — появляется `sceneId`. Для mesh-демо с полем и лучами — кнопка **Mesh demo (field)** (тянет `gui/public/demo/box-scenario.json`, `capturePaths: true`, fieldGrid включён). `mesh.uri` = `gui/public/demo/box.obj` и резолвится относительно CWD av-api — потому API и запускают из корня репозитория; иначе пропишите в uri абсолютный путь к `box.obj`.
4. Перетащить источник мышью в viewport (или ввести X/Y/Z, гизмо) — позиция уезжает в backend через WS `scene.patch`.
5. Нажать **Расчёт** — job, прогресс в статус-баре, результат в истории runs (метрики, `ir.wav`, поле и лучи по запросу engine).
6. В viewport: переключатели **Field** (heat-map, дБ относительно максимума сетки) и **Rays** (полилинии лучей из `result.rays`, цвет = амплитуда, не более 64 линий).

Повторять шаги 4–5 с разными алгоритмами — история runs сравнивает прогоны.

## Редактор сцен и mesh (V0.2)

```bash
av-cli gen-mesh box -o room.obj --w 6 --h 2.8 --d 4.5 --scenario room.json
av-cli mesh scene.json -o out.obj --report report.json --quality standard
# API: POST /v1/scenes · POST /v1/scenes/{id}/mesh · GET mesh.obj · POST to-scenario
```

Модель: [`docs/room-editor-mesh.md`](docs/room-editor-mesh.md) · план: [`docs/plan-v0.2.md`](docs/plan-v0.2.md).

## Структура репозитория

```
libs/core     # geometry, materials, metrics, scene model + mesher, Engine API (+ ray-tracing, hybrid)
libs/io       # OBJ/glTF, scenario/scene JSON, WAV, field.json, mesh-report, source catalog
libs/sim      # JobManager
apps/cli      # av-cli
apps/api      # av-api REST+WS, SceneStore, EditorSceneStore, RunsStore
data/         # source_catalog.json
schemas/      # scenario, scene, engine-descriptor, source-catalog
tests/        # unit + api_smoke + cli smoke + fixtures
gui/          # Electron + Vue + Three.js (P3, REST/WS /v1)
```

## Roadmap

| Фаза | Поставка | Статус |
|------|----------|--------|
| **P0** | Engine API, Sabine/Eyring, ISM, metrics, CLI | done |
| **P1** | av-api REST/WS, JobManager, runs history | done |
| **P2** | mesh OBJ+glTF, Ray Tracing, hybrid, field | done |
| **P3 backend** | source catalog, Scene Store, scene.patch | done |
| **V0.2** | Scene model (wall/solid/void), `av-cli mesh`, `/v1/scenes`, to-scenario | done |
| **P2.5** | Dynamic plugin SDK (C ABI) | only on request |
| **P3 GUI** | Electron + Vue + Three.js | прототип GUI1–4 + V0.1.1 (`gui/`); GUI-слайсы редактора — on request |
| **P4** | Research-алгоритмы | later |
| **P5** | Полноценный 3D-редактор (DCC) | later |

## Стек

C++20 · CMake ≥ 3.24 · nlohmann/json · GoogleTest · Crow+Asio · tinyobjloader · tinygltf · (GUI) Electron + Vue + Three.js

## Лицензия

TBD (open-source)
