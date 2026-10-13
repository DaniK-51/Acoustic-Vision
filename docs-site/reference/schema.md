# JSON Schema

Формальные контракты — в каталоге `schemas/` репозитория. Сайт пересказывает поля; при расхождении побеждает файл схемы.

| Файл | Документ | Кто читает |
|------|----------|------------|
| `scenario.schema.json` | Scenario — вход симуляции | `av-cli run`, `POST /v1/scenarios`, `POST /v1/simulations` |
| `scene.schema.json` | Scene — документ редактора | `av-cli mesh`, `POST /v1/scenes` |
| `engine-descriptor.schema.json` | EngineDescriptor | `GET /v1/engines`, `av-cli engines` |
| `source-catalog.schema.json` | Каталог источников | `GET /v1/source-catalog` |

Все документы несут `schemaVersion` (integer ≥ 1), где он предусмотрен схемой. Единицы — метры; конвертация только при импорте.

## Scenario

Обязательные ключи: `room`, `materials`, `params`.

```json
{
  "schemaVersion": 1,
  "name": "demo",
  "room": {
    "type": "rect",
    "size": { "w": 6, "h": 2.8, "d": 4.5 },
    "faces": {
      "floor": "mat-carpet", "ceiling": "mat-plaster",
      "x+": "mat-brick", "x-": "mat-brick", "z+": "mat-brick", "z-": "mat-brick"
    }
  },
  "materials": [
    {
      "id": "mat-carpet",
      "name": "Carpet on concrete",
      "bandsHz": [250, 500, 1000, 2000, 4000],
      "absorption": [0.10, 0.25, 0.35, 0.40, 0.50],
      "scattering": 0.1
    }
  ],
  "sources": [
    { "id": "src-1", "position": [1.2, 1.5, 1.0], "powerDb": 80.0 }
  ],
  "receivers": [
    { "id": "rcv-1", "position": [3.0, 1.5, 2.0] }
  ],
  "params": {
    "engine": "image-source",
    "maxReflectionOrder": 3,
    "sampleRate": 48000,
    "durationSec": 1.5,
    "speedOfSound": 343,
    "capturePaths": false,
    "signedIr": false,
    "fieldGrid": { "enabled": false, "nx": 16, "ny": 8, "nz": 16 }
  }
}
```

### room

| Поле | Смысл |
|------|-------|
| `type` | `rect` или `mesh` |
| `size` | rect: `w`, `h`, `d` (> 0); **y = высота** |
| `faces` | rect: материал грани — `floor`, `ceiling`, `x+`, `x-`, `z+`, `z-` |
| `mesh` | mesh: `uri`, `unit` (`m`/`mm`/`cm`/`in`), `scale` (> 0), `materialOverrides` (имя зоны → materialId) |

### materials[]

| Поле | Обязательно | Смысл |
|------|-------------|-------|
| `id` | да | ключ; на него ссылаются `faces` / `materialOverrides` |
| `absorption` | да | α(f) ∈ [0, 1] по полосам, **энергетическое** поглощение |
| `name` | нет | отображаемое имя |
| `bandsHz` | нет | полосы материала; без них — таблица сценария / default |
| `scattering` | нет | [0, 1]; RT-методы, ISM v1 игнорирует |

### sources[] / receivers[]

| Поле | Обязательно | Смысл |
|------|-------------|-------|
| `id` | да | уникальный ключ |
| `position` | да | `[x, y, z]` |
| `powerDb` | нет | dB SPL @ 1 м (см. [Источники](/guide/sources)) |
| `orientation` | нет | `{ yawDeg, pitchDeg }` |
| `directivity` | нет | `{ type, description?, horizontal?, vertical? }` |

`polarCurve` (`horizontal` / `vertical`): `freqsHz[]`, `anglesDeg[]`, `gainDb[][]` — по строке на частоту, длина строки = числу углов.

### params

| Поле | Смысл |
|------|-------|
| `engine` | id метода (обязательно) |
| `maxReflectionOrder` | 1–32, порядок отражений (ISM) |
| `sampleRate` | Гц, ≥ 1000 (IR) |
| `durationSec` | длительность IR (> 0) |
| `bandsHz` | полосы расчёта |
| `speedOfSound` | м/с (> 0), default 343 |
| `capturePaths` | сохранять полилинии лучей |
| `airAbsorption` | поглощение в воздухе |
| `signedIr` | IR со знаком (иначе energy envelope) |
| `methodParams` | параметры конкретного engine (см. его `paramsSchema`) |
| `fieldGrid` | `{ enabled, nx, ny, nz }`; размерности 1–64 |

## Scene

Обязательные ключи: `schemaVersion`, `units`, `materials`, `entities`. `units` в v1 — только `"m"`.

```text
Scene
├── schemaVersion, id?, name?, units = "m"
├── materials[]          # как в Scenario
├── entities[]
│   ├── wall             # стена: start, end, height, thickness, faces, openings[]
│   ├── solid_box        # origin, size, materialId
│   ├── solid_extrude    # polygon (XY) + height (Z)
│   ├── void             # вычитаемый объём
│   └── source / receiver
├── sources[] / receivers[]   # раскладки source/receiver (как в Scenario)
└── meshSettings         # surface + volume
```

Отличия от Scenario: **Z-up** (высота по Z), геометрия — entities, mesh-настройки входят в документ. Компиляция `to-scenario` переводит Z-up → Y-up. Поля и примеры — [Редактор сцен](/guide/scene-editor).

### meshSettings

| Блок | Ключи |
|------|-------|
| `surface` | `algorithm` (`auto`/`earclip`/`advancing-front`), `targetEdge`, `maxEdge`, `quality` (`draft`/`standard`/`fine`) |
| `volume` | `algorithm` (`tet`/`hex-box`), `targetSize`, `enabled` |

Явные `targetEdge` / `maxEdge` важнее пресета quality.

## EngineDescriptor

Обязательные: `id`, `name`, `version`, `caps`.

| Поле | Смысл |
|------|-------|
| `caps` | `supportsRect`, `supportsMesh`, `supportsRayPaths`, `supportsBands`, `maxOrder` (≥ 0) |
| `paramsSchema` | JSON Schema для `methodParams` |
| `artifacts` | subset из `ir`, `metrics`, `field`, `rays`, `findings` (уникальные) |

Пример — [Методы расчёта](/guide/engines).

## Source Catalog

Обязательные: `disclaimer`, `sources`. У каждой карточки обязательны `id`, `name`, `powerDb`, `spectrum`, `directivity`, `disclaimer`.

| Поле карточки | Смысл |
|---------------|-------|
| `manufacturer` | опционально |
| `category` | свободная группировка |
| `powerDb` | dB SPL @ 1 м на оси (примерные значения) |
| `spectrum` | `bandsHz[]` + `responseDb[]` (относительные смещения) |
| `directivity` | `type` + опциональные `horizontal` / `vertical` polarCurve |
| `notes`, `sourceUrl` | опционально |

Disclaimer на уровне каталога и на уровне карточки обязателен: данные — факт-ориентированные примеры, не проверенные вендорские спецификации.

## Что не покрывают схемы

Валидация «за схемой» живёт в коде `validate()` engines и `validateScene()`:

- тип комнаты под конкретный engine (rect vs mesh);
- нахождение source/receiver внутри комнаты (image-source);
- согласованность длин `absorption` / `bandsHz`;
- non-manifold mesh — warnings в mesh-report, не hard fail.

Схемы валидируют **форму** документа; семантику — движки.
