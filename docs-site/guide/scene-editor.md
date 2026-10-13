# Редактор сцен (V0.2)

Встроенный редактор описывает геометрию помещения как **Scene** — параллельный к Scenario документ (`schemas/scene.schema.json`). Из Scene получают surface mesh и компилируют Scenario для расчёта.

## Зачем отдельный Scenario

Сегодня сценарий часто приходит «извне»: rect-JSON или OBJ/glTF. Редактор даёт:

- стены/плоскости со свойствами и проёмами;
- тела (solid) и вычитаемые объёмы (void);
- mesh-настройки под алгоритмы;
- compile Scene → Scenario для `/v1/simulations`.

V0.2 **не ломает** `scenario.schema.json`.

## Модель сцены

```text
Scene
├── id, sceneVersion, units = meters
├── materials[]          # α(f), scattering, имя
├── entities[]
│   ├── wall / slab      # плоскость с толщиной
│   ├── solid_box        # твердотельный бокс
│   ├── solid_extrude    # выдавливание полигона
│   ├── void             # вычитаемый объём (проём, окно)
│   └── source / receiver
└── meshSettings         # глобальные + per-entity overrides
```

**Единицы:** только метры (`units: "m"`). Конвертация — только при импорте.

## Стены и проёмы

**Wall / Slab** — главный примитив:

| Поле | Смысл |
|------|--------|
| `id`, `name` | идентификатор |
| `thickness` | м, обычно 0.1–0.5 |
| `start` / `end` | отрезок в 3D (основание стены) |
| `height` | высота стены |
| `faces` | `inner` / `outer` / `side` → materialId |
| `openings[]` | двери/окна в плоскости стены |

Пример:

```json
{
  "type": "wall",
  "id": "wall-north",
  "start": [0, 0, 0],
  "end": [6, 0, 0],
  "height": 2.8,
  "thickness": 0.2,
  "faces": {
    "inner": "mat-brick",
    "outer": "mat-brick",
    "side": "mat-plaster"
  },
  "openings": [
    { "type": "door", "offset": 1.0, "width": 0.9, "height": 2.1, "sill": 0 }
  ]
}
```

### Opening

| Поле | Смысл |
|------|--------|
| `type` | `door` / `window` (в схеме) |
| `offset` | смещение вдоль стены от `start` |
| `width`, `height` | размер проёма |
| `sill` | высота подоконника / низа проёма от пола |

## Solid и void

| Тип | Поля | Назначение |
|-----|------|------------|
| `solid_box` | `origin`, `size`, `materialId` | объёмный бокс (мебель, плита пола/потолка) |
| `solid_extrude` | `polygon` (XY) + `height` (Z) | L-стена, ниша |
| `void` | `origin`, `size` | вычитаемый объём (проём в теле) |

Все сущности имеют `transform` (пока origin + axis-aligned; rotation — позже), `visible`, `locked`, `tags`.

## Z-up vs Y-up

| Документ | Вверх | Почему |
|----------|-------|--------|
| **Scene** (редактор) | **Z** | классический CAD/DCC, extrude идёт по Z |
| **Scenario** (симуляция) | **Y** | `room.size.h` — высота; так же в Three.js / GUI viewport |

Компиляция `to-scenario` переводит Z-up → Y-up автоматически. Если пишете Scenario руками — помните: `position: [x, y, z]` с **y = высота**.

## Mesh-настройки

```json
"meshSettings": {
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

Пресеты quality:

| quality | targetEdge | maxEdge |
|---------|------------|---------|
| draft | 0.5 | 1.0 |
| standard | 0.2 | 0.5 |
| fine | 0.08 | 0.2 |

Явные `targetEdge` / `maxEdge` важнее пресета. Подробнее — [Mesh](/guide/mesh).

## to-scenario (компиляция)

`POST /v1/scenes/{id}/to-scenario` (или эквивалент в CLI):

- **rect**, если 4 стены без проёмов + 2 SolidBox-плиты пол/потолок (Z-up → Y-up);
- иначе **mesh** + `ray-tracing` (surface mesh пишется во временный OBJ, uri абсолютный).

Материалы и sources/receivers копируются как есть. `meshSettings` в Scenario не попадают — это вход генератора.

Floor/ceiling в v1 — `solid_box`-плиты, отдельного типа нет.

## API редактора

| Метод | Путь | Назначение |
|-------|------|------------|
| POST | `/v1/scenes` | создать Scene → `sceneId`, `sceneVersion` |
| GET | `/v1/scenes/{id}` | чтение документа |
| PUT / PATCH | `/v1/scenes/{id}` | **замена всего документа** (v1); mesh сбрасывается |
| POST | `/v1/scenes/{id}/mesh` | генерация mesh (`?sync=1` — сразу) |
| GET | `/v1/scenes/{id}/mesh` | состояние job + mesh-report |
| GET | `/v1/scenes/{id}/mesh.obj` | скачать OBJ |
| POST | `/v1/scenes/{id}/to-scenario` | compile → Scenario JSON |

Mesh-джобы **не** живут в `/v1/jobs` (там только симуляции) — поллите `GET /v1/scenes/{id}/mesh`.

## CLI

```bash
av-cli mesh scene.json -o out.obj --report report.json --quality standard
```

См. [CLI](/reference/cli).

## UX-направление (GUI)

| В core/API | В GUI |
|------------|-------|
| Scene JSON, entities, materials | выделение wall/solid, gizmo resize |
| meshSettings + presets | панель «Mesh: algorithm, targetEdge, quality» |
| generate + mesh-report | wireframe-preview, warnings |
| entity→triangle trace | клик по грани → entity |
| to-scenario | кнопка «В расчёт» |

Планируемая раскладка: слева дерево entities + материалы; в центре 3D; справа инспектор; снизу mesh-генератор.

## Не в V0.2

- полный CSG и boolean-склейка общего вида;
- tets от внешних mesher’ов;
- GUI-редактор «как в CAD»;
- поля `preserveCorners` / `weldEpsilon` (omit v1).
