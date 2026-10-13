# Симуляция

Как устроены запуски: interactive-дельты через WebSocket, compute-jobs через REST, история runs и чтение артефактов.

## Два режима

| Режим | Когда | Протокол | Что получаете |
|-------|-------|----------|----------------|
| **Interactive** | drag источника, правка позиций | WS `scene.patch` | Scene Store на бэке, `sceneVersion` растёт |
| **Compute** | кнопка «Расчёт» | `POST /v1/simulations` | job → Result (metrics / ir / field / rays) |

Интерактив **не** пересчитывает полную акустику при каждом движении мыши. Тяжёлый расчёт — отдельный job, может идти **часы**.

## Scene Store (interactive)

Сцена целиком живёт на backend (in-memory, thread-safe).

```bash
curl -s -X POST http://127.0.0.1:8787/v1/scenarios \
  -H 'Content-Type: application/json' \
  --data @demo-scenario.json
# → {"sceneId":"…","sceneVersion":1}
```

- `POST /v1/scenarios` — сохранить scenario → `sceneId`, `sceneVersion` (uint64, с 1).
- `GET /v1/scenarios/{id}` — снапшот + текущий `sceneVersion`.
- Статика (геометрия, материалы) заливается один раз через REST; через WS идут только movers.

### WS `scene.patch`

Клиент → сервер:

```json
{
  "op": "scene.patch",
  "sceneId": "…",
  "ops": [
    { "op": "move", "entityType": "source", "id": "src-1", "position": [1.5, 1.5, 1.0] },
    { "op": "setPower", "entityType": "source", "id": "src-1", "powerDb": 85 }
  ]
}
```

| op | Поля | Эффект |
|----|------|--------|
| `move` | `entityType`: `source`\|`receiver`, `id`, `position` | новая позиция |
| `setPower` | `entityType`: `source`, `id`, `powerDb` | новая мощность |

Все ops в сообщении — all-or-nothing. Одно принятое сообщение = один bump `sceneVersion`. Неизвестный op, id или кривые поля → `scene.error`, сцена не меняется. `rotate` и правки материалов в v1 отклоняются как unknown ops.

Сервер отвечает на соединение инициатора:

- `scene.patched` — `{sceneId, sceneVersion, applied}`;
- `scene.error` — `{detail}`.

Тот же outcome раздаётся подписчикам `subscribeScene` (инициатор исключён — нет двойной доставки).

## Compute job

```bash
curl -s -X POST http://127.0.0.1:8787/v1/simulations \
  -H 'Content-Type: application/json' \
  -d '{"sceneId":"<sceneId>","engine":"ray-tracing","methodParams":{"rayCount":4000}}'
# → 202 {"jobId":"…"}
```

Варианты body:

1. **Полный scenario** inline (без `sceneId`) — как в файле.
2. **`{"sceneId"}`** + overlay: `engine`, `methodParams`, `params` (`engine`, `methodParams`, `maxReflectionOrder`, `sampleRate`, `durationSec`, `bandsHz`, `speedOfSound`, `capturePaths`, `airAbsorption`, `signedIr`, `fieldGrid`). Неизвестные ключи → `400`.

Снапшот сцены фиксируется на момент submit; его `sceneVersion` попадает в run record / `paramsDigest` — прогон воспроизводим.

До старта job API фейлит fast:

| Условие | Ответ |
|---------|--------|
| кривой JSON / scenario | `400 invalid …` |
| mesh не загрузился | `400 mesh load failed` |
| неизвестный engine | `400 unknown engine` |
| capability mismatch / validate | `400 scenario validation failed` |
| нет `sceneId` в store | `404 scene not found` |

### Sync-режим

`POST /v1/simulations?sync=1` — ждать завершения и вернуть Result (+ `jobId`, `state`). Неудача — `500 simulation failed`. Удобно для скриптов и smoke-тестов.

### Жизненный цикл job

`queued → running → succeeded | failed | cancelled`.

| Метод | Путь | Назначение |
|-------|------|------------|
| GET | `/v1/jobs/{id}` | статус, progress, error |
| POST | `/v1/jobs/{id}/cancel` | отмена (если уже finished — `409`) |
| GET | `/v1/jobs/{id}/result` | Result JSON (`409`, пока нет результата) |
| GET | `/v1/jobs/{id}/ir.wav` | WAV (`404`, если IR нет) |
| GET | `/v1/jobs/{id}/field` | поле (`404`, если field нет) |

Progress на WS:

```json
{ "event": "job.progress", "jobId": "…", "fraction": 0.42, "stage": "trace" }
```

Terminal: `job.succeeded` (+ `summary`), `job.failed`, `job.cancelled`. REST-polling `/v1/jobs/{id}` — fallback, WS — progressive enhancement.

Подписка на job:

```json
{ "op": "subscribe", "jobId": "…" }
```

`jobId: "*"` — на все jobs. Подписка на сцену: `{ "op": "subscribeScene", "sceneId": "…" }` → ack `scene.subscribed`.

## Run history

Все терминальные jobs (включая failed/cancelled) попадают в историю.

```bash
curl -s http://127.0.0.1:8787/v1/runs
curl -s http://127.0.0.1:8787/v1/runs?engine=ray-tracing
curl -s http://127.0.0.1:8787/v1/runs/<runId или jobId>
```

| Поле | Смысл |
|------|--------|
| `id` | `runId` (`run-…`) |
| `jobId` | handle артефактов |
| `engine`, `scenarioName`, `sceneVersion` | контекст |
| `state`, `submittedAt`, `finishedAt` | lifecycle |
| `paramsDigest` | engine + ключевые params |
| `links` | `/v1/jobs/{jobId}/result`, `/ir.wav`, `/field` |

Список — новые сверху. `GET /v1/runs/{id}` принимает `runId` **или** `jobId`; `links` всегда по `jobId`.

## Result: что читать

```json
{
  "metrics": {
    "rt60": {
      "500": { "schroederT30": 0.42, "sabine": 0.40, "eyring": 0.41 }
    },
    "edt": { "500": 0.31 },
    "earlyReflections": [{ "t": 0.012, "amp": 0.4, "order": 1 }]
  },
  "ir": {
    "sampleRate": 48000,
    "durationSec": 1.5,
    "channels": 1,
    "channelNames": ["src-1:rcv-1"],
    "bandsHz": []
  },
  "field": {
    "nx": 32, "ny": 16, "nz": 24,
    "origin": [0, 0, 0],
    "cellSize": 0.19,
    "bandHz": 1000,
    "unit": "energy",
    "values": []
  },
  "rays": {
    "paths": [
      { "points": [[0, 0, 0], [1, 0, 0]], "amplitude": 0.9, "order": 1 }
    ]
  }
}
```

Ключи `ir` / `field` / `rays` появляются, только если engine их выдал.

### Field (heat-map)

- Линейная энергия, `unit: "energy"`.
- Индекс: `x + nx*(y + ny*z)` — x самый быстрый.
- `bandHz: 0` = broadband.
- Пересчёт в дБ — на клиенте (относительно максимума сетки; в GUI порог −30 дБ).

### Rays

- `capturePaths: true` и caps `supportsRayPaths`.
- `capturePathLimit` ограничивает число полилиний (GUI рисует ≤ 64).
- Цвет линий в GUI = амплитуда относительно сильнейшего луча.

### IR

- Канал на пару source × receiver, имя `srcId:rcvId`.
- Скачать: `GET /v1/jobs/{jobId}/ir.wav` (float32 PCM WAV).
- График в GUI парсит WAV на клиенте.

## Практический сценарий

1. Залить scenario → `sceneId`.
2. Двигать источники через `scene.patch` (или GUI drag).
3. Выбрать engine + `methodParams` → `POST /v1/simulations`.
4. Следить за `job.progress` / поллить job.
5. Открыть run: метрики, IR, field, rays.
6. Повторить с другим engine — сравнить два run’а визуально.

## Ограничения

- Store in-memory: перезапуск av-api сбрасывает scenes и runs.
- Persistence (SQLite) — later.
- Нет Study/diff-конвейера — только история и визуальное сравнение.
