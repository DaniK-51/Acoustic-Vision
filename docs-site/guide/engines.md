# Методы расчёта

В Acoustic-Vision четыре встроенных engine: `sabine-eyring`, `image-source`, `ray-tracing`, `hybrid`. Каждый — реализация `IEngine` (`descriptor()`, `validate()`, `run()`), зарегистрированная через `REGISTER_ENGINE`. Список и возможности смотрите в `GET /v1/engines` или через `av-cli engines`.

## Как выбрать метод

| Задача | Метод | Комната | Почему |
|--------|-------|---------|--------|
| Быстрая оценка RT60 по объёму и материалам | `sabine-eyring` | rect | аналитика, мгновенно |
| Точная ранняя часть IR, порядок отражений | `image-source` | rect | детерминированный ISM |
| Произвольная геометрия, поле и лучи | `ray-tracing` | mesh | BVH + стохастика |
| Плотная ранняя часть + быстрый поздний хвост | `hybrid` | mesh | early/late split |

Тип комнаты жёстко проверяется в `validate()`: rect-методы отклоняют mesh, RT-методы — rect. Несовпадение capability → `400` на границе API (job не стартует).

## sabine-eyring

| Поле | Значение |
|------|----------|
| id | `sabine-eyring` |
| Имя | Sabine-Eyring Analytic RT60 |
| Комната | rect (`w × h × d` + материалы граней) |
| Артефакты | `metrics` |
| `methodParams` | нет (`paramsSchema` — пустой объект) |

Аналитика по среднему поглощению:

| Формула | Смысл |
|---------|-------|
| `T = 0.161 · V / (S · ᾱ)` | RT60 Sabine |
| Eyring | то же с поправкой `ln(1 − ᾱ)` |

Считает **только** метрики (`rt60[band].sabine` / `.eyring` по полосам), IR не строит. Подходит для экспресс-оценки и сверки RT-методов.

## image-source

| Поле | Значение |
|------|----------|
| id | `image-source` |
| Имя | Image Source Method |
| Комната | rect |
| Артефакты | `metrics`, `ir` |
| `methodParams` | нет |

Метод зеркальных источников (ISM): перебор решётки отражений до порядка `params.maxReflectionOrder` (1–32). Для каждой пары source × receiver накапливается IR.

Требования `validate()`:

- хотя бы один источник и один приёмник;
- все источники и приёмники — **внутри** комнаты.

Метрики: IR-derived (Schroeder T30, EDT, early reflections) плюс аналитические Sabine/Eyring по полосам.

### Конвенции ISM

Источник golden-тестов — менять молча нельзя:

| Правило | Формула |
|---------|---------|
| Задержка | `t = r / c`, `c = 343 м/с` по умолчанию |
| Геометрическое затухание | амплитуда `∝ 1/r` |
| Отскок | α — энергетическое поглощение: energy `× (1 − α)`, amplitude `× √(1 − α)` **на полосу** |
| Energy | `energy = amplitude²` |
| Полярность | по умолчанию energy envelope; `signedIr: true` — IR со знаком |

## ray-tracing

| Поле | Значение |
|------|----------|
| id | `ray-tracing` |
| Имя | Stochastic Ray Tracing |
| Комната | mesh (OBJ/glTF) |
| Артефакты | `metrics`, `ir`, `rays`, `field` |
| `supportsRayPaths` | да |

Стохастический трассировщик поверх BVH: из каждого источника бросается `rayCount` лучей, каждый отражается до `maxBounces` раз. Энергия отскоков оседает в IR (сфера приёмника радиуса `receiverRadius`) и в `field`-гриде (если включён `fieldGrid`). При `capturePaths: true` сохраняются полилинии лучей (`rays`).

### methodParams (ray-tracing)

| Ключ | Default | Смысл |
|------|---------|-------|
| `rayCount` | 2000 | лучей на источник |
| `maxBounces` | 32 | максимум отражений |
| `seed` | 42 | ГПСЧ — воспроизводимый прогон |
| `receiverRadius` | 0.5 | радиус «сбора» энергии приёмника, м |
| `capturePathLimit` | 64 | сколько полилиний сохранять (0 — не сохранять) |

Если `maxBounces` не задан в `methodParams`, но указан `params.maxReflectionOrder`, берётся `maxReflectionOrder`.

Стохастика: decay rate и T30 не зависят от `rayCount`, но **сравнение** двух прогонов требует одинакового `rayCount` и `seed`.

## hybrid

| Поле | Значение |
|------|----------|
| id | `hybrid` |
| Имя | Hybrid (early/late RT) |
| Комната | mesh |
| Артефакты | `metrics`, `ir`, `rays`, `field` |

Один RT-проход (как у `ray-tracing`), но IR делится на раннюю и позднюю части:

- депозиты с `pathTime < earlyTimeSec` **или** порядком `≤ earlyOrder` идут в раннюю часть (плотный суб-сэмпл депозит);
- остальное — в позднюю;
- сумма — одно `IrBuffer`.

Опция `useSabineTail` заменяет позднюю часть аналитическим экспоненциальным хвостом Сэбина, согласованным по энергии с ранней частью.

### methodParams (hybrid)

Все ключи `ray-tracing` плюс:

| Ключ | Default | Смысл |
|------|---------|-------|
| `earlyTimeSec` | 0.08 | граница ранней части, с |
| `earlyOrder` | 2 | порядок отражений в раннюю часть |
| `useSabineTail` | false | аналитический хвост вместо RT-поздней части |

## Caps и артефакты

`EngineDescriptor` (см. `GET /v1/engines`):

```json
{
  "id": "ray-tracing",
  "name": "Stochastic Ray Tracing",
  "version": "0.1.0",
  "caps": {
    "supportsRect": false,
    "supportsMesh": true,
    "supportsRayPaths": true,
    "supportsBands": true,
    "maxOrder": 0
  },
  "paramsSchema": { "type": "object", "properties": { "…": "…" } },
  "artifacts": ["metrics", "ir", "rays", "field"]
}
```

| Поле caps | Смысл |
|-----------|-------|
| `supportsRect` | принимает rect-комнату |
| `supportsMesh` | принимает mesh (требует загруженный `loadedMesh`) |
| `supportsRayPaths` | умеет `rays` при `capturePaths` |
| `supportsBands` | считает по полосам |
| `maxOrder` | лимит порядка отражений; 0 = не применимо |

`paramsSchema` — JSON Schema для `params.methodParams`: GUI рисует форму автоматически (number / integer / boolean / string + default / min / max / enum).

## Метрики, которые считают engines

| Метрика | Определение |
|---------|-------------|
| RT60 Schroeder | backward integration, T30: линейный фит −5…−35 дБ |
| RT60 Sabine | `T = 0.161 · V / (S · ᾱ)` |
| RT60 Eyring | с поправкой на `ln(1 − ᾱ)` |
| EDT | Early Decay Time, фит 0 → −10 дБ |
| EarlyReflections | top-K пиков в первые 80 мс: `t`, `amp`, `order` |

## Как добавить свой engine

1. Реализовать `IEngine` в `libs/core/src/acoustics/` (или в `engines/<name>/`).
2. Заполнить `descriptor()` (+ `paramsSchema`, `artifacts`), `validate()`, `run()`.
3. Зарегистрировать `REGISTER_ENGINE(...)` в `builtin_engines.cpp`.
4. Добавить unit/golden-тест, если есть аналитическая проверка.
5. Пересобрать — метод появится в `GET /v1/engines` и в GUI без правок UI.

Dynamic DLL C ABI (модель B) — отдельная фаза P2.5, по явному запросу. Контракт descriptor / paramsSchema / Result / cancel уже «плагинный» — внешняя модель его не ломает.

Детали контракта — `docs/engines.md` в репозитории.
