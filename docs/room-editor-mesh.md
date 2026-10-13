# Редактор помещений и mesh-система (V0.2)

Статус: **реализовано R6–R9** (scene model, mesher, CLI `mesh`, REST `/v1/scenes`); R10 — документация. GUI-слайсы — после стабилизации API.
Связано: [`plan-v0.1.1.md`](plan-v0.1.1.md) (готово), [`plan-v0.2.md`](plan-v0.2.md), [`engines.md`](engines.md), `ARCHITECTURE.md`.

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
Scene (документ)
├── id, schemaVersion, units = meters   # sceneVersion выдаёт API при create/replace
├── materials[]          # α(f), scattering, имя
├── entities[]
│   ├── Wall / Slab      # плоскость с толщиной
│   ├── SolidBox         # твердотельный бокс
│   ├── SolidExtrude     # выдавливание полигона (L-стена, ниша)
│   ├── Void             # вычитаемый объём (проём, окно)
│   └── Source / Receiver
└── meshSettings         # глобальные (per-entity overrides — позже)
```

### 2.2. Стены и плоскости

**Wall / Slab** — главный примитив редактора:

| Поле | Смысл |
|------|--------|
| `id`, `name` | идентификатор |
| `thickness` | м, обычно 0.1–0.5 |
| `start` / `end` / `height` | отрезок baseline + высота (v1; вариант `outline`-полигоном — позже) |
| `faces` | `inner` / `outer` / `side` → materialId |
| `openings[]` | двери/окна: rect в плоскости стены (void на фасаде) |

**SolidBox** — `origin`, `size`, `materialId` (все грани).  
**SolidExtrude** — polygon в XY + `height` (выдавливание вдоль +Z).  
**Void** — box subtract; влияет на mesh (проёмы).

Следующие поля — **не в schema v1** (omit, UX позже): `transform` (rotation), `visible`, `locked`, `tags`.

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
    "surface": { "algorithm": "auto|earclip|advancing-front", "targetEdge": 0.2, "maxEdge": 0.5, "quality": "draft|standard|fine" },
    "volume": { "algorithm": "tet|hex-box", "targetSize": 0.15, "enabled": false }
  }
}
```

**Экспорт → Scenario** (compile step, `scenarioFromScene` / `POST …/to-scenario`):
- Материалы и S/R копируются как есть; `meshSettings` не попадают в scenario (вход mesh-генератора). Дефолты params: `sampleRate` 48000, `durationSec` 1.0, стандартные `bandsHz`, `speedOfSound`.
- **rect** — если сцена распознана как замкнутая коробка (closed box):
  - ровно **4 стены**: axis-aligned, без проёмов, одинаковая `height`, baseline’ы на `z = 0`, каждая стена покрывает ровно одну сторону прямоугольника;
  - ровно **2 `solid_box`-плиты — пол и потолок**: XY-футпринт плиты точно совпадает с прямоугольником стен; пол касается `z = 0` (верх плиты ≠ `height`), потолок касается `z = height` (низ ≠ 0); полновысотный solid или дубль роли — не коробка;
  - иначе (отверстия, лишняя геометрия, `void` / `solid_extrude`, не та плитка) — **mesh**-fallback.
- **mesh** — всё остальное: `generateSurfaceMesh` пишется в OBJ (API — во временный `av-scene-<id>.obj`, uri абсолютный), `room.mesh.uri`, `unit: "m"`, `scale: 1.0`; materialOverrides не нужны — слоты mesh уже несут material id. Позиции остаются в исходной системе координат.
- **Z-up vs Y-up.** Редактор сцены — **Z-up** (высота стены вдоль `+Z`); `RectRoom` у image-source — **Y-up** (`h` вдоль `+y`, `d` вдоль `+z`). Ветка rect переносит точки: `(x, y, z) → (x − minX, z, y − minY)` (минимальный угол коробки в начало координат), грани: `faces.inner` стены → `x+`/`x-`/`z+`/`z-` по положению стены, `materialId` плит → `floor`/`ceiling`. Ветка mesh переноса нет — Z-up сохраняется (совпадает с ray-tracing / `generateSurfaceMesh`).
- Дефолтный engine: rect → `image-source` (`maxReflectionOrder` 10), mesh → `ray-tracing`.

---

## 3. Mesh-система

### 3.1. Два уровня

| Уровень | Назначение | Кто ест |
|---------|------------|---------|
| **Surface mesh** | треугольники границ | ray-tracing, hybrid, export OBJ/glTF |
| **Volume mesh** | тетры/гексы в объёме | будущие FDTD/modal (v1 — только каркас API) |

### 3.2. Surface mesh generation

**Вход:** Scene entities (CSG-lite: union solids, subtract voids, wall slabs).  
**Алгоритмы v1 (как построено):**
1. **Explicit quads→triangles** — стены/боксы: slab стены без проёмов = 6 quads → 12 tris, явная топология.
2. **Grid split** — opening’и и void-дыры: inner/outer грани стены режутся в квады вокруг отверстия (reveal/jamb-граней нет; для `sill=0` двери нижний кап остаётся под дырой — warning).
3. **Ear-clip** — капы `solid_extrude` (CW-полигон разворачивается в CCW); fan-fallback с warning.
4. **Advancing front / Delaunay** — **не реализован** в v1: `algorithm: "advancing-front"` отвечает warning и падает на explicit/earclip (v1.1).

**Настройки (`meshSettings.surface`):**

| Поле | Диапазон | Смысл |
|------|----------|--------|
| `algorithm` | auto / earclip / advancing-front | алгоритм (v1: см. выше) |
| `targetEdge` | м | целевое ребро (опционально; явное значение важнее пресета) |
| `maxEdge` | м | жёсткий потолок (опционально; явное значение важнее пресета) |
| `quality` | draft / standard / fine | пресет → (targetEdge, maxEdge) |

В v1 `targetEdge` / `maxEdge` / `quality` **резолвятся** в `settingsUsed` (mesh-report) и CLI `--quality`, но **не измельчают** топологию — mesh строится явными quads/tris. Это residual R7/R8 (см. [`plan-v0.2.md`](plan-v0.2.md)).

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
- статистика: verts, tris, zones, bbox;
- warnings (отверстия без jamb, void мимо стен, вырожденные сущности, fallback’и);
- per-entity «источник треугольников» (trace: triangle → entityId) для клика в 3D;
- preview wireframe (GUI, позже) — из тех же буферов.

Не в v1-отчёте (позже): histogram edge lengths, watertight/euler-проверки.

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
| POST | `/v1/scenes` | создать Scene document → `201 {sceneId, sceneVersion}` (отличается от `/v1/scenarios`) |
| GET | `/v1/scenes/{id}` | `{sceneId, sceneVersion, scene}`; списка `GET /v1/scenes` в v1 нет |
| PUT/PATCH | `/v1/scenes/{id}` | **замена всего документа** (оба глагола = одно действие); `sceneVersion` +1, mesh/report сбрасываются |
| POST | `/v1/scenes/{id}/mesh` | генерация: `202 {sceneId, jobId, state}` (async) или `?sync=1` → сразу статус + report |
| GET | `/v1/scenes/{id}/mesh` | состояние mesh-job + mesh-report (404 «mesh not generated», если не генерировали) |
| GET | `/v1/scenes/{id}/mesh.obj` | download OBJ (`model/obj`) |
| POST | `/v1/scenes/{id}/to-scenario` | compile Scene → Scenario JSON (для `/v1/simulations`) |

**Примечания реализации (R9):**
- Хранилища разные: `EditorSceneStore` (`scene-…` для редактора) и Scenario Store (`sceneId` из `/v1/scenarios`) — id действителен только в своём store, перекрёстный lookup → 404. Формат ошибок — общий problem JSON.
- mesh-job идёт как `meshjob-…` (uuid-lite) **вне** `JobManager` и `/v1/jobs` — там только симуляции (`job-…`), runs history симуляционная. Статус — поллинг `GET …/mesh`; `begin/finish/fail` в in-memory `EditorSceneStore`, «last submit wins».
- Хранение — in-memory (`EditorSceneStore`), без persistence (v1); PUT/PATCH сбрасывает mesh/report (артефакты принадлежат документу, из которого сгенерированы).
- `to-scenario`: **rect**, если сцена — замкнутая коробка (см. §2.3); иначе **mesh** + `ray-tracing`.
- Floor/ceiling в v1 — `solid_box`-плиты, отдельного типа нет.

**WebSocket (interactive):** ops `moveEntity` / `resizeEntity` / `setFaceMaterial` / `addOpening` — **отложены** (в v1 не реализованы; неизвестные `scene.patch` ops отклоняются). Правка документа сейчас — через PUT/PATCH `/v1/scenes/{id}`. Существующий `scene.patch` относится к Scenario Store (movers) и не путать с редактором.

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
libs/core/geometry   # MeshBuilder, mesh primitives (есть)
libs/core/scene      # SceneEntity model (scene_model), mesher (scene_mesher)
libs/io              # scene.json r/w, mesh-report.json, OBJ export, to-scenario (есть)
apps/api             # EditorSceneStore + /v1/scenes, /mesh, to-scenario (R9)
apps/cli             # av-cli mesh <scene.json> (R8); gen-mesh — генерация демо-геометрии
gui/                 # слайсы после стабилизации API
```

Зависимости без циклов: `scene` types → `geometry` mesh → `io` → `api`.

---

## 7. Фазы реализации

| Слайс | Scope | Exit | Статус |
|-------|--------|------|--------|
| **R6** | Scene model + schema + json r/w + tests | scene.json round-trip | done |
| **R7** | Primitives: wall/solid/void → SurfaceMesh (earclip/quads), zones | mesh from wall + opening | done |
| **R8** | meshSettings presets + mesh-report + CLI `av-cli mesh scene.json` | report + obj | done |
| **R9** | API scenes + generate mesh job + to-scenario | REST smoke | done |
| **R10** | Docs (RU/EN) + ru-check | документация вычитана | done |

**Не в V0.2:** full CSG, boolean-склейка общего вида, tets от внешних mesher’ов, GUI-редактор «как в CAD», entity-ops в `scene.patch`.

---

## 8. Риски

| Риск | Мера |
|------|------|
| CSG-дыры (openings, L-rooms) | grid split + warnings в mesh-report (jamb-граней в v1 нет) |
| Смешение Scenario / Scene | явный `to-scenario`, разные хранилища (lookup → 404) |
| Долгий meshing | async `meshjob-…` + draft quality по умолчанию |
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
