# Редактор помещений и mesh-система (V0.2)

Статус: **архитектура**. Реализация — по слайсам R6–R10.
Связано: [`plan-v0.1.1.md`](plan-v0.1.1.md) (готово), [`engines.md`](engines.md), `ARCHITECTURE.md`.

---

## 1. Зачем

Сейчас сцена приходит «извне»: rect-JSON или OBJ/glTF + gen-mesh. Для комфортной работы нужен **встроенный редактор**: стены/плоскости со свойствами, тела, mesh под алгоритмы, настройки генерации — и ясная модель данных под будущий UX.

**Опорные принципы (из видения продукта):**
- материал и поверхность — первоклассные сущности (α(f), направленность источников);
- одна сцена → разные engines (rect-ISM и mesh-RT);
- interactive (правки) и compute (часовые jobs) разделены;
- GUI не линкует C++ — только REST/WS.

---

## 2. Модель сцены (Scene Graph)

### 2.1. Сущности

```text
Scene
├── id, sceneVersion, units = meters
├── materials[]          # α(f), scattering, имя
├── entities[]
│   ├── Wall / Slab      # плоскость с толщиной
│   ├── SolidBox         # твердотельный бокс
│   ├── SolidExtrude     # выдавливание полигона (L-стена, ниша)
│   ├── Void             # вычитаемый объём (проём, окно)
│   └── Source / Receiver
└── meshSettings         # глобальные + per-entity overrides
```

### 2.2. Стены и плоскости

**Wall / Slab** — главный примитив редактора:

| Поле | Смысл |
|------|--------|
| `id`, `name` | идентификатор |
| `thickness` | м, обычно 0.1–0.5 |
| `outline` | 2D-полигон в плоскости стены **или** `start`/`end`/`height` (отрезок + высота) |
| `faces` | `inner` / `outer` / `side` → materialId |
| `openings[]` | двери/окна: rect в плоскости стены (void на фасаде) |

**SolidBox** — `origin`, `size`, `materialId` (все грани) или `faces`.  
**SolidExtrude** — polygon в XY + `height` (Z) или в плоскости стены.  
**Void** — box/polygon subtract; влияет на mesh (проёмы).

Все сущности имеют:
- `transform` (пока origin + axis-aligned; rotation — additive позже);
- `visible`, `locked` (для UX);
- `tags` (например `"acoustic-treatment"`).

### 2.3. Связь с текущим Scenario

V0.2 **не ломает** `scenario.schema.json`. Вводим **parallel document** `scene.schema.json`:

```json
{
  "schemaVersion": 1,
  "units": "m",
  "materials": [ ... ],
  "entities": [
    {
      "type": "wall",
      "id": "wall-north",
      "start": [0, 0, 0],
      "end": [6, 0, 0],
      "height": 2.8,
      "thickness": 0.2,
      "faces": { "inner": "mat-brick", "outer": "mat-brick", "side": "mat-plaster" },
      "openings": [{ "type": "door", "offset": 1.0, "width": 0.9, "height": 2.1, "sill": 0 }]
    }
  ],
  "sources": [...],
  "receivers": [...],
  "meshSettings": {
    "surface": { "algorithm": "earclip|advancing-front", "targetEdge": 0.2, "maxEdge": 0.5, "quality": "draft|standard|fine" },
    "volume": { "algorithm": "tet|hex-box", "targetSize": 0.15, "enabled": false }
  }
}
```

**Экспорт → Scenario** (compile step):
- `Scene` → (`rect` если все стены образуют короб без проёмов) **или** `mesh` (triangles + zones).
- Материалы и S/R копируются как есть.
- `meshSettings` не попадают в scenario; они — вход mesh-генератора.

---

## 3. Mesh-система

### 3.1. Два уровня

| Уровень | Назначение | Кто ест |
|---------|------------|---------|
| **Surface mesh** | треугольники границ | ray-tracing, hybrid, export OBJ/glTF |
| **Volume mesh** | тетры/гексы в объёме | будущие FDTD/modal (v1 — только каркас API) |

### 3.2. Surface mesh generation

**Вход:** Scene entities (CSG-lite: union solids, subtract voids, wall slabs).  
**Алгоритмы v1:**
1. **Explicit quads→triangles** — стены/боксы без CSG (быстро, стабильно, draft).
2. **Clip + ear-clip** — стены с opening’ами, L-extrude (reliable fallback).
3. **Advancing front / Delaunay** (v1.1) — если targetEdge сильно меньше размера комнаты.

**Настройки (`meshSettings.surface`):**

| Поле | Диапазон | Смысл |
|------|----------|--------|
| `algorithm` | auto / earclip / advancing-front | алгоритм |
| `targetEdge` | м | целевое ребро (опционально; явное значение важнее пресета) |
| `maxEdge` | м | жёсткий потолок (опционально; явное значение важнее пресета) |
| `quality` | draft / standard / fine | пресет → (targetEdge, maxEdge) |

Поля `preserveCorners` / `weldEpsilon` — **не в v1** (omit v1): явная топология без сглаживания углов и склейки вершин.

**Пресеты quality:**

| quality | targetEdge | maxEdge |
|---------|------------|---------|
| draft | 0.5 | 1.0 |
| standard | 0.2 | 0.5 |
| fine | 0.08 | 0.2 |

**Выход:** `SurfaceMesh { positions, triangles, zones[] }` — **тот же тип**, что `av::Mesh` (zone = material slot name). Пишется в OBJ/glTF.

### 3.3. Volume mesh (каркас)

```text
VolumeMeshSettings { algorithm: tet | hex-box, targetSize, enabled }
generateVolumeMesh(scene) → { cells, nodes, zones }  // stub v1
```

V0.2: **API + schema + box-only hex-box stub** (одна hex-ячейка на `SolidBox`; остальная геометрия и `algorithm: tet` — предупреждение и ноль ячеек). Полноценные tets — следующая итерация (TetGen/Netgen-подобное не тащим в v1).

### 3.4. Инспекция mesh

После генерации:
- статистика: verts, tris, cells, bbox, watertight?, euler-ish warnings;
- histogram edge lengths;
- preview wireframe (GUI) — из тех же буферов;
- per-entity «источник треугольников» (trace: triangle → entityId) для клика в 3D.

**Формат артефакта `mesh-report.json`:**
```json
{
  "kind": "surface",
  "stats": { "vertices": 0, "triangles": 0, "zones": ["floor", "walls"] },
  "bbox": [[0,0,0],[6,2.8,4]],
  "warnings": ["non-manifold edge at wall-north/openings/0"],
  "settingsUsed": { ... },
  "entityTriangleCount": { "wall-north": 120, "floor": 4 }
}
```

---

## 4. API (добавки к `/v1`)

| Method | Path | Назначение |
|--------|------|------------|
| GET/POST | `/v1/scenes` | Scene document (отличается от `/v1/scenarios`) |
| GET/PUT/PATCH | `/v1/scenes/{id}` | чтение / **замена всего документа** (v1); mesh при замене сбрасывается |
| POST | `/v1/scenes/{id}/mesh` | job `meshjob-…` → report; `?sync=1` — сразу |
| GET | `/v1/scenes/{id}/mesh` | состояние job + mesh-report (404 если не генерировали) |
| GET | `/v1/scenes/{id}/mesh.obj` | download OBJ |
| POST | `/v1/scenes/{id}/to-scenario` | compile Scene → Scenario (для `/v1/simulations`) |

**Примечания реализации (R9):**
- mesh-джобы **не** в `JobManager` (`/v1/jobs` — только симуляции); поллинг `/v1/scenes/{id}/mesh`.
- `to-scenario`: **rect**, если 4 стены без проёмов + 2 SolidBox-плиты пол/потолок (Z-up → Y-up); иначе **mesh** + `ray-tracing`.
- Floor/ceiling в v1 — `solid_box`-плиты, отдельного типа нет.

**WebSocket (interactive):** `scene.entity.patch` — move/resize wall, change material, openings.  
(Не путать с существующим `scene.patch` для sources — можно ввести `scene.op` union или отдельный op set. **Решение:** расширить `scene.patch` ops: `moveEntity`, `resizeEntity`, `setFaceMaterial`, `addOpening`.)

**Job:** mesh generation может быть долгим → `/v1/jobs` как у simulations (reuse JobManager).

---

## 5. UX-готовность (что готовим сейчас)

| Готовим в core/API | Потом в GUI |
|--------------------|-------------|
| Scene JSON, entities, materials | выделение wall/solid, gizmo resize |
| meshSettings schema + presets | панель «Mesh: algorithm, targetEdge, quality» |
| generate + mesh-report | предпросмотр wireframe, warnings в UI |
| entity→triangle trace | клик по грани в 3D → entity |
| to-scenario compile | кнопка «В расчёт» |

**Взаимодействие редактора (направление):**
1. Слева — дерево entities + материалы.
2. Центр — 3D (viewport V0.1.1 уже есть).
3. Справа — инспектор выбранной сущности (размеры, openings, material per face).
4. Bottom sheet — Mesh generator (настройки, Generate, статистика).

---

## 6. Слои кода

```text
libs/core/geometry   # MeshBuilder (есть), CSG-lite, wall/solid primitives, volume stub
libs/core/scene      # SceneEntity model (parsing-agnostic)
libs/io              # scene.json r/w, mesh-report.json, OBJ export (есть), to-scenario
libs/sim             # jobs for mesh generation (reuse)
apps/api             # /v1/scenes, /mesh, to-scenario
apps/cli             # gen-mesh → расширить: gen-scene / mesh-from-scene
gui/                 # слайсы после стабилизации API
```

Зависимости без циклов: `scene` types → `geometry` mesh → `io` → `api`.

---

## 7. Фазы реализации

| Слайс | Scope | Exit |
|-------|--------|------|
| **R6** | Scene model + schema + json r/w + tests | scene.json round-trip |
| **R7** | Primitives: wall/solid/void → SurfaceMesh (earclip/quads), zones | mesh from wall + opening |
| **R8** | meshSettings presets + mesh-report + CLI `av-cli mesh scene.json` | report + obj |
| **R9** | API scenes + generate mesh job + to-scenario | REST smoke |
| **R10** | Docs (RU/EN) + ru-check + gui stubs (Mesh panel read-only) | документация вычитана |

**Не в V0.2:** full CSG, boolean-склейка общего вида, tets от внешних mesher’ов, GUI-редактор «как в CAD».

---

## 8. Риски

| Риск | Мера |
|------|------|
| CSG-дыры (openings, L-rooms) | earclip fallback + mesh-report warnings |
| Смешение Scenario / Scene | явный `to-scenario`, разные id-пространства |
| Долгий meshing | JobManager + draft quality по умолчанию |
| Несовпадение единиц | только meters; import-конвертация как сейчас |
| UX-перегруз | v1 редактор = стены + боксы + проёмы; остальное постепенно |

---

## 9. Словарь

| Термин | Определение |
|--------|-------------|
| **Scene** | Документ редактора (entities, materials, meshSettings) |
| **Scenario** | Вход симуляции (room mesh/rect, S/R, params) |
| **Wall / Slab** | Плоскость с толщиной и материалами граней |
| **Solid** | Объёмное тело (box, extrude) |
| **Void** | Вычитаемый объём (проём) |
| **Surface mesh** | Треугольники границ |
| **Volume mesh** | Клетки в объёме (tet/hex) |
| **Zone** | Именованный материал-слот на mesh |
| **mesh-report** | Статистика и warnings после генерации |
