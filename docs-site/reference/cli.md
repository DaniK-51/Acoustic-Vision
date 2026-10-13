# av-cli

Консольный инструмент: запуск расчётов без сервера, список engines, генерация и экспорт mesh.

```text
av-cli engines
av-cli run <scenario.json> [-o outdir]
av-cli mesh <scene.json> [-o out.obj] [--report report.json]
    [--quality draft|standard|fine]
av-cli gen-mesh box -o room.obj --w <m> --h <m> --d <m>
    [--origin x,y,z] [--scenario room-scenario.json]
av-cli gen-mesh corridor -o corr.obj --w <m> --h <m> --d <m>
    [--origin x,y,z] [--scenario corr-scenario.json]   (--len = --d)
av-cli gen-mesh lroom -o l.obj --w <m> --d <m> --w2 <m> --d2 <m>
    --h <m> [--origin x,y,z] [--scenario l-scenario.json]
```

Бинарник: `build/<preset>/apps/cli/av-cli`. `-h` / `--help` печатают usage.

## engines

```bash
av-cli engines
```

Список зарегистрированных методов: id, имя, версия, артефакты.

```text
sabine-eyring  Sabine-Eyring Analytic RT60  v1.0.0  artifacts: metrics
image-source  Image Source Method  v1.0.0  artifacts: metrics ir
ray-tracing  Stochastic Ray Tracing  v0.1.0  artifacts: metrics ir rays field
hybrid  Hybrid (early/late RT)  v0.1.0  artifacts: metrics ir rays field
```

Если engines не зарегистрированы — `error: no engines registered`, exit 1.

## run

```bash
av-cli run tests/fixtures/demo-scenario.json -o out/
```

Читает scenario JSON, валидирует через engine (`params.engine`), пишет артефакты в `-o` (по умолчанию — текущий каталог):

| Файл | Когда |
|------|-------|
| `metrics.json` | engine выдаёт `metrics` |
| `ir.wav` | engine выдаёт `ir` |
| `field.json` | engine выдаёт `field` |

Тонкости:

- `mesh.uri` резолвится **относительно каталога scenario-файла**;
- warnings валидации — в stderr, ошибки — в stderr + exit 1;
- прогресс — в stderr (`progress: <stage> <N>%`);
- если engine не выдал ни одного артефакта — `error: engine produced no artifacts`, exit 1.

Примеры сценариев: `tests/fixtures/demo-scenario.json` (rect), `tests/fixtures/box-scenario.json` (mesh + поле и лучи).

## mesh

Scene → surface mesh OBJ + опциональный mesh-report:

```bash
av-cli mesh scene.json -o out.obj --report report.json --quality standard
```

| Параметр | Смысл |
|----------|--------|
| `scene.json` | входной Scene (`schemas/scene.schema.json`) |
| `-o` | выходной OBJ (default: рядом со scene, расширение `.obj`) |
| `--report` | записать mesh-report.json |
| `--quality` | draft / standard / fine — **полный** density override |

`--quality` заменяет quality сцены и сбрасывает custom `targetEdge` / `maxEdge`, чтобы применился пресет. Warnings — в stderr; ошибки валидации Scene — в stderr, exit ≠ 0. Формат report — см. [Mesh](/guide/mesh).

## gen-mesh

Готовые примитивы + placeholder-scenario:

```bash
av-cli gen-mesh box -o room.obj --w 6 --h 2.8 --d 4.5 --scenario room.json
av-cli gen-mesh corridor -o corr.obj --w 6 --h 2.8 --d 12 --scenario corr.json
av-cli gen-mesh lroom -o l.obj --w 6 --d 4 --w2 3 --d2 3 --h 2.8 --scenario l.json
```

| Форма | Размеры | Примечание |
|-------|---------|------------|
| `box` | `--w`, `--h`, `--d` | прямоугольная комната |
| `corridor` | `--w`, `--h`, `--d` | коридор; `--len` — синоним `--d` |
| `lroom` | `--w`, `--d`, `--w2`, `--d2`, `--h` | L-комната, два плеча |

| Параметр | Смысл |
|----------|--------|
| `-o` | выходной OBJ (обязателен) |
| `--origin` | `x,y,z` — начало (default: начало координат) |
| `--scenario` | записать placeholder scenario рядом с OBJ |

Placeholder-scenario: mesh-room + три материала + один source/receiver + `ray-tracing` + `fieldGrid`. `mesh.uri` — путь OBJ **относительно scenario-файла**, так что пара `gen-mesh … --scenario` сразу готова к `av-cli run`.

Пример полного цикла:

```bash
av-cli gen-mesh box -o room.obj --w 6 --h 2.8 --d 4.5 --scenario room.json
av-cli run room.json -o out/
```

## av-api (для сравнения)

| Флаг | Default | Смысл |
|------|---------|-------|
| `--port N`, `-p N` | 8787 | порт |
| `--bind ADDR` | `127.0.0.1` | адрес привязки |

Каталог источников ищется рядом с бинарником (или по `AV_SOURCE_CATALOG`). Запускайте из корня репозитория, если сценарии ссылаются на относительные `mesh.uri`.
