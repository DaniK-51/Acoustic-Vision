# Быстрый старт

Страница ведёт от сборки ядра к первому расчёту через CLI, API и GUI.

## Требования

| Компонент | Версия / заметки |
|-----------|------------------|
| CMake | ≥ 3.24 |
| Компилятор | MSVC (Windows) или GCC/Clang (Linux) |
| vcpkg | manifest-режим (`vcpkg.json`) |
| Node.js | 18+ для GUI и этой документации |
| Python | 3.x для интеграционных smoke-тестов |

## Сборка ядра

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Windows:

```bash
cmake --preset windows-msvc-x64-release
cmake --build --preset windows-msvc-x64-release
ctest --preset windows-msvc-x64-release
```

Бинарники появляются в `build/<preset>/apps/…`. Юнит-тесты — `ctest` (~237).

## av-cli: первый расчёт без сервера

```bash
build/linux-debug/apps/cli/av-cli engines
build/linux-debug/apps/cli/av-cli run tests/fixtures/demo-scenario.json -o out/
```

`av-cli engines` печатает зарегистрированные методы и их артефакты.  
`av-cli run` читает scenario JSON, валидирует через engine, пишет в `-o`:

| Файл | Когда |
|------|--------|
| `metrics.json` | engine выдаёт `metrics` |
| `ir.wav` | engine выдаёт `ir` |
| `field.json` | engine выдаёт `field` |

Mesh-демо (поле + лучи):

```bash
build/linux-debug/apps/cli/av-cli run tests/fixtures/box-scenario.json -o out-mesh/
```

Подробнее о командах — в [справочнике CLI](/reference/cli).

## av-api + GUI

### 1. Поднять API

Запускайте **из корня репозитория** — относительные `mesh.uri` резолвятся против CWD процесса:

```bash
build/linux-debug/apps/api/av-api --port 8787
```

Проверка:

```bash
curl http://127.0.0.1:8787/v1/health
curl http://127.0.0.1:8787/v1/engines
```

### 2. Открыть GUI

```bash
cd gui
npm install
npm run dev      # vite (127.0.0.1:5173) + electron
```

Продакшен-сборка рендерера:

```bash
npm run build    # → gui/dist/
npm start        # electron .
```

Base URL API — `gui/src/config.ts` (по умолчанию `http://127.0.0.1:8787/v1`), переопределяется `VITE_API_BASE`. WS: `ws://127.0.0.1:8787/v1/events`.

::: warning CORS
av-api не отдаёт CORS-заголовков. Electron main инжектит их в `onHeadersReceived`, клиент шлёт JSON-body как `text/plain` (без preflight). В обычном браузере fetch к av-api не заработает — это desktop-контракт.
:::

### 3. Загрузить demo-сценарий

В GUI:

1. Кнопка **Demo** → **Load** — появляется `sceneId`.
2. Для mesh-демо с полем и лучами — **Mesh demo (field)**: грузит `gui/public/demo/box-scenario.json` (`capturePaths: true`, `fieldGrid` включён).

`mesh.uri` в mesh-демо = `gui/public/demo/box.obj` и резолвится относительно CWD av-api. Поэтому API запускают из корня; иначе пропишите в `uri` абсолютный путь к `box.obj`.

### 4. Переместить источник

Перетащите источник мышью в viewport (или введите X/Y/Z, гизмо). Позиция уезжает в backend через WS `scene.patch`.

### 5. Нажать «Расчёт»

Job стартует, прогресс — в статус-баре. Результат появляется в истории runs: метрики, `ir.wav`, поле и лучи — по запросу engine.

### 6. Посмотреть результат

В viewport:

- **Field** — heat-map, дБ относительно максимума сетки;
- **Rays** — полилинии лучей из `result.rays`, цвет = амплитуда, не более 64 линий.

Повторяйте шаги 4–5 с разными алгоритмами — история runs сравнивает прогоны.

## Первый расчёт через REST

```bash
# 1. Сохранить сцену
curl -s -X POST http://127.0.0.1:8787/v1/scenarios \
  -H 'Content-Type: application/json' \
  --data @tests/fixtures/demo-scenario.json
# → {"sceneId":"…","sceneVersion":1}

# 2. Запустить (async)
curl -s -X POST http://127.0.0.1:8787/v1/simulations \
  -H 'Content-Type: application/json' \
  -d '{"sceneId":"<sceneId>"}'
# → {"jobId":"…"}

# 3. Статус / результат
curl -s http://127.0.0.1:8787/v1/jobs/<jobId>
curl -s http://127.0.0.1:8787/v1/jobs/<jobId>/result
```

Синхронный режим: `POST /v1/simulations?sync=1` — ответ приходит с готовым result (или `500`, если job упал).

## Редактор сцен и mesh (V0.2)

```bash
av-cli gen-mesh box -o room.obj --w 6 --h 2.8 --d 4.5 --scenario room.json
av-cli mesh scene.json -o out.obj --report report.json --quality standard
```

API: `POST /v1/scenes` · `POST /v1/scenes/{id}/mesh` · `GET mesh.obj` · `POST to-scenario`.

Подробнее — [Редактор сцен](/guide/scene-editor) и [Mesh](/guide/mesh).

## Документы проекта

| Документ | Содержание |
|----------|------------|
| `docs/idea.md` | видение, принципы, roadmap |
| `docs/engines.md` | Engine API, как добавлять методы |
| `ARCHITECTURE.md` | модули, API, данные, стек |
| `schemas/` | JSON Schema — контракты |
