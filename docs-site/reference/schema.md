# JSON Schema

Канонические контракты лежат в `schemas/` репозитория. C++-структуры зеркалят их; runtime-валидатора-библиотеки пока нет — форму покрывают юнит-тесты, смысл — `validate()` engine.

| Файл | Документ |
|------|----------|
| `scenario.schema.json` | вход симуляции |
| `scene.schema.json` | документ редактора (V0.2) |
| `source-catalog.schema.json` | каталог источников |
| `engine-descriptor.schema.json` | описание метода расчёта |

---

## Scenario

`title: Acoustic-Vision Scenario`

| Поле | Тип | Обязательно | Смысл |
|------|-----|-------------|--------|
| `schemaVersion` | integer ≥ 1 | — | версия формата |
| `name` | string | — | имя прогона/сценария |
| `room` | object | да (по type) | комната |
| `materials[]` | array | — | материалы |
| `sources[]` | array | — | источники |
| `receivers[]` | array | — | приёмники |
| `params` | object | да (`engine`) | параметры расчёта |

### room

| Поле | Тип | Смысл |
|------|-----|--------|
| `type` | `rect` \| `mesh` | вид комнаты |
| `size` | `{w,h,d}` > 0 | размеры rect |
| `faces` | `floor`, `ceiling`, `x+`, `x-`, `z+`, `z-` → materialId | материалы граней |
| `mesh` | object | mesh-комната |

**mesh:**

| Поле | Тип | Смысл |
|------|-----|--------|
| `uri` | string | OBJ/glTF; резолвинг — см. [FAQ](/faq) |
| `unit` | `m`\|`mm`\|`cm`\|`in` | единицы файла (default m) |
| `scale` | number > 0 | масштаб |
| `materialOverrides` | map slot → materialId | материалы слотов |

### materials[]

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `id` | string | да |
| `name` | string | — |
| `bandsHz[]` | number > 0 | — |
| `absorption[]` | number [0,1] | да |
| `scattering` | number [0,1] | — |

### sources[]

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `id` | string | да |
| `position` | `[x,y,z]` | да |
| `powerDb` | number | — |
| `orientation` | `{yawDeg?, pitchDeg?}` | — |
| `directivity` | object | — |

### receivers[]

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `id` | string | да |
| `position` | `[x,y,z]` | да |

### params

| Поле | Тип | Ограничения |
|------|-----|-------------|
| `engine` | string | обязательно |
| `maxReflectionOrder` | integer | 1…32 |
| `sampleRate` | integer | ≥ 1000 |
| `durationSec` | number | > 0 |
| `bandsHz[]` | number > 0 | |
| `speedOfSound` | number > 0 | |
| `capturePaths` | boolean | |
| `airAbsorption` | boolean | |
| `signedIr` | boolean | |
| `methodParams` | object | по paramsSchema engine |
| `fieldGrid` | object | `enabled`, `nx/ny/nz` 1…64 |

### directivity

| Поле | Тип |
|------|-----|
| `type` | `omni`\|`dipole`\|`cardioid`\|`custom` |
| `description` | string |
| `horizontal` / `vertical` | polarCurve |

### polarCurve

| Поле | Тип | Форма |
|------|-----|--------|
| `freqsHz[]` | number > 0 | N полос |
| `anglesDeg[]` | number | M углов |
| `gainDb[][]` | number | N строк × M столбцов |

---

## Scene

`title: Acoustic-Vision Scene`

| Поле | Тип | Смысл |
|------|-----|--------|
| `schemaVersion` | integer | версия |
| `id` | string | id документа |
| `units` | `m` | только метры |
| `materials[]` | material | как в scenario |
| `entities[]` | union | wall / solid_box / solid_extrude / void / source / receiver |
| `sources[]`, `receivers[]` | array | тоже есть в документе |
| `meshSettings` | object | генерация mesh |

### wall

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `type` | `"wall"` | да |
| `id`, `start`, `end`, `height`, `thickness`, `faces` | | да |
| `name`, `openings[]` | | — |

`faces`: `inner`, `outer`, `side` → materialId.

### opening

| Поле | Смысл |
|------|--------|
| `type` | door / window |
| `offset` | смещение вдоль стены |
| `width`, `height` | размер |
| `sill` | низ проёма от пола |

### solid_box / solid_extrude / void

| Тип | Поля |
|-----|------|
| `solid_box` | `origin`, `size`, `materialId` |
| `solid_extrude` | `polygon` (XY), `height` (Z), `materialId` |
| `void` | `origin`, `size` |

### source / receiver (entities)

```json
{ "type": "source", "id": "src-1", "name": "…", "position": [x,y,z], "powerDb": 90 }
{ "type": "receiver", "id": "rcv-1", "position": [x,y,z] }
```

Позиции в Scene — **Z-up**.

### meshSettings

```json
{
  "surface": {
    "algorithm": "auto|earclip|advancing-front",
    "targetEdge": 0.2,
    "maxEdge": 0.5,
    "quality": "draft|standard|fine"
  },
  "volume": {
    "algorithm": "tet|hex-box",
    "targetSize": 0.15,
    "enabled": false
  }
}
```

Пресеты quality: draft (0.5 / 1.0), standard (0.2 / 0.5), fine (0.08 / 0.2).

---

## Source catalog

`title: Acoustic-Vision Source Catalog`

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `schemaVersion` | integer | — |
| `disclaimer` | string | да (каталог) |
| `sources[]` | entry | — |

### entry

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `id`, `name` | string | да |
| `manufacturer` | string | — |
| `category` | string | — |
| `powerDb` | number | да |
| `spectrum` | `{bandsHz[], responseDb[]}` | — |
| `directivity` | object | — |
| `notes`, `sourceUrl` | string | — |
| `disclaimer` | string | да |

`spectrum.responseDb` — относительные сдвиги по полосам (АЧХ), dB.

---

## Engine Descriptor

`title: Acoustic-Vision Engine Descriptor`

| Поле | Тип | Обязательно |
|------|-----|-------------|
| `id` | string | да |
| `name` | string | да |
| `version` | string | да |
| `caps` | object | да |
| `paramsSchema` | object (JSON Schema) | — |
| `artifacts[]` | enum subset | — |

### caps

| Поле | Тип | Смысл |
|------|-----|--------|
| `supportsRect` | boolean | rect-комнаты |
| `supportsMesh` | boolean | mesh-комнаты |
| `supportsRayPaths` | boolean | лучи в результате |
| `supportsBands` | boolean | работа по полосам |
| `maxOrder` | integer ≥ 0 | 0 = неприменимо |

### artifacts

Допустимые значения (unique): `ir`, `metrics`, `field`, `rays`, `findings`.

---

## Result (wire)

Не отдельный schema-файл, но контракт `resultToJson` / `GET /v1/jobs/{id}/result`:

| Ключ | Появляется | Поля |
|------|------------|------|
| `metrics` | всегда у большинства engines | `rt60` {band: {schroederT30, sabine, eyring}}, `edt`, `earlyReflections[]` |
| `ir` | engine с `ir` | `sampleRate`, `durationSec`, `channels`, `channelNames`, `bandsHz` |
| `field` | engine с `field` + `fieldGrid` | `nx,ny,nz`, `origin`, `cellSize`, `bandHz`, `unit: "energy"`, `values` |
| `rays` | engine с `rays` + `capturePaths` | `paths[]`: `{points, amplitude, order}` |

Индекс поля: `x + nx*(y + ny*z)`.
