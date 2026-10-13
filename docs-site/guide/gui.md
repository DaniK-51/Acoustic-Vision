# GUI

Прототип на Electron + Vue 3 + Three.js + Vite + TypeScript в `gui/`. GUI **не линкует C++** — только REST/WS `/v1`.

**Статус:** срезы GUI1–4 + V0.1.1 (runs, metrics, field heat-map, rays, gizmo, materials, catalog). Редактор помещений (V0.2) — в работе / по запросу. Полный DCC — later.

## Запуск

1. API — **из корня репозитория**:

```bash
build/linux-debug/apps/api/av-api --port 8787
```

2. GUI:

```bash
cd gui
npm install
npm run dev      # vite (127.0.0.1:5173) + electron
```

Продакшен-сборка:

```bash
npm run build    # → gui/dist/
npm start        # electron .
```

| Настройка | Где |
|-----------|-----|
| Base URL API | `gui/src/config.ts`, default `http://127.0.0.1:8787/v1`; override `VITE_API_BASE` |
| WS events | `ws://127.0.0.1:8787/v1/events` |

::: warning CORS
av-api не отдаёт CORS-заголовков. Electron main инжектит их в `onHeadersReceived`; клиент шлёт JSON-body как `text/plain` (без preflight). В обычном браузере fetch к av-api не заработает.
:::

## Раскладка

| Зона | Панели |
|------|--------|
| Слева / верх | Engines, Source catalog, Load scenario, Materials, Scene outline |
| Центр | Viewport (three.js) |
| Правая | Source inspector, Run controls, Runs |
| Низ | Status bar |
| Модалка | Run detail |

Stage-логика: каталог доступен на этапе сцены; heat-map/rays привязаны к открытому run.

## Панели

### Engines

Dropdown из `GET /v1/engines` + caps. Форма `methodParams` рисуется по `engine.paramsSchema` (number / integer / boolean / string + default / min / max / enum; raw fallback при незнакомой схеме).

### Load scenario

Paste/upload JSON → `POST /v1/scenarios` → `sceneId`. Кнопки demo:

| Кнопка | Что грузит |
|--------|------------|
| «Demo» | фикстура в духе `tests/fixtures/demo-scenario.json` |
| «Mesh demo (field)» | `gui/public/demo/box-scenario.json` (mesh + `capturePaths` + fieldGrid) |

Mesh-демо требует, чтобы `mesh.uri` (`gui/public/demo/box.obj`) резолвился: av-api из корня репозитория, либо абсолютный `uri`.

### Source catalog

Таблица `GET /v1/source-catalog` + disclaimer. Кнопка «Вставить источник» добавляет source в загруженный сценарий (в центр комнаты, со смещением от существующих). В карточке — polar-диаграмма.

### Materials

Просмотр/правка материалов сценария (α(f) по полосам).

### Scene outline

JSON-структура загруженного сценария.

### Runs / Run detail

Список `GET /v1/runs`, открытие прогона → модалка:

| Блок | Источник |
|------|----------|
| meta (runId, jobId, engine, state, время) | `GET /v1/runs/{id}` |
| RT60 (Schroeder / Sabine / Eyring по полосам) | `GET /v1/jobs/{jobId}/result` |
| EDT, early reflections (top-8 preview) | то же |
| IR chart (canvas, опция dB) | `ir.wav`, парсится клиентски |
| field summary / rays summary | result |
| кнопка скачать `ir.wav` | `/v1/jobs/{jobId}/ir.wav` |

Field/IR/rays следуют за открытым run; при смене прогона старые данные сбрасываются, чтобы не показать устаревшее.

### Status bar

Health (`GET /v1/health`), id job из submit, WS `job.*` (прогресс, terminal → refresh runs).

## Viewport

| Фича | Поведение |
|------|-----------|
| Комната | grid + box под rect-room (или mesh demo) |
| Маркеры | source / receiver, select + drag (X/Z) |
| Gizmo | TransformControls, осевое перемещение; optional grid snap |
| Числовые X/Z | точный ввод позиции |
| «Field» | point-cloud heat-map: linear energy → дБ от максимума; legend; порог −30 дБ (тихие ячейки скрыты); индекс `x + nx*(y + ny*z)` |
| «Rays» | LineSegments из `result.rays.paths`; цвет = амплитуда; cap 64 линии |
| Орбита | мышь, дистанция к камере сохраняется |

Переключатели «Field» и «Rays» в viewport. Heat-map и лучи относятся к завершённому run; новый run или новая сцена чистят старые.

## Interactive move

Select + drag (или X/Z) → WS `scene.patch` op `move`. Ответы `scene.patched` / `scene.error`. Позиция живёт на бэке — GUI не хранит master-copy сцены. Ориентация yaw/pitch в v1 сохраняется **новым snapshot-сценарием** (как и вставка из каталога): rotate-op в `scene.patch` нет.

Submit расчёта: `{"sceneId", engine?, methodParams?, params?}` — сервер накладывает overlay на snapshot.

## Что есть / чего нет

| Есть сейчас | Не в GUI3/GUI4 (позже) |
|-------------|-------------------------|
| runs, metrics, IR chart | полный 3D-редактор (wall/gizmos, import OBJ/glTF) |
| field heat-map, rays | графики RT-кривых |
| gizmo move, materials | WS `setPower` из UI (move уже есть) |
| catalog + polar | room editor V0.2 |
| methodParams form | |

## Структура кода

```
gui/
  electron/main.js     # BrowserWindow + CORS-инъекция
  src/
    main.ts            # entry Vue
    App.vue            # shell + состояние
    config.ts          # apiBaseUrl / eventsUrl
    params-schema.ts   # subset JSON Schema → форма methodParams
    api/client.ts      # REST /v1
    api/ws.ts          # /v1/events
    api/wav.ts         # парсер ir.wav
    components/        # Engine, Catalog, Viewport, RunDetail, Runs, …
```
