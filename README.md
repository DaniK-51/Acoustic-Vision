# Acoustic-Vision

Open-source инструмент акустического анализа помещений: 3D-сцена → источники → алгоритмы → поле звука, отражения, артефакты.

**Стадия: P0–P3 backend готов** (engines, mesh, field, runs, catalog, scene.patch, REST/WS). GUI — следующий этап. Сборка: CMake presets `linux-debug` / `windows-msvc-x64-*`; `ctest` (~167 tests).

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
| Ядро | C++ engines + combinator, REST/WS |

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
| Один контракт результата | Любой engine → IR + metrics + field (+ paths) |
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

## Структура репозитория

```
libs/core     # geometry, materials, metrics, Engine API (+ ray-tracing, hybrid)
libs/io       # OBJ/glTF, scenario JSON, WAV, field.json, source catalog
libs/sim      # JobManager
apps/cli      # av-cli
apps/api      # av-api REST+WS, SceneStore, RunsStore
data/         # source_catalog.json
schemas/      # scenario, engine-descriptor, source-catalog
tests/        # unit + api_smoke + cli smoke + fixtures
gui/          # placeholder (Electron later)
```

## Roadmap

| Фаза | Поставка | Статус |
|------|----------|--------|
| **P0** | Engine API, Sabine/Eyring, ISM, metrics, CLI | done |
| **P1** | av-api REST/WS, JobManager, runs history | done |
| **P2** | mesh OBJ+glTF, Ray Tracing, hybrid, field | done |
| **P3 backend** | source catalog, Scene Store, scene.patch | done |
| **P2.5** | Dynamic plugin SDK (C ABI) | only on request |
| **P3 GUI** | Electron + Vue + Three.js | only on request |
| **P4** | Research-алгоритмы | later |

## Стек

C++20 · CMake ≥ 3.24 · nlohmann/json · GoogleTest · Crow+Asio · tinyobjloader · tinygltf · (GUI) Electron + Vue + Three.js

## Лицензия

TBD (open-source)
