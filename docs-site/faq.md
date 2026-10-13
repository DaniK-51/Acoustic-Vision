# Частые вопросы

## Почему `mesh.uri` не находит файл?

Разные инструменты резолвят URI по-разному:

| Кто | База |
|-----|------|
| `av-cli run` | каталог scenario-файла |
| `av-api` (`POST /v1/simulations`) | **CWD процесса** |

Поэтому API нужно запускать **из корня репозитория** (как в README и GUI-демо):

```bash
build/linux-debug/apps/api/av-api --port 8787
```

Либо пропишите в `mesh.uri` **абсолютный** путь к OBJ/glTF. Иначе `POST /v1/simulations` вернёт `400 mesh load failed` — job не стартует.

«Mesh demo» в GUI использует `gui/public/demo/box.obj` (CWD-relative). Отсюда требование «API из корня».

---

## Z-up или Y-up?

| Документ | Вверх | Пример |
|----------|-------|--------|
| **Scene** (редактор, `scene.schema.json`) | **Z** | extrude по Z, CAD-логика |
| **Scenario** (симуляция, GUI viewport) | **Y** | `room.size.h` — высота, `position: [x, y, z]` |

`to-scenario` переводит Z-up → Y-up автоматически. Если пишете scenario руками: y — вертикаль. Если scene: z — вертикаль.

---

## Что такое `fieldGrid`?

`params.fieldGrid` включает пространственное поле (heat-map):

```json
"fieldGrid": { "enabled": true, "nx": 32, "ny": 16, "nz": 24 }
```

| Поле | Диапазон |
|------|----------|
| `nx`, `ny`, `nz` | 1…64 |

Поле заполняют engines с артефактом `field` (ray-tracing, hybrid). Значения — линейная энергия (`unit: "energy"`), индекс `x + nx*(y + ny*z)`. Пересчёт в дБ идёт в GUI/API-edge. `bandHz: 0` — broadband.

Без `fieldGrid.enabled` ключ `field` в результате не появится.

---

## Почему GUI не открывает API из браузера (CORS)?

av-api **не** отдаёт CORS-заголовков. Это desktop-контракт:

- Electron main инжектит заголовки в `onHeadersReceived`;
- клиент шлёт JSON-body как `text/plain` (без preflight).

В обычном браузере fetch к av-api не заработает. Для скриптов используйте curl / httpx / av-cli.

---

## `sceneId` из `/v1/scenarios` и `/v1/scenes` — это одно и то же?

**Нет.** Разные id-пространства:

| Endpoint | Документ | Для чего |
|----------|----------|----------|
| `/v1/scenarios` | Scenario | расчёт, Scene Store, scene.patch |
| `/v1/scenes` | Scene (V0.2) | редактор, mesh, to-scenario |

Совпадения id случайны; не подставляйте один в другой ресурс.

---

## Чем `runId` отличается от `jobId`?

| Поле | Роль |
|------|------|
| `runId` (`run-…`) | ключ истории, `GET /v1/runs/{id}` |
| `jobId` | handle артефактов `/v1/jobs/{jobId}/…` |

`GET /v1/runs/{id}` принимает **либо то, либо другое**; `links` в ответе всегда по `jobId`. В историю попадают все терминальные jobs, включая failed/cancelled.

---

## Расчёт идёт слишком долго. Это нормально?

Да. Compute-режим не обещает «1 секунду»: тяжёлые методы (особенно mesh + ray-tracing с большим `rayCount`) могут считаться **минуты и часы**. Ускорение:

| Приём | Эффект |
|-------|--------|
| уменьшить `rayCount`, `maxBounces` | быстрее, шумнее |
| `quality: draft` для mesh | меньше треугольников |
| меньше `durationSec`, ниже `sampleRate` | короче IR |
| выключить `fieldGrid`, `capturePaths` | без поля/лучей |
| `image-source` / `sabine-eyring` для rect | почти мгновенно |

Отмена: `POST /v1/jobs/{id}/cancel` (или кнопка в GUI).

---

## Почему ISM ругается на mesh?

`image-source` и `sabine-eyring` — **только rect** (`caps.supportsMesh = false`). Меш-комната → capability mismatch → `400 scenario validation failed`. Используйте `ray-tracing` или `hybrid`.

---

## Где лежат engines?

In-repo методы — `libs/core/src/acoustics/`:

- `sabine_eyring_engine.cpp`
- `image_source_engine.cpp`
- `ray_tracing_engine.cpp`
- `hybrid_engine.cpp`
- общий `rt_core.*`
- регистрация — `builtin_engines.cpp`

Каталог `engines/` зарезервирован под будущие standalone-модули.

---

## Как добавить свой метод?

1. Реализовать `IEngine` (`descriptor`, `validate`, `run`).
2. `REGISTER_ENGINE`.
3. Тесты (capability mismatch → fail validate).
4. Строка в `docs/engines.md`.

Метод появится в `GET /v1/engines` и GUI без правок UI. Dynamic DLL (C ABI) — P2.5, только по запросу.

---

## Как читать RT60 в metrics?

В результате три оценки на полосу:

| Ключ | Метод |
|------|--------|
| `schroederT30` | backward integration, фит −5…−35 дБ |
| `sabine` | `T = 0.161 · V / (S · ᾱ)` |
| `eyring` | с ln(1−ᾱ) |

Сравнивайте их как sanity-check: у rect-комнат они должны быть близки. `edt` — фит 0 → −10 дБ (ранний спад).

---

## Что за `disclaimer` в каталоге?

Каталог источников — данные **«как есть»**, без гарантии качества спеков. Каждая карточка и каталог целиком несут `disclaimer` (example specs, not verified vendor data). Custom-источник всегда работает offline.

---

## Почему runs / scenes пропали после перезапуска?

Store **in-memory**. Persistence (SQLite) — later. Перезапуск av-api чистит Scene Store, editor scenes и runs.

---

## Что не входит в v1?

Measurement/mic/phone pipeline · FDTD/волновые солверы, дифракция · real-time DSP 60 FPS · HRTF · multi-room · distributed compute · auth/multi-tenant · тяжёлый Study/diff · обязательный GPU · полный DCC-редактор.
