# Методы расчёта (engines)

Четыре встроенных метода: аналитика, зеркальные источники и два mesh-метода на стохастических лучах. Новый метод = один модуль + `REGISTER_ENGINE`, без правок API и GUI.

## Сводка

| id | Модель | Комната | Артефакты | paramsSchema |
|----|--------|---------|-----------|--------------|
| `sabine-eyring` | аналитика `T = 0.161·V/(S·ᾱ)` | rect | `metrics` | пустой |
| `image-source` | зеркальные источники | rect | `metrics`, `ir` | пустой |
| `ray-tracing` | лучи + BVH | mesh | `metrics`, `ir`, `rays`, `field` | `rayCount`, `maxBounces`, `seed`, `receiverRadius`, `capturePathLimit` |
| `hybrid` | RT + early/late split | mesh | `metrics`, `ir`, `rays`, `field` | как RT + `earlyTimeSec`, `earlyOrder`, `useSabineTail` |

Список живьём: `GET /v1/engines` или `av-cli engines`.

## Общие параметры симуляции (`params`)

| Поле | Default | Назначение |
|------|---------|------------|
| `engine` | — | id метода |
| `maxReflectionOrder` | 0 | порядок отражений (1–32), ISM |
| `sampleRate` | — | Гц |
| `durationSec` | — | длительность IR |
| `bandsHz` | 125…4000 | октавные полосы |
| `speedOfSound` | 343 | м/с |
| `capturePaths` | false | полилинии лучей |
| `airAbsorption` | false | поглощение в воздухе |
| `signedIr` | false | IR со знаком |
| `fieldGrid` | off | `{enabled, nx, ny, nz}` — сетка поля |
| `methodParams` | `{}` | параметры конкретного engine |

`fieldGrid.nx/ny/nz` — по 1…64. Поле заполняют engines с артефактом `field` (ray-tracing / hybrid), когда `fieldGrid.enabled = true`.

## sabine-eyring

Аналитическая оценка времени реверберации из объёма V, площади S и среднего поглощения ᾱ(f).

| | |
|--|--|
| Комната | rect |
| Артефакты | `metrics` (RT60 Sabine, Eyring; без IR) |
| methodParams | нет |
| Скорость | мгновенно |

Используйте для быстрой оценки и sanity-check mesh-методов.

## image-source

Классический метод зеркальных источников (ISM) для прямоугольной комнаты.

| | |
|--|--|
| Комната | rect |
| Артефакты | `metrics`, `ir` |
| Управление | `maxReflectionOrder` (1–32) |
| methodParams | нет (пустой `paramsSchema`) |

**Требования validate:**

- хотя бы один source и один receiver;
- source/receiver **внутри** комнаты;
- rect-геометрия (mesh → capability mismatch, `400`).

**Конвенции ISM** (не менять молча — источник golden-тестов):

| Правило | Формула |
|---------|---------|
| Задержка | `t = r / c`, `c = 343 м/с` по умолчанию |
| Затухание | амплитуда `∝ 1/r` |
| Отскок | energy `× (1-α)`, amplitude `× √(1-α)` **на полосу** |
| Energy | `energy = amplitude²` |
| Полярность | по умолчанию energy envelope; `signedIr` — опция |

## ray-tracing

Стохастический трассировщик лучей по mesh + BVH.

| | |
|--|--|
| Комната | mesh (`loadedMesh`) |
| Артефакты | `metrics`, `ir`, `rays`, `field` |
| validate | rect отклоняется |

### methodParams

| Ключ | Тип | Default | Смысл |
|------|-----|---------|--------|
| `rayCount` | integer ≥ 1 | 2000 | число лучей на источник |
| `maxBounces` | integer ≥ 1 | 32 | максимум отражений |
| `seed` | integer ≥ 0 | 42 | seed ГПСЧ |
| `receiverRadius` | number > 0 | 0.5 | радиус сферы приёмника, м |
| `capturePathLimit` | integer ≥ 0 | 64 | cap путей в `rays.paths` |

Если `maxBounces` не задан в `methodParams`, но указан `params.maxReflectionOrder`, берётся `maxReflectionOrder`.

Дополнительно `methodParams.capturePaths` может включить сбор путей (OR с `params.capturePaths`).

**Замечания:**

- `rayCount` влияет на абсолютный уровень депозитов; decay rate и T30 от `rayCount` не зависят, но **сравнение** двух прогонов требует одинаковых `rayCount` и `seed`.
- Лучи и поле заполняются при `capturePaths` / `fieldGrid.enabled`.
- `CancelToken` опрашивается в циклах — длинные прогоны отменяемы.

## hybrid

Один стохастический RT-проход на mesh (как `ray-tracing`), но ранние/поздние депозиты разведены.

| | |
|--|--|
| Комната | mesh (`loadedMesh`) |
| Артефакты | `metrics`, `ir`, `rays`, `field` |
| Идея | early → плотный суб-сэмпл депозит; late → экспоненциальный хвост |

### methodParams

Все ключи ray-tracing, плюс:

| Ключ | Тип | Default | Смысл |
|------|-----|---------|--------|
| `earlyTimeSec` | number ≥ 0 | 0.08 | граница ранней части, с |
| `earlyOrder` | integer ≥ 0 | 2 | порядок отражений для early |
| `useSabineTail` | boolean | false | заменить позднюю часть аналитическим хвостом Сэбина |

Депозит считается ранним, если `pathTime < earlyTimeSec` **или** `order ≤ earlyOrder`. Сумма ранней и поздней частей — одно `IrBuffer`.

`useSabineTail = true` подставляет после `earlyTimeSec` экспоненциальный энергетический спад, согласованный с Сэбином.

## Артефакты: что запрашивать

| Нужно | Включить | Engine |
|-------|----------|--------|
| RT60/EDT | default | любой |
| IR WAV | engine с `ir` | image-source, ray-tracing, hybrid |
| Heat-map | `fieldGrid.enabled` | ray-tracing, hybrid |
| Лучи в 3D | `capturePaths: true` | ray-tracing, hybrid |

## Capability mismatch

`EngineRegistry` выбирает engine по `params.engine` + типу комнаты. Несовпадение (например ISM + mesh) → **`400`** на границе API (`scenario validation failed` / `unknown engine`) или ошибка validate в CLI. Не запускайте job вслепую — проверяйте `GET /v1/engines` (`caps.supportsRect`, `caps.supportsMesh`).

## Как добавить метод

1. Реализовать `IEngine` в `libs/core/src/acoustics/` (или `engines/<name>/`).
2. Заполнить `descriptor` (+ `paramsSchema`, `artifacts`), `validate`, `run`.
3. `REGISTER_ENGINE(...)` в `builtin_engines.cpp`.
4. Юнит/golden-тест при аналитической проверке.
5. Строка в таблице `docs/engines.md` §4.

Метод появится в `GET /v1/engines` и в GUI без правок UI. Полный контракт — `docs/engines.md` и [Engine API](/reference/schema#engine-descriptor).

## Резерв: fdtd / моды

Волновое ядро, LF-моды — later / plugin. Не входит в v1; не путать с геометрической акустикой.
