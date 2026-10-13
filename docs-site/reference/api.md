# REST API и WebSocket

Base URL: `http://127.0.0.1:8787/v1` (порт — флаг `av-api --port`). Все тела — JSON, кроме `ir.wav` и `mesh.obj`. Ошибки — объект в духе problem+json:

```json
{ "type": "about:blank", "title": "scenario validation failed", "status": 400, "detail": "…" }
```

Список маршрутов ниже соответствует `apps/api/routes_*.cpp`.

## Метаданные

### GET /v1/health

```json
{ "status": "ok", "version": "…", "engines": ["sabine-eyring", "image-source", "ray-tracing", "hybrid"] }
```

### GET /v1/engines

Массив `EngineDescriptor`: `id`, `name`, `version`, `caps`, `paramsSchema`, `artifacts`. GUI строит dropdown и форму `methodParams` из этого ответа.

### GET /v1/bands

```json
{ "bandsHz": [125, 250, 500, 1000, 2000, 4000] }
```

Default-полосы расчёта. Свои полосы — `params.bandsHz` сценария.

### GET /v1/source-catalog

Каталог приборов: `{ schemaVersion?, disclaimer, sources: [...] }`. Всегда `200`; пустой `sources`, если файл каталога не загрузился. Данные — «как есть», disclaimer обязателен. Подробнее — [Источники](/guide/sources).

## Scenario Store

### POST /v1/scenarios

Тело — полный Scenario JSON. Создаёт snapshot в Scene Store.

Ответ `201`: `{ "sceneId": "scene-…", "sceneVersion": 1 }`.

Ошибки: `400 invalid JSON body` / `400 invalid scenario`.

### GET /v1/scenarios/{sceneId}

`200`: `{ "sceneId", "sceneVersion", "scenario": { … } }`; `404 scene not found`.

## Симуляции

### POST /v1/simulations

Два вида тела:

1. **Полный scenario** (без `sceneId`) — как файл сценария.
2. **Overlay**: `{ "sceneId", "engine"?, "methodParams"?, "params"? }` — snapshot + допустимые ключи SimParams.

Ошибки: `400 invalid submit body` (лишние ключи при `sceneId`, неизвестный override), `404 scene not found`, `400 mesh load failed`, `400 unknown engine`, `400 scenario validation failed`.

Ответы:

| Режим | Код | Тело |
|-------|-----|------|
| async (default) | `202` | `{ "jobId": "job-…" }` |
| `?sync=1` (или `?sync=true`) | `200` | Result + `jobId`, `state` |
| sync, job упал | `500` | `simulation failed` |

## Jobs

### GET /v1/jobs/{jobId}

```json
{
  "id": "job-…",
  "state": "running",
  "progress": { "fraction": 0.42, "stage": "ray-tracing" },
  "error": ""
}
```

`state`: `queued` / `running` / `succeeded` / `failed` / `cancelled`. `404 job not found`.

### POST /v1/jobs/{jobId}/cancel

`200` — тот же объект статуса (после отмены); `404 job not found`; `409 job already finished`, если job уже в терминальном состоянии.

### GET /v1/jobs/{jobId}/result

`200` — Result JSON (см. ниже). `404 job not found`; `409 result not available`, если job ещё не succeeded (`detail` содержит `state=` и текст ошибки).

Форма Result (`resultToJson`):

```json
{
  "metrics": {
    "rt60": { "500": { "schroederT30": 0.85, "sabine": 0.9, "eyring": 0.87 } },
    "edt": { "0": 0.12 },
    "earlyReflections": [ { "t": 0.011, "amp": 0.4, "order": 1 } ]
  },
  "ir": { "sampleRate": 48000, "durationSec": 1.5, "channels": 1, "channelNames": ["src-1:rcv-1"], "bandsHz": [] },
  "field": { "nx": 16, "ny": 8, "nz": 16, "origin": [0, 0, 0], "cellSize": 0.25, "bandHz": 0, "unit": "energy", "values": [ … ] },
  "rays": { "paths": [ { "points": [[0,0,0],[1,0,0]], "amplitude": 0.8, "order": 2 } ] }
}
```

`metrics` присутствует всегда (возможно, пустым); `ir` / `field` / `rays` — только если engine их выдал. Сами сэмплы IR в JSON не кладутся — за ними в WAV.

### GET /v1/jobs/{jobId}/ir.wav

`200` `audio/wav` — многоканальный WAV (канал на пару source × receiver). `404` (`job not found` / `ir not available`).

### GET /v1/jobs/{jobId}/field

`200` — field JSON (тот же объект, что `result.field`). `404` (`job not found` / `field not available`).

## Runs (история)

### GET /v1/runs

Массив сводок, новые сверху. Query `?engine=ray-tracing` — фильтр по engine.

```json
[
  {
    "id": "run-…",
    "jobId": "job-…",
    "engine": "hybrid",
    "scenarioName": "demo",
    "sceneVersion": 3,
    "state": "succeeded",
    "submittedAt": "2026-01-15T10:00:00.000Z",
    "finishedAt": "2026-01-15T10:00:04.210Z",
    "paramsDigest": { "engine": "hybrid", "…": "…" }
  }
]
```

`id` — ключ истории (`run-…`), `jobId` — handle артефактов. Записи append-on-terminal; GET дополнительно сверяет терминальные jobs, чтобы быстрый прогон был виден сразу.

### GET /v1/runs/{id}

Принимает **либо `runId`, либо `jobId`**. `200` — сводка плюс ссылки:

```json
{
  "id": "run-…",
  "jobId": "job-…",
  "…": "…",
  "links": {
    "result": "/v1/jobs/job-…/result",
    "ir": "/v1/jobs/job-…/ir.wav",
    "field": "/v1/jobs/job-…/field"
  }
}
```

`404 run not found`.

## Редактор сцен (V0.2)

Отдельное id-пространство от Scenario Store. Хранение — в памяти.

| Метод | Путь | Назначение |
|-------|------|------------|
| POST | `/v1/scenes` | создать Scene (тело — Scene JSON) → `201 {sceneId, sceneVersion}` |
| GET | `/v1/scenes/{id}` | `200 {sceneId, sceneVersion, scene}` |
| PUT, PATCH | `/v1/scenes/{id}` | **замена всего документа** → `200 {sceneId, sceneVersion}`; mesh сбрасывается |
| POST | `/v1/scenes/{id}/mesh` | генерация mesh; `?sync=1` — сразу report, иначе `202 {sceneId, jobId, state}` |
| GET | `/v1/scenes/{id}/mesh` | `200 {sceneId, sceneVersion, jobId, state, report?, error?}`; `404` если не генерировали |
| GET | `/v1/scenes/{id}/mesh.obj` | `200 model/obj`; `404` если mesh нет |
| POST | `/v1/scenes/{id}/to-scenario` | compile Scene → Scenario JSON |

Ошибки: `400 invalid JSON body` / `400 invalid scene` / `400 scene validation failed`; `500 to-scenario failed`.

Mesh-джобы **не** живут в `/v1/jobs` — поллите `GET /v1/scenes/{id}/mesh`.

## WebSocket /v1/events

Один endpoint, три клиентские операции (JSON-кадры, бинарные игнорируются):

| Op | Тело | Эффект |
|----|------|--------|
| `subscribe` | `{"op":"subscribe","jobId":"job-…"}` | события job (или `jobId:"*"` — все) |
| `subscribeScene` | `{"op":"subscribeScene","sceneId":"scene-…"}` | события сцены; ack `scene.subscribed` |
| `scene.patch` | `{"op":"scene.patch","sceneId":"…","ops":[…]}` | дельты movers |

Неизвестные `op` и некорректные кадры молча отбрасываются.

### scene.patch ops

```json
[
  { "op": "move", "entityType": "source", "id": "src-1", "position": [1, 1.5, 2] },
  { "op": "setPower", "entityType": "source", "id": "src-1", "powerDb": 85 }
]
```

| Op | `entityType` | Поля | Ошибки |
|----|--------------|------|--------|
| `move` | `source` / `receiver` | `position: [x,y,z]` | неверный тип, кривой `position` |
| `setPower` | `source` | `powerDb: number` | неверный тип, не число |

Patch атомарен: одна невалидная op — отклонение всего patch. Ответ инициатору и fan-out подписчикам сцены (без инициатора):

```json
{ "event": "scene.patched", "sceneId": "scene-…", "sceneVersion": 4, "applied": 2 }
{ "event": "scene.error", "sceneId": "scene-…", "detail": "move position must be a [x, y, z] number array" }
```

### События сервера

| Событие | Поля |
|---------|------|
| `job.progress` | `jobId`, `fraction`, `stage` |
| `job.succeeded` | `jobId`, `simulationId`, `summary` (metrics) |
| `job.failed` | `jobId`, `error` |
| `job.cancelled` | `jobId` |
| `scene.subscribed` | `sceneId` (ack на `subscribeScene`) |
| `scene.patched` | `sceneId`, `sceneVersion`, `applied` |
| `scene.error` | `sceneId`, `detail` |

## Заметки

- Хранение сцен и runs — **в памяти**: перезапуск `av-api` очищает историю.
- CORS-заголовков нет — контракт desktop (Electron инжектит CORS сам).
- `mesh.uri` в `POST /v1/simulations` резолвится относительно **CWD процесса** `av-api`.
