# Acoustic-Vision

Симулятор акустики помещений: геометрия и материалы → импульсный отклик (IR) → метрики реверберации (RT60, EDT, ранние отражения) → визуализация.

**Стадия проекта: архитектура.** Реализации кода пока нет — в репозитории только дизайн-документы и JSON Schema контрактов.

C++ ядро расчётов · REST/WS API · Electron GUI с 3D-редактором (roadmap).

## Идея

От комнаты к IR за несколько шагов:

1. Смоделировать комнату (box/extrude в редакторе) или импортировать mesh (OBJ/glTF).
2. Назначить материалы с поглощением α(f).
3. Расставить источники и приёмники.
4. Выбрать **метод расчёта** (engine).
5. Получить IR, RT60/EDT, ранние отражения и картинку лучей.

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
| Ядро отделено от UI | C++ считает, GUI — Electron, связь REST/WS |
| Методы — плагины | Новый алгоритм не трогает API, GUI и чужие движки |
| Один контракт результата | Любой engine → IR + metrics (+ paths) |
| Core first | CLI/API работают без GUI |
| Честные пределы | Геометрическая акустика ≠ волновая; LF-моды — отдельным engine |

## Целевая структура репозитория

```
docs/         # idea.md, engines.md          — есть
schemas/      # scenario, engine-descriptor  — есть (контракты)
ARCHITECTURE.md, README.md, AGENTS.md       — есть

# --- ниже — план, появится на этапе реализации ---
libs/core     # geometry, materials, metrics, Engine API
libs/io       # OBJ/glTF, scenario JSON, WAV/CSV
libs/sim      # job queue, progress, cancel
engines/      # sabine_eyring, image_source, _template, …
apps/cli      # av-cli
apps/api      # av-api (REST + WebSocket, /v1)
tests/        # unit, integration, fixtures/golden
gui/          # P3: Electron + 3D-редактор
```

## Roadmap

| Фаза | Поставка | Код |
|------|----------|-----|
| **P0** | Engine API + Sabine/Eyring + Image Source + metrics + CLI | начать |
| **P1** | av-api REST/WS, jobs, schemas | |
| **P2** | OBJ/glTF, mesh rooms, Ray Tracing, paths | |
| **P2.5** | Dynamic plugin SDK (C ABI) | |
| **P3** | Electron GUI + 3D-редактор | |

## Стек (план)

C++20 · CMake ≥ 3.24 · vcpkg · Eigen · nlohmann/json · GoogleTest · Crow (P1) · tinyobjloader/tinygltf (P2) · Electron + three.js (P3)

## Лицензия

TBD
