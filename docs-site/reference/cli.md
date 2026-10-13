# av-cli

Консольный инструмент: запуск расчётов headless, список engines, генерация mesh-примитивов и mesh из Scene.

## Общий вид

```text
av-cli engines
av-cli run <scenario.json> [-o outdir]
av-cli mesh <scene.json> [-o out.obj] [--report report.json]
    [--quality draft|standard|fine]
av-cli gen-mesh box -o room.obj --w <m> --h <m> --d <m>
    [--origin x,y,z] [--scenario room-scenario.json]
av-cli gen-mesh corridor -o corr.obj --w <m> --h <m> --d <m>
    [--origin x,y,z] [--scenario corr-scenario.json]  (--len = --d)
av-cli gen-mesh lroom -o l.obj --w <m> --d <m> --w2 <m> --d2 <m>
    --h <m> [--origin x,y,z] [--scenario l-scenario.json]
```

`-h` / `--help` — usage. Неизвестная команда → usage + exit 1.

Путь к бинарнику (linux-debug):

```bash
build/linux-debug/apps/cli/av-cli …
```

---

## `engines`

Печатает зарегистрированные методы: `id`, `name`, `version`, список `artifacts`.

```bash
av-cli engines
# sabine-eyring  Sabine-Eyring RT60  v…  artifacts: metrics
# image-source  Image Source Method  v…  artifacts: metrics ir
# …
```

Если engines не зарегистрированы — `error: no engines registered`, exit 1.

---

## `run`

```bash
av-cli run <scenario.json> [-o outdir]
```

| Аргумент | Default | Смысл |
|----------|---------|--------|
| `scenario.json` | — | входной scenario |
| `-o`, `--out` | `.` | выходная директория |

Что делает:

1. Читает scenario (`av::loadScenario`).
2. Резолвит `mesh.uri` **относительно каталога scenario-файла**.
3. Создаёт engine по `params.engine`, прогоняет `validate`.
4. Печатает warnings / errors; при ошибках validate — exit 1.
5. Создаёт выходную директорию, запускает `engine->run` с progress в stderr.
6. Пишет артефакты:

| Файл | Когда |
|------|--------|
| `metrics.json` | engine выдал `metrics` |
| `ir.wav` | engine выдал `ir` |
| `field.json` | engine выдал `field` |

Если engine не дал ни одного артефакта — `error: engine produced no artifacts`, exit 1.

Пример:

```bash
av-cli run tests/fixtures/demo-scenario.json -o out/
av-cli run tests/fixtures/box-scenario.json -o out-mesh/
```

---

## `gen-mesh`

Готовые mesh-примитивы + placeholder scenario (для быстрого старта RT).

### box

```bash
av-cli gen-mesh box -o room.obj --w 6 --h 2.8 --d 4.5 \
  [--origin x,y,z] [--scenario room.json]
```

### corridor

```bash
av-cli gen-mesh corridor -o corr.obj --w 6 --h 2.8 --d 12 \
  [--origin x,y,z] [--scenario corr.json]
```

`--len` — синоним `--d` (длина коридора).

### lroom

```bash
av-cli gen-mesh lroom -o l.obj --w 6 --d 4 --w2 3 --d2 3 --h 2.8 \
  [--origin x,y,z] [--scenario l.json]
```

| Параметр | Смысл |
|----------|--------|
| `-o` | выходной OBJ (обязателен) |
| `--w`, `--h`, `--d` | размеры, м |
| `--w2`, `--d2` | второе плечо L-комнаты |
| `--origin` | `x,y,z` |
| `--scenario` | записать placeholder scenario рядом с OBJ |

Placeholder-scenario: mesh-room, три материала, один source/receiver, engine `ray-tracing`, `fieldGrid`, `capturePaths: true`. `mesh.uri` — путь OBJ **относительно scenario-файла**. Для L-комнат source/receiver ставятся в «залитые» плечи, а не в bbox-центр (там может быть вырез).

На stdout — `wrote <path> (N vertices, M triangles)`.

---

## `mesh`

Scene → surface mesh OBJ (+ optional mesh-report).

```bash
av-cli mesh scene.json -o out.obj --report report.json --quality standard
```

| Аргумент | Default | Смысл |
|----------|---------|--------|
| `scene.json` | — | входной Scene (`schemas/scene.schema.json`) |
| `-o`, `--out` | рядом, расширение `.obj` | выходной OBJ |
| `--report` | — | записать mesh-report.json |
| `--quality` | — | draft / standard / fine |

`--quality` — **полный density override**: заменяет quality сцены и сбрасывает custom `targetEdge`/`maxEdge`, чтобы применился пресет.

Warnings генерации — в stderr. Ошибки `validateScene` — в stderr, exit 1.

Пример:

```bash
av-cli mesh scene.json -o out.obj --report report.json --quality fine
```

---

## Коды выхода

| Код | Когда |
|-----|--------|
| 0 | успех |
| 1 | нет аргументов / неизвестная команда / ошибка parse / validate / IO / no artifacts |

Сообщения об ошибках — префикс `error:`; предупреждения — `warning:`.

---

## Замечания

| Тема | Правило |
|------|---------|
| Mesh URI в `run` | каталог scenario-файла |
| Mesh URI в `av-api` | CWD процесса |
| Единицы | метры; конвертация — при импорте |
| Progress | stderr: `progress: <stage> <N>%` |
| Отмена | в CLI нет (API — `POST /v1/jobs/{id}/cancel`) |
