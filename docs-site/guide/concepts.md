# Концепции

Короткий словарь и модель данных: что такое Scenario и Scene, какие артефакты выдают engines и как читать метрики.

## Scenario vs Scene

| | **Scenario** | **Scene** |
|---|--------------|-----------|
| Назначение | вход симуляции | документ редактора |
| Схема | `schemas/scenario.schema.json` | `schemas/scene.schema.json` |
| Комната | `room`: `rect` или `mesh` | entities: `wall`, `solid_box`, `solid_extrude`, `void` |
| API | `/v1/scenarios`, `/v1/simulations` | `/v1/scenes`, mesh, `to-scenario` |
| Система координат | Y-up (`h` — высота) | Z-up (высота по Z) |
| Материалы | inline `materials[]` | inline `materials[]` |
| Источники / приёмники | `sources[]`, `receivers[]` | `sources[]`, `receivers[]` |

**Scenario** — единица работы расчёта: комната, материалы, источники, приёмники, `params`.  
**Scene** — документ встроенного редактора: стены с проёмами, тела, void, mesh-настройки. Чтобы запустить расчёт, Scene компилируется в Scenario через `to-scenario` (или `av-cli` pipeline).

V0.2 не ломает `scenario.schema.json` — это параллельный документ.

## Комната

### Rect

Прямоугольная комната `w × h × d` (метры) с материалами граней:

| Грань | Ключ |
|-------|------|
| пол | `floor` |
| потолок | `ceiling` |
| +X / −X | `x+`, `x-` |
| +Z / −Z | `z+`, `z-` |

Подходит для `sabine-eyring` и `image-source`.

### Mesh

Произвольная геометрия (OBJ/glTF):

```json
"mesh": {
  "uri": "room.obj",
  "unit": "m",
  "scale": 1.0,
  "materialOverrides": { "walls": "mat-brick" }
}
```

`uri` резолвится:

| Инструмент | База |
|------------|------|
| `av-cli` | каталог scenario-файла |
| `av-api` | CWD процесса |

Подходит для `ray-tracing` и `hybrid`.

## Материалы

```json
{
  "id": "mat-carpet",
  "name": "Carpet on concrete",
  "bandsHz": [250, 500, 1000, 2000, 4000],
  "absorption": [0.10, 0.25, 0.35, 0.40, 0.50],
  "scattering": 0.1
}
```

| Поле | Смысл |
|------|--------|
| `absorption` | α(f) ∈ [0, 1] по октавным полосам — **энергетическое** поглощение |
| `bandsHz` | полосы; если не заданы — берётся таблица сценария / default |
| `scattering` | коэффициент рассеяния; используют RT-методы, ISM v1 игнорирует |

## Источники и приёмники

**Источник:** `id`, `position`, `powerDb`, опционально `orientation` (yaw/pitch), `directivity` (omni / dipole / cardioid / custom + polar-кривые).  
**Приёмник:** `id`, `position` — точка наблюдения.

Мощность в `powerDb` — уровень в dB SPL @ 1 м (не электрические ватты). Подробнее — [Источники](/guide/sources).

## Параметры расчёта (`params`)

| Поле | Смысл |
|------|--------|
| `engine` | id метода: `sabine-eyring`, `image-source`, `ray-tracing`, `hybrid` |
| `maxReflectionOrder` | порядок отражений (1–32), для ISM |
| `sampleRate` | Гц, для IR |
| `durationSec` | длительность IR |
| `bandsHz` | полосы расчёта |
| `speedOfSound` | м/с, default 343 |
| `capturePaths` | сохранять полилинии лучей |
| `airAbsorption` | поглощение в воздухе |
| `signedIr` | IR со знаком (иначе energy envelope) |
| `fieldGrid` | `{ enabled, nx, ny, nz }` — сетка пространственного поля |
| `methodParams` | параметры конкретного engine (см. его `paramsSchema`) |

## Engines (методы расчёта)

Engine — реализация метода за `IEngine`: `descriptor()`, `validate()`, `run()`. Регистрация — `REGISTER_ENGINE`.

| id | Модель | Комната | Артефакты |
|----|--------|---------|-----------|
| `sabine-eyring` | аналитика RT60 | rect | `metrics` |
| `image-source` | зеркальные источники | rect | `metrics`, `ir` |
| `ray-tracing` | лучи + BVH | mesh | `metrics`, `ir`, `rays`, `field` |
| `hybrid` | RT-проход, early/late split | mesh | `metrics`, `ir`, `rays`, `field` |

Подробно — [Методы расчёта](/guide/engines).

## Артефакты (Result)

Единый контракт результата. Ключи появляются только если engine их выдал.

| Артефакт | Что это |
|----------|---------|
| `metrics` | RT60 (Schroeder / Sabine / Eyring), EDT, early reflections |
| `ir` | импульсный отклик (`IrBuffer`); экспорт — WAV |
| `field` | `SpatialField` — энергия на 3D-гриде |
| `rays` | `paths[]` — полилинии лучей с амплитудой и порядком |
| `findings` | опциональные флаги анализа |

### IR (Impulse Response)

Многоканальный broadband-буфер: канал на пару `source × receiver`, имя `srcId:rcvId`. Задержка `t = r / c`, амплитуда `∝ 1/r`, отскок amplitude `× √(1-α)`.

### Поле (`field`)

`nx, ny, nz`, `origin`, `cellSize`, `bandHz` (0 = broadband), линейные `values` с индексом `x + nx*(y + ny*z)`. Хранится в `unit: "energy"`; пересчёт в дБ — на границе API/UI.

### Лучи (`rays`)

`paths[]`: `{ points, amplitude, order }`. Появляются при `capturePaths: true` и `supportsRayPaths` у engine. GUI рисует не более 64 линий.

## Метрики

| Метрика | Определение |
|---------|-------------|
| **RT60 Schroeder** | backward integration, T30: линейный фит −5…−35 дБ |
| **RT60 Sabine** | `T = 0.161 · V / (S · ᾱ)` |
| **RT60 Eyring** | с поправкой на ln(1−ᾱ) |
| **EDT** | Early Decay Time, фит 0 → −10 дБ |
| **EarlyReflections** | top-K пиков в первые 80 мс: `t`, `amp`, `order` |

## Bands (частотные полосы)

Default — октавные: 125, 250, 500, 1000, 2000, 4000 Гц (`GET /v1/bands`). Расширение до 63 Гц / 8 кГц идёт через `bandsHz` в материале и params. Симуляция считает **по полосам**; линейная интерполяция — только для отображения.

## Run / история запусков

Один **run** = scenario + engine + params + Result. История — append-only список (новые сверху). UI может показать два run’а визуально рядом.

| Поле записи | Смысл |
|-------------|--------|
| `id` (`runId`) | ключ истории, `run-…` |
| `jobId` | handle артефактов (`/v1/jobs/{jobId}/…`) |
| `engine`, `scenarioName`, `sceneVersion` | контекст прогона |
| `submittedAt`, `finishedAt`, `state` | жизненный цикл |
| `paramsDigest` | engine + ключевые sim-параметры |

`GET /v1/runs/{id}` принимает **либо `runId`, либо `jobId`**.

## Два режима

| Режим | Когда | Протокол | Результат |
|-------|-------|----------|-----------|
| **Interactive** | drag, правка сцены | WS `scene.patch` | Scene Store на бэке |
| **Compute** | кнопка «Расчёт» | `POST /simulations` → job | полный Result |

Интерактив **не** обязан пересчитывать акустику за 1 с. Тяжёлые методы — отдельной кнопкой, job может идти часы.
