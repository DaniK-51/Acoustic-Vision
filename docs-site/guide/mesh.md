# Mesh

Mesh-система V0.2: surface mesh из Scene, пресеты качества, mesh-report, `gen-mesh` и `av-cli mesh`.

## Два уровня

| Уровень | Назначение | Кто ест |
|---------|------------|---------|
| **Surface mesh** | треугольники границ | ray-tracing, hybrid, export OBJ/glTF |
| **Volume mesh** | тетры/гексы в объёме | будущие FDTD/modal (v1 — каркас API) |

## Surface mesh

**Вход:** Scene entities (CSG-lite: union solids, subtract voids, wall slabs).

**Алгоритмы v1:**

1. **Explicit quads→triangles** — стены/боксы без CSG (быстро, стабильно, draft).
2. **Clip + ear-clip** — стены с opening’ами, L-extrude (reliable fallback).
3. **Advancing front / Delaunay** (v1.1) — если `targetEdge` сильно меньше размера комнаты.

**Выход:** `SurfaceMesh { positions, triangles, zones[] }` — тот же тип, что `av::Mesh`. Zone = material slot name. Пишется в OBJ/glTF.

## Настройки (`meshSettings.surface`)

| Поле | Диапазон | Смысл |
|------|----------|--------|
| `algorithm` | auto / earclip / advancing-front | алгоритм |
| `targetEdge` | м | целевое ребро (опционально) |
| `maxEdge` | м | жёсткий потолок (опционально) |
| `quality` | draft / standard / fine | пресет → (targetEdge, maxEdge) |

Пресеты:

| quality | targetEdge | maxEdge | Когда |
|---------|------------|---------|--------|
| draft | 0.5 | 1.0 | быстрый предпросмотр |
| standard | 0.2 | 0.5 | рабочий расчёт |
| fine | 0.08 | 0.2 | плотная сетка |

Явные `targetEdge` / `maxEdge` **важнее** пресета. Флаг CLI `--quality` — полный override: заменяет quality и **сбрасывает** custom `targetEdge`/`maxEdge`, чтобы применился пресет.

Поля `preserveCorners` / `weldEpsilon` — **не в v1**.

## Volume mesh (каркас)

```text
VolumeMeshSettings { algorithm: tet | hex-box, targetSize, enabled }
generateVolumeMesh(scene) → { cells, nodes, zones }
```

V0.2: API + schema + box-only `hex-box` stub (одна hex-ячейка на `SolidBox`; остальная геометрия и `algorithm: tet` — предупреждение и ноль ячеек). Полноценные tets — следующая итерация.

## mesh-report

После генерации — статистика и warnings:

```json
{
  "kind": "surface",
  "stats": {
    "vertices": 120,
    "triangles": 240,
    "zones": ["floor", "walls"]
  },
  "bbox": [[0, 0, 0], [6, 2.8, 4]],
  "warnings": ["non-manifold edge at wall-north/openings/0"],
  "settingsUsed": {
    "algorithm": "earclip",
    "targetEdge": 0.2,
    "maxEdge": 0.5
  },
  "entityTriangleCount": {
    "wall-north": 120,
    "floor": 4
  }
}
```

| Поле | Смысл |
|------|--------|
| `stats` | verts, tris, zones |
| `bbox` | мировой ограничивающий бокс |
| `warnings` | non-manifold и т.п. |
| `settingsUsed` | фактически применённые настройки |
| `entityTriangleCount` | trace: triangle → entityId (для клика в 3D) |

## CLI: `gen-mesh`

Готовые примитивы + placeholder-scenario:

```bash
av-cli gen-mesh box -o room.obj --w 6 --h 2.8 --d 4.5 \
  [--origin x,y,z] [--scenario room.json]

av-cli gen-mesh corridor -o corr.obj --w 6 --h 2.8 --d 12 \
  [--origin x,y,z] [--scenario corr.json]   # --len = --d

av-cli gen-mesh lroom -o l.obj --w 6 --d 4 --w2 3 --d2 3 --h 2.8 \
  [--origin x,y,z] [--scenario l.json]
```

| Параметр | Смысл |
|----------|--------|
| `-o` | выходной OBJ (обязателен) |
| `--w`, `--h`, `--d` | размеры, м |
| `--w2`, `--d2` | второе плечо L-комнаты |
| `--origin` | `x,y,z` |
| `--scenario` | записать placeholder scenario рядом |

Placeholder-scenario: mesh-room + три материала + один source/receiver + `ray-tracing` + `fieldGrid`. `mesh.uri` — путь OBJ **относительно scenario-файла**.

## CLI: `mesh`

Scene → surface mesh OBJ + optional mesh-report:

```bash
av-cli mesh scene.json -o out.obj --report report.json --quality standard
```

| Параметр | Смысл |
|----------|--------|
| `scene.json` | входной Scene |
| `-o` | выходной OBJ (default: рядом со scene, расширение `.obj`) |
| `--report` | записать mesh-report.json |
| `--quality` | draft / standard / fine — **полный** density override |

Warnings пишутся в stderr; ошибки валидации Scene — в stderr, exit ≠ 0.

## API mesh

| Метод | Путь | Ответ |
|-------|------|-------|
| POST | `/v1/scenes/{id}/mesh` | `202` `{sceneId, jobId, state}` или `?sync=1` сразу report |
| GET | `/v1/scenes/{id}/mesh` | `{sceneId, sceneVersion, jobId, state, report}` (404 если не генерировали) |
| GET | `/v1/scenes/{id}/mesh.obj` | `model/obj` (404 если mesh нет) |

Mesh-джобы **не** в `/v1/jobs` — поллинг `GET /v1/scenes/{id}/mesh`.

## Резолвинг mesh.uri в расчёте

Когда Scenario ссылается на OBJ/glTF:

| Кто | База пути |
|-----|-----------|
| `av-cli run` | каталог scenario-файла |
| `av-api` (`POST /v1/simulations`) | **CWD процесса** |

Поэтому API запускают из корня репозитория (как в GUI-демо), либо пишут абсолютный `uri`. Ошибка загрузки — `400 mesh load failed` до старта job.

## Ограничения v1

- CSG-lite, не полный boolean.
- Volume mesh — stub.
- Нет `preserveCorners` / `weldEpsilon`.
- Плохие mesh из DCC — warnings в mesh-report, не hard fail.
