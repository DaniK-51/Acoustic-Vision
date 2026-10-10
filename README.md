# Acoustic-Vision

Open-source инструмент акустического анализа помещений: 3D-сцена → источники → алгоритмы → поле звука, отражения, артефакты.

**Стадия проекта: архитектура.** Реализации кода пока нет — дизайн-документы и JSON Schema контрактов.

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

## Целевая структура репозитория

```
docs/         # idea.md, engines.md          — есть
schemas/      # scenario, engine-descriptor  — есть (контракты)
ARCHITECTURE.md, README.md, AGENTS.md       — есть

# --- ниже — план, появится на этапе реализации ---
libs/core     # geometry, materials, sources, metrics, Engine API, field
libs/io       # OBJ/glTF, scenario JSON, WAV/CSV, field
libs/sim      # job queue, progress, cancel
engines/      # sabine_eyring, image_source, ray_tracing, …
apps/cli      # av-cli
apps/api      # av-api (REST + WebSocket, /v1)
tests/        # unit, integration, fixtures/golden
gui/          # P3: Electron + Vue + Three.js
```

## Roadmap

| Фаза | Поставка |
|------|----------|
| **P0** | Engine API + Sabine/Eyring + Image Source + metrics + CLI |
| **P1** | av-api REST/WS, jobs, runs history |
| **P2** | OBJ/glTF, mesh, Ray Tracing, paths, spatial field |
| **P2.5** | Dynamic plugin SDK (C ABI) |
| **P3** | GUI: базовый 3D-редактор, catalog, scene.patch, кнопка «Расчёт» |
| **P4** | Research-алгоритмы, каталог источников, сравнение runs |

## Стек (план)

C++20 · CMake ≥ 3.24 · vcpkg · Eigen · nlohmann/json · GoogleTest · Crow (P1) · tinyobjloader/tinygltf (P2) · Electron + Vue + Three.js (P3)

## Лицензия

TBD (open-source)
