# GUI

Прототип графического интерфейса: Electron + Vue 3 + Three.js + Vite + TypeScript в каталоге `gui/`. Связь с ядром — **только** REST/WS `/v1`; GUI не линкует C++.

**Статус:** прототип (слайсы GUI1–4 + V0.1.1). Полный 3D-DCC — отдельная фаза, по явному запросу.

## Запуск

1. Поднять API (см. [Быстрый старт](/guide/getting-started)).
2. В `gui/`:

```bash
cd gui
npm install
npm run dev      # vite (127.0.0.1:5173) + electron
```

Продакшен-сборка рендерера:

```bash
npm run build    # → gui/dist/
npm start        # electron . (грузит dist/index.html)
```

Base URL API — `gui/src/config.ts` (default `http://127.0.0.1:8787/v1`), переопределяется `VITE_API_BASE`. WS: `ws://127.0.0.1:8787/v1/events`.

::: warning CORS
av-api не отдаёт CORS-заголовков. Electron main инжектит их в `onHeadersReceived`, клиент шлёт JSON-body как `text/plain` (без preflight). В обычном браузере fetch к av-api не заработает — это desktop-контракт.
:::

## Что умеет прототип

| Зона | Возможность | Источник данных |
|------|-------------|-----------------|
| Engines | dropdown + caps | `GET /v1/engines` |
| Method params | форма по `paramsSchema` (number / integer / boolean / string; default, min, max, enum; raw fallback) | `GET /v1/engines` |
| Каталог | таблица карточек, about-card, полярный график, «Вставить источник» | `GET /v1/source-catalog` |
| Загрузка сценария | Demo, Mesh demo (field), paste/upload JSON | `POST /v1/scenarios` |
| Outline | JSON-структура сценария, материалы, источники | локальный snapshot |
| Source inspector | yaw/pitch, полярный график ДН | scenario + catalog |
| Compute | «Расчёт» / «Отмена» | `POST /v1/simulations`, `POST /v1/jobs/{id}/cancel` |
| Runs | история, детали прогона, скачать `ir.wav` | `GET /v1/runs`, `GET /v1/jobs/{jobId}/…` |
| Viewport | box/комната, маркеры src/rcv, drag источников | three.js + WS `scene.patch` |
| Field | heat-map point-cloud, blue→red, легенда в относительных дБ | `result.field` |
| Rays | полилинии лучей, цвет = амплитуда, cap 64 линии | `result.rays.paths` |
| IR | график в RunDetail (canvas, опция дБ), парсинг `ir.wav` на клиенте | `GET /v1/jobs/{jobId}/ir.wav` |
| Status bar | health, id job, прогресс WS | `GET /v1/health`, WS `job.*` |

## Ключевые сценарии

### Демо

1. Кнопка **Demo** → **Load** — появляется `sceneId`.
2. **Mesh demo (field)** — грузит `gui/public/demo/box-scenario.json` (mesh + `capturePaths: true` + `fieldGrid`). `mesh.uri` = `gui/public/demo/box.obj` и резолвится относительно CWD av-api: запускайте API из корня репозитория, иначе расчёт вернёт `400 mesh load failed`.

### Интерактивный перенос источника

Select + drag в viewport (X/Z) или числовые X/Y/Z → WS `scene.patch` `move` → `scene.patched` / `scene.error`. Мощность — op `setPower` (панель источника).

Ориентация yaw/pitch в v1 сохраняется **новым snapshot-сценарием** (как и вставка из каталога): rotate-op в `scene.patch` ещё нет.

### Расчёт и результат

1. Выбрать engine и methodParams.
2. «Расчёт» → `POST /v1/simulations` с `{sceneId, engine, methodParams, params?}`; сервер накладывает overlay на snapshot (в digest попадает `sceneVersion`).
3. Статус-бар — прогресс по WS `job.progress`; терминальное событие обновляет runs.
4. RunDetail: метрики (RT60 / EDT / early reflections), IR-график, скачивание WAV, field и лучи в viewport.

### Каталог

Кнопка «Вставить источник» добавляет источник в центр комнаты (со сдвигом от существующих), перенося `powerDb` и `directivity` карточки — полярный график переживает save/load.

## Структура

```text
gui/
  electron/main.js     # BrowserWindow + CORS-инъекция
  src/
    main.ts            # entry Vue
    App.vue            # shell, состояние панелей
    config.ts          # apiBaseUrl / eventsUrl
    params-schema.ts   # subset JSON Schema → форма methodParams
    directivity.ts     # аналитические пресеты + polar-кривые
    api/client.ts      # REST /v1
    api/types.ts       # wire-типы
    api/ws.ts          # /v1/events (subscribe / subscribeScene)
    api/wav.ts         # клиентский парсинг ir.wav
    components/        # Engine, EngineParamsForm, RunControls, RunDetail,
                       # Catalog, LoadScenario, SceneOutline, Runs, StatusBar,
                       # SourceInspector, PolarChart, Viewport, …
```

## Не в прототипе

- Полноценный 3D-редактор: gizmos resize, материалы, импорт OBJ/glTF из GUI.
- Графики RT60 по полосам в отдельной вкладке.
- Rotate-op в `scene.patch` (ориентация — через пересохранение сценария).
- Real-time пересчёт акустики при drag.

Комментарии к roadmap — `docs/idea.md` §7 и `gui/README.md`.
