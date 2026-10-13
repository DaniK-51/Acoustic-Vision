# REST API и WebSocket

Base: `http://localhost:8787/v1` · WS: `ws://localhost:8787/v1/events` · JSON only · `Content-Type: application/json`.

Контракт вытекает из `apps/api/routes_*.cpp`. Store in-memory; перезапуск av-api сбрасывает данные.

## Ошибки

Формат RFC7807-ish:

```json
{
  "type": "about:blank",
  "title": "job not found",
  "status": 404,
  "detail": "job-123",
  "fields": {}
}
```

| Поле | Смысл |
|------|--------|
| `type` | тип проблемы |
| `title` | короткий заголовок |
| `status` | HTTP-код |
| `detail` | детали (id, state, текст исключения) |
| `fields` | опционально |

Типовые коды:

| Код | Когда |
|-----|--------|
| 400 | кривой JSON / scenario / scene / overlay / validate / mesh load |
| 404 | job, run, scene не найдены; нет артефакта |
| 409 | result ещё нет / job уже finished |
| 500 | simulation failed / to-scenario failed |

---

## Служебные

### GET `/v1/health`

Liveness + версия.

```json
{ "status": "ok", "version": "…", "engines": ["sabine-eyring", "image-source", "ray-tracing", "hybrid"] }
```

### GET `/v1/engines`

Массив `EngineDescriptor`: `id`, `name`, `version`, `caps`, `paramsSchema`, `artifacts`.

### GET `/v1/bands`

```json
{ "bandsHz": [125, 250, 500, 1000, 2000, 4000] }
```

### GET `/v1/source-catalog`

Built-in каталог приборов. Всегда `200` (пустой список, если файл не загрузился). Формат — [Источники](/guide/sources) и `schemas/source-catalog.schema.json`.

---

## Scenarios (Scene Store)

### POST `/v1/scenarios`

Body — scenario JSON. Ответ `201`:

```json
{ "sceneId": "…", "sceneVersion": 1 }
```

### GET `/v1/scenarios/{id}`

```json
{ "sceneId": "…", "sceneVersion": 1, "scenario": { … } }
```

`404 scene not found`.

> Не путать с `/v1/scenes` — это документ редактора (V0.2), отдельное id-пространство.

---

## Simulations

### POST `/v1/simulations`

**Вариант 1 — inline scenario** (без `sceneId`): body = полный scenario JSON.

**Вариант 2 — `{"sceneId"}`** + overlay:

| Ключ body | Смысл |
|-----------|--------|
| `sceneId` | обязательная строка |
| `engine`, `methodParams` | shorthand для `params.*` |
| `params` | объект-override: `engine`, `methodParams`, `maxReflectionOrder`, `sampleRate`, `durationSec`, `bandsHz`, `speedOfSound`, `capturePaths`, `airAbsorption`, `signedIr`, `fieldGrid` |

Неизвестные ключи рядом с `sceneId` → `400`. `methodParams` / `fieldGrid` должны быть объектами.

**Query:** `?sync=1` — синхронный запуск.

**Async (default):** `202 { "jobId": "…" }`.  
**Sync:** `200` — Result JSON + `jobId` + `state`; при падении `500 simulation failed`.

До submit: `400 invalid JSON body` / `invalid scenario` / `mesh load failed` / `unknown engine` / `scenario validation failed`; `404 scene not found`.

Mesh-URI резолвятся против **CWD процесса av-api**.

---

## Jobs

### GET `/v1/jobs/{id}`

Статус job: `jobId`, `state` (`queued`/`running`/`succeeded`/`failed`/`cancelled`), progress, error. `404 job not found`.

### POST `/v1/jobs/{id}/cancel`

Запрос отмены. `404` если нет; `409 job already finished` если уже терминален. Ответ — текущий статус.

### GET `/v1/jobs/{id}/result`

Result JSON (`resultToJson`). `metrics` присутствует всегда (возможно, пустым); `ir` / `field` / `rays` — только если engine их выдал; сэмплы IR в JSON не кладутся — они в WAV. `409 result not available`, пока job не succeeded. `404 job not found`.

### GET `/v1/jobs/{id}/ir.wav`

WAV (float32 PCM), `Content-Type: audio/wav`. `404 ir not available`, если артефакта нет.

### GET `/v1/jobs/{id}/field`

Spatial field grid JSON. `404 field not available`, если поля нет.

---

## Runs (история)

### GET `/v1/runs`

Массив run-summary, **новые сверху**. Query `?engine=` — фильтр.

Поля элемента: `id` (runId), `jobId`, `engine`, `scenarioName`, `sceneVersion`, `state`, `submittedAt`, `finishedAt`, `paramsDigest`.

### GET `/v1/runs/{id}`

Принимает **`runId` или `jobId`**. Полная запись + `links`:

```json
{
  "links": {
    "result": "/v1/jobs/<jobId>/result",
    "ir": "/v1/jobs/<jobId>/ir.wav",
    "field": "/v1/jobs/<jobId>/field"
  }
}
```

`404 run not found`. Все терминальные jobs (включая failed/cancelled) попадают в историю.

---

## Scenes (редактор V0.2)

Отдельная id-пространство от `/v1/scenarios`. Схема — `schemas/scene.schema.json`.

### POST `/v1/scenes`

Body — scene JSON. `201 { sceneId, sceneVersion }`. Ошибки parse/validate → `400`.

### GET `/v1/scenes/{id}`

`{ sceneId, sceneVersion, scene }`. `404 scene not found`.

### PUT / PATCH `/v1/scenes/{id}`

**Замена всего документа** (v1; PATCH = тот же handler). Валидация как у POST. `200 { sceneId, sceneVersion }`. Mesh при замене сбрасывается.

### POST `/v1/scenes/{id}/mesh`

Генерация surface mesh. Query `?sync=1` — сразу вернуть отчёт.

- Async: `202 { sceneId, jobId, state: "running" }`.
- Sync: статус mesh-job + `report` (если готов).

Mesh-job id (`meshjob-…`) **не** живёт в `/v1/jobs`.

### GET `/v1/scenes/{id}/mesh`

Состояние:

```json
{
  "sceneId": "…",
  "sceneVersion": 1,
  "jobId": "meshjob-…",
  "state": "succeeded",
  "report": { … }
}
```

`404 mesh not generated`, если генерации ещё не было.

### GET `/v1/scenes/{id}/mesh.obj`

`Content-Type: model/obj`. `404 mesh not generated`.

### POST `/v1/scenes/{id}/to-scenario`

Компиляция Scene → Scenario JSON (для `/v1/simulations`).  
Ветка mesh пишет OBJ во временный файл, `mesh.uri` — абсолютный.  
`400 scene validation failed`; `500 to-scenario failed`.

---

## WebSocket `/v1/events`

### Клиент → сервер

| op | Поля | Назначение |
|----|------|------------|
| `subscribe` | `jobId` (или `"*"`) | события job.* |
| `subscribeScene` | `sceneId` | fan-out scene.* (ack `scene.subscribed`) |
| `scene.patch` | `sceneId`, `ops[]` | дельты movers |

Битые JSON-фреймы и неизвестные ops молча игнорируются. REST остаётся авторитетным.

### scene.patch ops (v1)

```json
{
  "op": "scene.patch",
  "sceneId": "…",
  "ops": [
    { "op": "move", "entityType": "source", "id": "src-1", "position": [1, 1.5, 2] },
    { "op": "setPower", "entityType": "source", "id": "src-1", "powerDb": 90 }
  ]
}
```

| op | Поля | Эффект |
|----|------|--------|
| `move` | `entityType`: `source`\|`receiver`, `id`, `position` | позиция |
| `setPower` | `entityType`: `source`, `id`, `powerDb` | мощность |

All-or-nothing на сообщение; один bump `sceneVersion`. Неизвестный op / id / поля → `scene.error`, сцена не тронута.

### Сервер → клиент

| event | payload |
|-------|---------|
| `scene.subscribed` | `sceneId` (ack подписки) |
| `scene.patched` | `sceneId`, `sceneVersion`, `applied` |
| `scene.error` | `detail` (+ fan-out) |
| `job.progress` | `jobId`, `fraction` 0..1, `stage` |
| `job.succeeded` | `jobId`, `simulationId`, `summary` |
| `job.failed` | `jobId`, `error` |
| `job.cancelled` | `jobId` |
| `simulation.reflection` | optional P3+: batched paths для live 3D |

`scene.patched` / `scene.error` приходят инициатору patch и fan-out подписчикам `subscribeScene` (инициатор исключён — без двойной доставки).

REST-polling `GET /v1/jobs/{id}` — fallback; WS — progressive enhancement.

---

## Примечания

- Все id — uuid-lite / `run-…` / `job-…` / `meshjob-…`.
- `sceneVersion` — uint64 везде (run record, paramsDigest, wire).
- Планируется, но **не реализовано**: CRUD materials/rooms/sources/receivers, `POST /v1/rooms/import`, list/PATCH/DELETE scenarios, `GET /v1/scenarios/{id}/validate`.
