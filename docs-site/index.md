# Acoustic-Vision

Open-source инструмент акустического анализа помещений: 3D-сцена → источники → алгоритмы → поле звука, отражения и артефакты.

**Стадия:** P0–P3 backend и ядро редактора сцен/mesh (V0.2) реализованы. GUI — прототип в `gui/`. Полный 3D-DCC и dynamic DLL — только по явному запросу.

## Что делает продукт

Пользователь строит 3D-сцену помещения, расставляет источники звука, запускает разные алгоритмы акустического расчёта и **видит**, как звук распределяется в пространстве: куда и с какой силой приходит, где отражения, где появляются артефакты (фокус, мёртвые зоны, эхо).

| Возможность | Смысл |
|-------------|--------|
| 3D-сцена | rect-комната или mesh (OBJ/glTF), материалы с α(f) |
| Источники | custom + built-in каталог приборов «как есть» |
| Несколько методов | одна сцена → разные engines → история запусков |
| Артефакты | metrics / IR / field / rays / findings — по caps engine |
| Interactive | WS-дельты позиций при перетаскивании |
| Compute | кнопка «Расчёт»; job может идти часы (CPU/GPU) |
| Экспорт | IR WAV, metrics JSON, field JSON, OBJ |

## Принципы

| Принцип | Смысл |
|---------|--------|
| Ядро отделено от UI | C++ считает; GUI — Electron + Vue + Three.js; связь REST/WS |
| Методы — плагины | новый алгоритм не трогает API, GUI и чужие движки |
| Один контракт результата | любой engine → Result |
| Interactive first | WS-дельты сцены; тяжёлый расчёт — job |
| Без measurement | pipeline замеров не входит в scope |

## Ограничения и «не в v1»

| Не делаем | Комментарий |
|-----------|-------------|
| Замеры помещений | без mic/phone/calibration pipeline |
| FDTD и волновые солверы | геометрическая акустика ≠ волновая; LF-моды — отдельным engine позже |
| Гарантия качества каталога | данные каталога — «как есть», с disclaimer |
| Обязательная «1 секунда» на расчёт | тяжёлые методы могут считаться часами |
| Real-time DSP 60 FPS | interactive = синхронизация сцены, не полный пересчёт |
| HRTF / binaural | приёмники — точечные |
| multi-room, auth, multi-tenant | localhost-инструмент |
| Полноценный DCC-редактор | базовый редактор в v1; полный DCC — позже |

## Быстрый взгляд на стек

- **Ядро:** C++20, CMake ≥ 3.24, vcpkg, GoogleTest
- **API:** Crow + Asio, REST + WebSocket `/v1`
- **GUI:** Electron + Vue + Three.js (прототип)
- **Форматы:** JSON (scenario / scene / catalog), OBJ, glTF/GLB, WAV

## С чего начать

1. [Быстрый старт](/guide/getting-started) — сборка, `av-api`, GUI, первый расчёт.
2. [Концепции](/guide/concepts) — Scenario vs Scene, IR, RT60/EDT, engines, bands.
3. [Справочник API](/reference/api) — все endpoints и WebSocket.

## Статус roadmap

| Фаза | Поставка | Статус |
|------|----------|--------|
| P0 | Engine API, Sabine/Eyring, ISM, metrics, CLI | done |
| P1 | av-api REST/WS, JobManager, runs history | done |
| P2 | mesh OBJ+glTF, Ray Tracing, hybrid, field | done |
| P3 backend | source catalog, Scene Store, scene.patch | done |
| V0.2 | Scene model, `av-cli mesh`, `/v1/scenes`, to-scenario | done |
| P2.5 | Dynamic plugin SDK (C ABI) | only on request |
| P3 GUI | Electron + Vue + Three.js | прототип GUI1–4 + V0.1.1 |
| P4 | Research-алгоритмы | later |
| P5 | Полноценный 3D-редактор (DCC) | later |
