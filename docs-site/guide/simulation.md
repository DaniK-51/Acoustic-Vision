# Симуляция

Два режима работы: **Interactive** — синхронизация сцены через WebSocket, **Compute** — полный расчёт job-ом. История прогонов (runs) сравнивает методы на одной сцене.

## Interactive против Compute

| Режим | Когда | Протокол | Результат |
|-------|-------|----------|-----------|
| Interactive | перетаскивание источников, правка мощности | WS `scene.patch` | Scene Store на бэке |
| Compute | кнопка «Расчёт» | `POST /v1/simulations` → job | полный Result |

Interactive **не** пересчитывает акустику: он держит живой snapshot сцены, чтобы «Расчёт» стартовал с актуальных позиций. Тяжёлые методы могут считаться часами — это нормально.

## Scene Store (Interactive)

Полный scenario живёт на backend (в памяти процесса). Клиент создаёт snapshot и шлёт дельты:

```bash
curl -s -X POST http://127.0.0.1:8787/v1/scenarios \
  -H 'Content-Type: application/json' \
  --data @demo-scenario.json
# → {"sceneId":"scene-…","sceneVersion":1}
```

`sceneId` — ключ снапшота, `sceneVersion` растёт с каждым примененным patch. Дельты идут по WS `/v1/events`.

### scene.patch

```json
{
  "op": "scene.patch",
  "sceneId": "scene-…",
  "ops": [
    { "op": "move", "entityType": "source", "id": "src-1", "position": [2.0, 1.5, 1.0] },
    { "op": "setPower", "entityType": "source", "id": "src-1", "powerDb": 85.0 }
  ]
}
```

| Op | Кому | Поля |
|----|------|------|
| `move` | `source` или `receiver` | `position: [x, y, z]` |
| `setPower` | только `source` | `powerDb: number` |

Правила:

- patch применяется **целиком**: если хотя бы одна op невалидна, отклоняются все — полусохранённых mover-состояний не бывает;
- ответ инициатору — `scene.patched` (с `sceneVersion`, `applied`) или `scene.error` (с `detail`);
- подписчики этой сцены (`subscribeScene`) получают тот же исход без повторной доставки инициатору.

## Compute: запуск расчёта

### Полный scenario

```bash
curl -s -X POST http://127.0.0.1:8787/v1/simulations \
  -H 'Content-Type: application/json' \
  --data @demo-scenario.json
# → 202 {"jobId":"job-…"}
```

### Overlay поверх снапшота

Если в теле есть `sceneId`, сервер берёт snapshot из Scene Store и накладывает допустимые ключи:

```json
{
  "sceneId": "scene-…",
  "engine": "ray-tracing",
  "methodParams": { "rayCount": 8000, "seed": 7 },
  "params": { "capturePaths": true, "fieldGrid": { "enabled": true, "nx": 16, "ny": 8, "nz": 16 } }
}
```

С `sceneId` принимаются только `engine`, `methodParams`, `params` (плюс сам `sceneId`). Ключи overlay внутри `params` — те же, что в SimParams: `engine`, `methodParams`, `maxReflectionOrder`, `sampleRate`, `durationSec`, `bandsHz`, `speedOfSound`, `capturePaths`, `airAbsorption`, `signedIr`, `fieldGrid`. Неизвестный ключ → `400`.

### Синхронный режим

`POST /v1/simulations?sync=1` ждёт конца job и возвращает Result сразу (плюс `jobId`, `state`). Если job упал — `500 simulation failed`. Синхронный запуск удобен для скриптов; GUI работает асинхронно.

### Жизненный цикл job

| Состояние | Смысл |
|-----------|-------|
| `queued` | в очереди |
| `running` | считает (прогресс — WS `job.progress` и `GET /v1/jobs/{id}`) |
| `succeeded` | Result готов |
| `failed` | ошибка (текст в `error`) |
| `cancelled` | отменён `POST /v1/jobs/{id}/cancel` |

Mesh-джобы редактора сцен в `/v1/jobs` **не** попадают — их поллят через `GET /v1/scenes/{id}/mesh`.

## Run: история запусков

Один **run** = scenario + engine + params + Result. История — append-only список (новые сверху), сравнивает прогоны между методами и параметрами.

| Поле | Смысл |
|------|-------|
| `id` (`run-…`) | ключ истории |
| `jobId` | handle артефактов: `/v1/jobs/{jobId}/…` |
| `engine`, `scenarioName`, `sceneVersion` | контекст прогона |
| `state`, `submittedAt`, `finishedAt` | жизненный цикл |
| `paramsDigest` | engine + ключевые sim-параметры (включая `sceneVersion` при overlay) |

`runId` и `jobId` — **не одно и то же**: `runId` живёт в истории, `jobId` указывает на артефакты. `GET /v1/runs/{id}` принимает любой из двух.

Полезные ссылки из `GET /v1/runs/{id}`: `links.result`, `links.ir`, `links.field` — готовые пути к артефактам job.

## Артефакты результата

Result — единый контракт: ключи появляются, только если engine их выдал (`artifacts` в descriptor).

### metrics

`rt60` по полосам (ключ — центр полосы в Гц, `"0"` — broadband): `schroederT30`, `sabine`, `eyring`; `edt` по полосам; `earlyReflections` — массив `{ t, amp, order }`.

### ir (Impulse Response)

Многоканальный broadband-буфер: канал на пару `source × receiver`, имя `srcId:rcvId`. В JSON результата — метаданные (`sampleRate`, `durationSec`, `channels`, `channelNames`, `bandsHz`); сами сэмплы — в WAV:

```bash
curl -s -o ir.wav http://127.0.0.1:8787/v1/jobs/<jobId>/ir.wav
```

### field (пространственное поле)

3D-грид энергии: `nx`, `ny`, `nz`, `origin`, `cellSize`, `bandHz` (0 = broadband), `unit: "energy"`, линейный `values` с индексом `x + nx*(y + ny*z)`. Пересчёт в дБ (относительно максимума сетки) — на стороне UI. Включается `params.fieldGrid`.

### rays (лучи)

`paths[]`: `{ points, amplitude, order }` — полилинии в мировых координатах. Появляются при `capturePaths: true` и `supportsRayPaths` у engine. GUI рисует не более 64 линий.

## События WebSocket

Подписка на job: `{"op":"subscribe","jobId":"job-…"}` (или `jobId:"*"` — все jobs). События:

| Событие | Поля |
|---------|------|
| `job.progress` | `jobId`, `fraction`, `stage` |
| `job.succeeded` | `jobId`, `simulationId`, `summary` (metrics) |
| `job.failed` | `jobId`, `error` |
| `job.cancelled` | `jobId` |

Подписка на сцену: `{"op":"subscribeScene","sceneId":"scene-…"}` → ack `scene.subscribed`. Дальше — `scene.patched` / `scene.error` (см. выше).

Некорректные и неизвестные кадры сервер молча игнорирует: REST остаётся авторитетным контрактом.

## Практический сценарий сравнения методов

1. Загрузить сцену: `POST /v1/scenarios` → `sceneId`.
2. Запустить `sabine-eyring` для базовой линии RT60.
3. Запустить `image-source` (rect) с `maxReflectionOrder: 3–5` — ранние отражения и IR.
4. Для mesh-сцены — `ray-tracing` и `hybrid` с одинаковыми `rayCount` и `seed`.
5. Сверить `GET /v1/runs` — `paramsDigest` подскажет, чем прогоны различались.
