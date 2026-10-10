# Acoustic-Vision — идея продукта

**Стадия:** архитектура (без реализации). Контракты Engine API — в [`engines.md`](engines.md); модули и API — в [`ARCHITECTURE.md`](../ARCHITECTURE.md).

## 1. Проблема

Акустический расчёт помещений (студии, переговорные, залы, классы) обычно разбит между:

- табличными калькуляторами RT60 — быстро, но без IR и геометрии;
- тяжёлыми CAE-пакетами — точно, но долго, дорого, крутой UX;
- research-кодом — гибко, но не как продукт.

## 2. Решение

**Acoustic-Vision** — десктопное приложение, где пользователь ведёт сцену от геометрии до метрик:

1. комната во встроенном 3D-редакторе или импорт mesh (OBJ/glTF);
2. материалы с частотно-зависимым поглощением α(f);
3. источники и приёмники;
4. выбор **метода расчёта** (engine);
5. IR, RT60/EDT, ранние отражения, визуализация лучей.

## 3. Ключевые принципы

| Принцип | Смысл |
|---------|--------|
| **Ядро отделено от UI** | C++ вычисления; GUI — Electron; связь REST/WS. Ядро headless и в CI. |
| **Методы расчёта — плагины** | Новый алгоритм не должен трогать API, GUI и чужие движки. |
| **Один контракт результата** | Любой метод отдаёт IR + metrics (+ paths); экспорт и графики не знают, кто считал. |
| **Сначала ядро, потом GUI** | Считать можно из CLI/API; оболочка не блокирует методы. |
| **Честные пределы модели** | Геометрическая акустика ≠ волновая. Дифракция/моды — отдельные engines позже. |

## 4. Сценарии использования

- Предсказание RT60 / ясности речи при выборе отделки.
- Сравнение материалов и расстановки (mesh-мебель).
- Обучение: как порядок отражений и α меняют IR.
- Источник IR для аурализации (экспорт WAV) — без real-time DSP в v1.

## 5. Концепция системы

```
┌─────────────────────────────────────────────────────────┐
│  GUI (P3) Electron + 3D-редактор                         │
│  выбор engine · форма paramsSchema · IR/метрики · лучи   │
└───────────────────────────┬─────────────────────────────┘
                            │ REST + WebSocket  (/v1)
┌───────────────────────────▼─────────────────────────────┐
│  av-api / av-cli                                         │
│  сценарии · jobs · артефакты (WAV/CSV/JSON)              │
└───────────────────────────┬─────────────────────────────┘
┌───────────────────────────▼─────────────────────────────┐
│  av_sim — очередь задач, progress, cancel                │
└───────────────────────────┬─────────────────────────────┘
┌───────────────────────────▼─────────────────────────────┐
│  av_core                                                 │
│  geometry · materials · metrics · Engine API             │
│    ├─ SabineEyringEngine   (аналитика RT60)              │
│    ├─ ImageSourceEngine    (ISM, rect)                   │
│    ├─ RayTracingEngine     (mesh, P2)                    │
│    └─ … новые методы без правок ядра                     │
└───────────────────────────┬─────────────────────────────┘
┌───────────────────────────▼─────────────────────────────┐
│  av_io — OBJ/glTF import · scenario JSON · WAV/CSV       │
└─────────────────────────────────────────────────────────┘
```

### Scenario — единица работы

JSON-документ (`schemas/scenario.schema.json`):

- **room** — rect (w×h×d + материалы граней) или mesh (uri, unit, materialOverrides);
- **materials** — α(f) по октавным полосам + scattering;
- **sources / receivers** — позиции (SI, метры);
- **params** — `engine`, `maxReflectionOrder`, `sampleRate`, `durationSec`, `bandsHz`, `speedOfSound`, `methodParams`.

### Result — единый для всех методов

```json
{
  "metrics": {
    "rt60": { "500": { "schroederT30": 0.42, "sabine": 0.40, "eyring": 0.41 } },
    "edt":  { "500": 0.31 },
    "earlyReflections": [{ "t": 0.012, "amp": 0.4, "order": 1 }]
  },
  "ir": { "sampleRate": 48000, "channels": ["src-1:rcv-1"], "urls": { "wav": "..." } },
  "paths": null
}
```

## 6. API (контракт для GUI)

- Base: `http://localhost:8787/v1`
- WS: `ws://localhost:8787/v1/events`
- JSON only. Ошибки: `{ type, title, status, detail, fields? }`.

| Метод | Путь | Назначение |
|-------|------|------------|
| GET | `/v1/health` | статус, версия |
| GET | `/v1/engines` | descriptor'ы методов (caps + paramsSchema) |
| GET/POST | `/v1/materials`, `/v1/rooms`, `/v1/sources`, `/v1/receivers`, `/v1/scenarios` | CRUD |
| POST | `/v1/rooms/import` | OBJ/glTF → room |
| POST | `/v1/simulations` | запуск сценария → job или `?sync=1` |
| GET | `/v1/simulations/{id}/ir` \| `/metrics` \| `/paths` | артефакты |
| GET | `/v1/jobs/{id}` · POST `/v1/jobs/{id}/cancel` | async lifecycle |
| GET | `/v1/bands` | сетка частот |

WS: `job.progress` · `job.succeeded` · `job.failed` · `job.cancelled` · (P3) `simulation.reflection`

## 7. GUI (P3)

- 3D-редактор комнаты: box/extrude, стены/пол/потолок, палитра материалов.
- Импорт OBJ/glTF.
- Gizmos источников и приёмников.
- Панель метода: список из `/v1/engines` + автформа из `paramsSchema`.
- Результаты: IR, RT60/EDT, подсветка ранних отражений в сцене.
- Только REST/WS — GUI не линкует C++.

## 8. Roadmap

| Фаза | Поставка | Критерий готовности |
|------|----------|---------------------|
| **P0** | Engine API + Sabine/Eyring + ISM + metrics + CLI | RT60 из IR ≈ аналитике; «новый engine = файл + register» |
| **P1** | av-api REST/WS, jobs, schemas | Контракт `/v1` заморожен |
| **P2** | OBJ/glTF, mesh, RayTracing, paths | glTF-комната считается |
| **P2.5** | Dynamic plugin SDK (C ABI) | Внешний sample.dll без пересборки |
| **P3** | Electron GUI + 3D-редактор | edit → simulate → visualize |

### Не в v1

FDTD в ядре · real-time VST · HRTF · multi-room · GPU · auth · multi-tenant.

## 9. Риски

| Риск | Мера |
|------|------|
| Ошибки в ISM (1/r, α vs √α) | Golden-тесты + письменные конвенции |
| Mesh ломает чистый IMS | Capability flags → `400`; RT — отдельный engine |
| Плохие mesh из DCC | Import-валидатор (warnings) |
| Долгие расчёты блокируют API | Job queue + cancel-token в каждом engine с P0 |
| GUI «прирастает» к ядру | Запрет: только HTTP/WS |
| Слишком ранний plugin SDK | A→B: registry сначала, ABI по факту |

## 10. Словарь

| Термин | Определение |
|--------|-------------|
| **IR** | Impulse Response — импульсный отклик |
| **RT60** | Время реверберации (−60 дБ) |
| **EDT** | Early Decay Time (0…−10 дБ) |
| **ISM** | Image Source Method — метод зеркальных источников |
| **Engine** | Реализация метода расчёта за `IEngine` |
| **Scenario** | Комната + материалы + S/R + params |
| **α(f)** | Коэффициент звукопоглощения по частоте |
| **Scattering** | Коэффициент рассеяния (для RT; ISM v1 не использует) |
| **Bands** | Октавные полосы (default 125…4k Hz) |
