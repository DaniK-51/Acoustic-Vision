# Acoustic-Vision V0.1.1 — план изменений

Источник: UX-ревью (пользователь, live-проход UI). Релиз: «сцена понятна, источники честные, drag управляемый, UI по этапам».

## Статус
In progress. Реализация — субагенты по слайсам, архитектор контролирует.

---

## A. Scene comprehension (P1)

| ID | Изменение | Критерий |
|----|-----------|----------|
| A1 | Подсветка граней 3D по material id | цвет floor/ceiling/walls различим |
| A2 | Клик по грани → tooltip: имя грани, materialId, α(f) | |
| A3 | Панель Materials: таблица грань → материал → α по полосам | не просто список id |
| A4 | ≥3 preset-сцены: soft / hard / studio (разные α) | загружаются из UI |
| A5 | Outline: привязка материал → поверхность | |

## B. Source model + directivity (P1)

| ID | Изменение | Критерий |
|----|-----------|----------|
| B1 | Source.orientation (yaw/pitch или forward) в Scenario schema + C++ + UI | сериализация |
| B2 | Directivity data: horizontal/vertical polar curves по частотам (125…4k) | типы omni/dipole/cardioid + curves |
| B3 | UI polar chart (angle × dB, линии по частотам, интерполяция) | |
| B4 | Interpolation f между ближайшими curves | |
| B5 | Учёт gain(θ,f) в RT (если успеваем; иначе V0.1.2) | |

## C. Viewport controls (P0 — первым)

| ID | Изменение | Критерий |
|----|-----------|----------|
| C1 | Drag в плоскости экрана, без сдвига по глубине | |
| C2 | TransformControls гизмо X/Y/Z | |
| C3 | UI: всегда 3 координаты X,Y,Z (баг «2 числа») | |
| C4 | Опц. snap to grid | |

## D. UX stage order (P1)

| ID | Изменение | Критерий |
|----|-----------|----------|
| D1 | Скрыть Source catalog из compute-зоны | не мешает Run |
| D2 | Этапность: Scene → Objects → Engines → Run → Results | |
| D3 | Catalog «вставить источник» только на этапе Scene | |
| D4 | Кнопка «О источнике» — карточка (не PDF) | отложено detail PDF |

## E. Engine documentation (P2)

| ID | Изменение | Критерий |
|----|-----------|----------|
| E1 | EngineInfo `?`: что считает, артефакты, когда use, ограничения, параметры | |
| E2 | Данные из docs/engines.md + descriptor (notes) | |

## F. Fixes

| ID | Изменение |
|----|-----------|
| F3 | 3D coordinate display fix |
| F4 | Mesh demo note (uri = CWD av-api) |
| F5 | README screenshots + карта UI |

## Не в V0.1.1
Полный 3D-редактор · PDF datasheet · real-time DSP · замеры polar.

## DoD
- [ ] C1–C3
- [ ] A1–A5
- [ ] D1–D3
- [ ] B1–B4 (B5 опц.)
- [ ] E1
- [ ] ctest 169+ · npm build green

## Слайсы реализации (субагенты, последовательно)
1. **R1** C1–C4 + F3 (viewport)
2. **R2** A1–A5 (materials + presets)
3. **R3** D1–D3 (UX stages / catalog)
4. **R4** B1–B4 (orientation + polar UI)
5. **R5** E1 (engine docs)
6. **R6** B5 polar in RT (optional)
