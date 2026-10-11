# GUI (P3, slice 1)

Electron + Vue 3 + Three.js + Vite + TypeScript. Связь с ядром **только** через REST/WS `/v1` (GUI не линкует C++).

**Статус slice 1:** каркас приложения, панели чтения, загрузка сценария, viewport-stub (сетка + бокс, орбита). Без редактора, heat-map и графов — это GUI2/3.

## Запуск

1. Поднять API (порт по умолчанию `8787`):

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
build/linux-debug/apps/api/av-api --port 8787
```

2. В `gui/`:

```bash
cd gui
npm install
npm run dev      # vite (127.0.0.1:5173) + electron
```

Продакшен-сборка рендерера и запуск без vite:

```bash
npm run build    # → gui/dist/
npm start        # electron . (грузит dist/index.html)
```

Base URL API: `src/config.ts` (по умолчанию `http://127.0.0.1:8787/v1`), переопределяется `VITE_API_BASE` на этапе сборки. WS events: `ws://127.0.0.1:8787/v1/events`.

CORS: av-api не отдаёт CORS-заголовков; Electron main инжектит их в `onHeadersReceived`, клиент шлёт JSON-body как `text/plain` (без preflight). В обычном браузере fetch к av-api не заработает — это desktop-контракт.

## Что есть в slice 1

| Панель | Источник |
|--------|----------|
| Engines (dropdown + caps) | `GET /v1/engines` |
| Source catalog (таблица) | `GET /v1/source-catalog` |
| Load scenario (paste/upload JSON) | `POST /v1/scenarios` → `sceneId` |
| Scene outline (JSON-структура) | локальный сценарий после загрузки |
| Runs (история) | `GET /v1/runs` |
| Status bar | `GET /v1/health` + WS `job.*` |
| Viewport stub | three.js: grid, box (масштаб под rect-room), маркеры src/rcv, орбита |

Кнопка **Demo** подставляет фикстуру в духе `tests/fixtures/demo-scenario.json`.

## Структура

```
gui/
  electron/main.js     # BrowserWindow + CORS-инъекция
  src/
    main.ts            # entry Vue
    App.vue            # shell + состояние панелей
    config.ts          # apiBaseUrl / eventsUrl
    api/client.ts      # REST /v1
    api/types.ts       # wire-типы
    api/ws.ts          # /v1/events (subscribe/subscribeScene)
    components/        # Engine, Catalog, LoadScenario, SceneOutline, Runs, StatusBar, Viewport
```

## Дальше (GUI2/3, не в slice 1)

- Форма `params.methodParams` из `paramsSchema`, кнопка «Расчёт» (`POST /v1/simulations`).
- Полный 3D-редактор (gizmos, материалы, импорт OBJ/glTF), heat-map, графики IR/RT.
- Interactive mode: WS `scene.patch` (move / setPower).

См. `docs/idea.md` §7.
