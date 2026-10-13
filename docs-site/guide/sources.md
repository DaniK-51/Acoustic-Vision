# Источники

Источник звука в Acoustic-Vision: позиция, мощность, ориентация, направленность и карточка каталога.

## Модель источника (Scenario)

```json
{
  "id": "src-1",
  "position": [1.2, 1.5, 1.0],
  "powerDb": 80.0,
  "orientation": { "yawDeg": 30, "pitchDeg": -5 },
  "directivity": {
    "type": "cardioid",
    "description": "…",
    "horizontal": { "freqsHz": […], "anglesDeg": […], "gainDb": [[…]] }
  }
}
```

| Поле | Обязательно | Смысл |
|------|-------------|--------|
| `id` | да | идентификатор |
| `position` | да | `[x, y, z]` в метрах, **Y-up** |
| `powerDb` | нет (default 0) | уровень в dB SPL @ 1 м |
| `orientation` | нет | наведение: yaw/pitch |
| `directivity` | нет | диаграмма направленности |

В Scene (редакторе) source проще: `id`, `name`, `position`, `powerDb`.

## powerDb

`powerDb` — **уровень звукового давления** (dB SPL @ 1 м на оси), не электрические ватты. В каталоге тот же смысл: rated/typical max SPL с карточки «как есть».

Как мощность применяется в расчётах:

| Метод | Эффект `powerDb` |
|-------|------------------|
| `ray-tracing`, `hybrid` | масштаб энергии источника: `10^(powerDb / 10)` |
| `image-source` | v1 строит IR в относительной шкале (амплитуда единичного источника) |
| `sabine-eyring` | не используется — RT60 от мощности не зависит |

Абсолютные дБ на `field` и в IR RT-методов отражают `powerDb`; сравнение «что громче» между методами корректнее делать в относительных дБ. Мощность меняется без пересоздания сцены — op `setPower` в WS `scene.patch` (см. [Симуляция](/guide/simulation)).

## Orientation (наведение)

```json
"orientation": { "yawDeg": 0, "pitchDeg": 0 }
```

| Параметр | Ось | Нулевое значение | Положительное |
|----------|-----|------------------|---------------|
| `yawDeg` | поворот вокруг +Y | смотрит в +Z | поворот к +X |
| `pitchDeg` | наклон вверх/вниз | горизонт | вверх |

Оба аддитивны, default 0. Отсутствующий `orientation` в JSON = «наведён в +Z». Ориентация хранится в сценарии и показывается в GUI; rotate-op в `scene.patch` v1 нет — GUI сохраняет поворот новым snapshot-сценарием.

## Directivity (направленность)

```json
"directivity": {
  "type": "omni|dipole|cardioid|custom",
  "description": "…",
  "horizontal": { … },
  "vertical": { … }
}
```

| Поле | Смысл |
|------|--------|
| `type` | аналитическое семейство |
| `description` | текстовое описание |
| `horizontal` | polar-кривая в азимутальной плоскости |
| `vertical` | polar-кривая в плоскости возвышения |

Если кривых нет, потребитель может взять аналитический пресет для omni/dipole/cardioid. `custom` без кривых — паттерна нет.

### Polar-кривая

```json
{
  "freqsHz": [125, 250, 500, 1000, 2000, 4000],
  "anglesDeg": [-180, -150, …, 180],
  "gainDb": [
    [0, -0.5, …],
    …
  ]
}
```

| Поле | Форма |
|------|--------|
| `freqsHz` | длина N — номер полосы |
| `anglesDeg` | длина M — углы |
| `gainDb` | N строк × M столбцов; строка `i` относится к `freqsHz[i]` |

Кривые **относительные**: 0 dB — осевой уровень источника/карточки, не абсолютная громкость.

GUI рисует polar-диаграмму в инспекторе источника и в карточке каталога.

Сейчас диаграмма — часть модели данных и визуализации. Ядра расчёта v1 сэмплируют направления изотропно: направленность ещё **не** применяется в физике отскоков — cardioid не изменит поле.

## Built-in catalog

`GET /v1/source-catalog` отдаёт read-only каталог реальных/типовых приборов.

Формат (`schemas/source-catalog.schema.json`):

```json
{
  "schemaVersion": 1,
  "disclaimer": "example specs, not verified vendor data",
  "sources": [
    {
      "id": "generic-studio-monitor",
      "name": "Generic 2-way Studio Monitor (example)",
      "manufacturer": "…",
      "category": "studio-monitor",
      "powerDb": 106.0,
      "spectrum": {
        "bandsHz": [125, 250, 500, 1000, 2000, 4000],
        "responseDb": [0.0, 0.5, 0.0, 0.0, -0.5, -1.5]
      },
      "directivity": { "type": "cardioid", "description": "…" },
      "notes": "powerDb = typical max SPL in dB SPL @ 1 m on-axis…",
      "sourceUrl": "…",
      "disclaimer": "example specs, not verified vendor data"
    }
  ]
}
```

| Поле | Смысл |
|------|--------|
| `powerDb` | rated/typical max SPL, dB SPL @ 1 м |
| `spectrum.responseDb` | относительные сдвиги уровня по полосам (АЧХ); движки v1 их не применяют |
| `directivity` | как у custom-источника |
| `disclaimer` | **обязателен** у каталога и каждой карточки |

Встроенные карточки (примеры, не верифицированные vendor-данные):

| id | Категория | powerDb | Направленность |
|----|-----------|---------|----------------|
| `generic-studio-monitor` | studio-monitor | 106 | cardioid |
| `generic-pa-top` | pa | 128 | cardioid |
| `generic-omni-source` | omni | 120 | omni |
| `generic-dipole` | dipole | 100 | dipole |

### Загрузка файла каталога

Данные лежат в `data/source_catalog.json`. Сервер читает `source_catalog.json` рядом с executable; переменная `AV_SOURCE_CATALOG` переопределяет путь. Если файла нет — пустой список + warning, endpoint всё равно `200`.

### Ограничения каталога

- данные **«как есть»**, без гарантии качества спеков;
- каталог read-only (CRUD нет);
- custom-источник всегда работает offline, без каталога.

GUI: панель каталога, кнопка «Вставить источник» добавляет source в загруженный сценарий (в центр комнаты, с небольшим смещением от существующих).

## Источники в Scene (редактор)

В `scene.schema.json` source — entity:

```json
{ "type": "source", "id": "src-1", "position": [1, 2, 0.5], "powerDb": 90 }
```

Позиции в Scene — **Z-up**. При `to-scenario` они переводятся в Y-up Scenario.

## Практические советы

1. Для сравнения прогонов держите `powerDb` и позиции фиксированными — меняйте один фактор.
2. Используйте catalog-карточку как стартовую точку, затем уточняйте custom.
3. Для heat-map и лучей ставьте `capturePaths` и `fieldGrid` в params — см. [Симуляция](/guide/simulation).
4. Проверяйте, что source внутри комнаты: ISM validate ругается на `outside the room`.
