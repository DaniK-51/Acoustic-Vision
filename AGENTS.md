# AGENTS.md — правила для агентов в Acoustic-Vision

## Роль

Агент в этом репозитории — **архитектор и реализатор**.

- По умолчанию: проектирование, документация, согласованность.
- **Код** — только когда пользователь просит реализацию (или явно открывает этап P0+).

## Стадия проекта

**P0 delivered** (core + Engine API + sabine-eyring + image-source + av_io + av-cli). Next: **P1** (av_sim jobs + av-api REST/WS).

Реализация идёт по `ARCHITECTURE.md` и `docs/engines.md`. GUI (P3) и dynamic DLL (P2.5) — не трогать. Новый scope — сначала в docs, потом код.

## Источники истины

| Приоритет | Документ | Для чего |
|-----------|----------|----------|
| 1 | `docs/idea.md` | Видение, принципы, ограничения продукта |
| 2 | `ARCHITECTURE.md` | Модули, API, модель данных, стек, фазы |
| 3 | `docs/engines.md` | Контракт Engine API и модель плагинов |
| 4 | `schemas/*.json` | Формальный контракт Scenario / EngineDescriptor |
| 5 | `README.md` | Краткий обзор для человека |

При противоречии: идея → архитектура → engines → schemas. После изменения **обновить все затронутые документы в одном заходе**.

## Зафиксированные решения (не менять без явного запроса пользователя)

1. **Продукт** — open-source инструмент акустического анализа помещений (видение: `docs/idea.md`).
2. **Ядро** — C++20, target OS Windows (MSVC); сборка CMake ≥ 3.24 + vcpkg.
3. **GUI** — Electron + Vue + Three.js (фаза P3); базовый 3D-редактор в v1, полный DCC в конце.
4. **Связь GUI ↔ ядро** — только REST + WebSocket (`/v1`). GUI не линкует C++.
5. **Два режима** — Interactive: WS `scene.patch` (дельты movers), статика на бэке; Compute: кнопка «Расчёт», job может идти **часы** (CPU/GPU). Без обещания «1 с на любой алгоритм».
6. **Источники** — custom (мощность, АЧХ, направленность) + **built-in catalog** «как есть»; без measurement/mic pipeline.
7. **Сравнение** — **история запусков (runs)**, не тяжёлый Study/diff в v1.
8. **Артефакты** — определяет engine (`ir` / `metrics` / `field` / `rays` / `findings`).
9. **Ввод геометрии** — свой 3D-редактор (базовый в v1) + импорт OBJ/glTF.
10. **Порядок поставки** — core first, GUI later. Одна сцена → много методов параллельно.
11. **Расширяемость методов** — модель **A → B**: compile-time `EngineRegistry`; dynamic DLL C ABI — P2.5.
12. **В P0** — полный Engine API + `sabine-eyring` + `image-source`.
13. **Единый Result** — metrics / IR / field / rays / findings (по caps engine).
14. **SI метры** — конвертация единиц только при импорте.

---

## Dev rules (P0+)

### Стек и layout

```
libs/core     # av_core — расчёты, Engine API (без HTTP/файлов)
libs/io       # av_io — scenario JSON, экспорт WAV/CSV
libs/sim      # av_sim — job/run orchestration (в P0 — sync runner достаточно)
apps/cli      # av-cli
apps/api      # av-api (P1)
engines/      # по желанию; можно и внутри core до P2
tests/unit    # GoogleTest
schemas/      # JSON Schema
```

Зависимости: `av_core` ← `av_io` ← `av_sim` ← (`av-cli`, `av-api`). Запрет обратных и циклических линков.

### C++ стиль

- **C++20**, без compiler extensions (`CXX_EXTENSIONS OFF`).
- Именование: `PascalCase` — типы/классы; `camelCase` — методы; `snake_case` — файлы; `kConst` / `m_` — по согласованному в headers (смотри существующие).
- Заголовки: `#pragma once`, минимальные include, forward-declare где можно.
- Ошибки: исключения — на границах IO/CLI; внутри движков — `ValidateResult` + коды; не `exit()` из библиотек.
- Комментарии — только **почему**, не **что** (см. конвенции ISM — там комментарии обязательны).
- Предупреждения: `/W4 /permissive-` (MSVC) или `-Wall -Wextra`; в CI можно `/WX`.
- Формат: clang-format, если появится config — держаться его.

### Чего не делать в коде

- Не тащить Boost, FDTD-либы, GPU-SDK в P0.
- Не писать GUI/N-API/WASM-мосты.
- Не менять конвенции ISM молча (`docs/engines.md` §5):  
  `t=r/c`, amp `∝ 1/r`, bounce amplitude `√(1-α)` / energy `×(1-α)`, `energy=amp²`.
- Не коммитить `build/`, `.mimocode/`, артефакты `.wav`.
- Не коммитить без просьбы пользователя (см. Git).

### Порядок реализации P0

1. CMake superbuild + vcpkg + GoogleTest harness.
2. `av_core`: `Vec3`, `RectRoom`, `Material`, Scenario/SimParams, units.
3. **Engine API**: `IEngine`, `EngineDescriptor` (+ caps, paramsSchema), `ValidateResult`, `CancelToken`, `EngineRegistry`, `REGISTER_ENGINE`, `Result`.
4. `SabineEyringEngine` + unit-тесты (аналитика vs hand calc).
5. `ImageSourceEngine` + `IrBuffer` + metrics (Schroeder T30, EDT, early reflections) + golden shoebox.
6. `av_io`: scenario JSON r/w, WAV/CSV export.
7. `av-cli`: `run <scenario.json>`, `engines`.
8. Заморозить `schemas/` (scenario + engine-descriptor) → можно P1.

### Definition of Done (feature/engine)

- [ ] Код собирается (`cmake --preset …` / `cmake --build`).
- [ ] Юнит-тесты зелёные (`ctest`).
- [ ] Для алгоритма с аналитикой — golden/сравнение в допуске.
- [ ] `validate()` честно отвечает на неподдерживаемый room/params.
- [ ] `CancelToken` опрашивается в циклах (если расчёт > 100 мс).
- [ ] `descriptor()` / caps / paramsSchema заполнены.
- [ ] Документы не врут (если менялся контракт — обновить docs + schemas).
- [ ] Ручной smoke: `av-cli run tests/fixtures/demo-scenario.json`.

### Как добавлять engine (dev-чеклист)

1. Реализовать `IEngine` (descriptor, validate, run).
2. `REGISTER_ENGINE` + появляется в registry.
3. Тесты: вход/выход, capability mismatch → fail validate.
4. При необходимости — fixture-scenario.
5. Строка в таблице `docs/engines.md` §4.
6. **Не** править GUI/API — контракт самодостаточен.

---

## Git

- Remote: `origin` → `git@github.com:DaniK-51/Acoustic-Vision.git` (ветка `main`).
- История чистая: без `build/`, `.mimocode/`, мусора.
- **Коммит и push — только по явной просьбе пользователя.**
- Сообщение коммита: `feat|fix|docs|test|build|refactor(scope): …` (imperative mood, зачем > что).

## Чего не делать

- Не начинать реализацию без запроса (если stage снова «architecture-only»).
- Не добавлять FDTD/волновое ядро в v1 (это отдельный engine позже).
- Не тащить measurement/mic/phone pipeline.
- Не тащить auth, multi-tenant, multi-room в v1.
- Не обещать «пересчёт всей акустики за 1 с».
- Не подключать GUI к ядру иначе чем через HTTP/WS.
- Не менять конвенции ISM молча.
- Не создавать лишние git-коммиты.

## Как оформлять архитектурные изменения

1. Обоснование (зачем, какая альтернатива).
2. Согласованный апдейт `docs/` + `schemas/`.
3. Отметка в roadmap при смене фазы.
4. Резюме пользователю: что изменилось и компромиссы.

## Согласованность (чек-лист перед сдачей docs)

- [ ] Roadmap в `README.md`, `docs/idea.md`, `ARCHITECTURE.md` совпадает.
- [ ] «Не в v1» одинаковый.
- [ ] Engine API в `ARCHITECTURE.md` и `docs/engines.md` идентичен по полям.
- [ ] `schemas/` покрывает поля из docs.
- [ ] Стек упоминается одинаково.
- [ ] Нет намёков на несуществующий код.
- [ ] README/docs — по-русски; `ARCHITECTURE.md` — можно по-английски.

## Стиль документации

- Короткие таблицы.
- Формулы: V, S, ᾱ, α(f), r, c.
- JSON — валидный, с `schemaVersion`.
- Не выдумывать URL и не писать «код уже работает», если это не так.
