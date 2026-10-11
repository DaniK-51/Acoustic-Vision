# Методы расчёта (Engine API)

Главное требование: **новый метод = один модуль + регистрация**, без правок API, GUI и чужих движков.

## 1. Контракт

```cpp
struct Capabilities {
  bool supportsRect = true;
  bool supportsMesh = false;
  bool supportsRayPaths = false;
  bool supportsBands = true;
  int  maxOrder = 0;          // 0 = не применимо
};

struct EngineDescriptor {
  std::string id;             // "image-source"
  std::string name;           // "Image Source Method"
  std::string version;        // "1.0.0"
  Capabilities caps;
  nlohmann::json paramsSchema; // JSON Schema → GUI рисует форму
  std::vector<std::string> artifacts; // subset of ir|metrics|field|rays|findings
};

struct ValidateResult {
  bool ok = true;
  std::vector<std::string> errors;
  std::vector<std::string> warnings;
};

struct Progress { double fraction; std::string stage; };
using ProgressFn = std::function<void(const Progress&)>;

class CancelToken {
public:
  void cancel();
  bool cancelled() const;
};

class IEngine {
public:
  virtual ~IEngine() = default;
  virtual EngineDescriptor descriptor() const = 0;
  virtual ValidateResult validate(const Scenario&) const = 0;
  virtual Result run(const Scenario&, const SimParams&,
                     const CancelToken&, ProgressFn) = 0;
};
```

Регистрация:

```cpp
REGISTER_ENGINE(ImageSourceEngine);
```

`EngineRegistry` отдаёт список descriptor'ов и фабрики по `id`.

## 2. Как добавить метод (на этапе реализации)

1. Создать `engines/<name>/` (по образцу `_template/`, появится в P0).
2. Реализовать `IEngine`: `descriptor` (+ `paramsSchema`), `validate`, `run`.
3. Зарегистрировать `REGISTER_ENGINE(...)`.
4. Добавить unit/golden-тест, если есть аналитическая проверка.
5. Собрать. Метод появится в `GET /v1/engines` и в GUI без правок UI.

## 3. Модель расширяемости: A → B

| Ступень | Когда | Суть |
|---------|-------|------|
| **A. Registry** | P0–P2 | Статическая регистрация, пересборка. Достаточно для методов в репо. |
| **B. DLL C-ABI** | P2.5+ | `LoadLibrary` + стабильный `av_engine_api_v1`. Внешние/закрытые методы без пересборки. |

Контракт (descriptor, schema, Result, cancel) уже «плагинный» — B не ломает A.

**Не** делаем ядром: process/WASM engines (только для research-методов), pipeline stages (внутренняя деталь hybrid), config-only формулы (могут быть engines поверх A).

## 4. Методы в roadmap

| id | Модель | Комнаты | Выдаёт | Фаза |
|----|--------|---------|--------|------|
| `sabine-eyring` | Аналитика T=0.161·V/(S·ᾱ) | любые (V, S, α) | RT60 | P0 |
| `image-source` | Зеркальные источники | rect | IR, метрики, ранние пути | P0 |
| `ray-tracing` | Лучи + BVH | mesh | IR, метрики, paths, поле | P2 |
| `hybrid` | ISM early + RT late | mesh | полное IR | P2 |
| `fdtd` / моды | Волновое | small rect | LF-моды | later / plugin |

**`hybrid` (P2g).** Один стохастический RT-проход на mesh (как `ray-tracing`);
депозиты с `pathTime < earlyTimeSec` (по умолчанию 0.08 с) **или** порядком
отражений ≤ `earlyOrder` (по умолчанию 2) идут в раннюю часть IR (плотный
суб-сэмпл депозит), остальное — в позднюю; сумма — одно `IrBuffer`.
Опционально `useSabineTail` заменяет позднюю часть аналитическим хвостом
Сэбина. `methodParams`: `earlyTimeSec`, `earlyOrder`, `rayCount`,
`maxBounces`, `seed`, `receiverRadius`, `useSabineTail`, `capturePathLimit`.
Валидация требует mesh + `loadedMesh` (rect отклоняется). Артефакты:
`metrics`, `ir`, `rays`, `field` (rays/field — как у `ray-tracing`, при
capture/grid).

## 5. Физические конвенции (ISM)

Не менять молча — это источник golden-тестов.

| Правило | Формула / смысл |
|---------|-----------------|
| Задержка | `t = r / c`, `c = 343 м/с` (по умолчанию) |
| Геометрическое затухание | амплитуда `∝ 1/r` |
| Отскок | α — **энергетическое** поглощение: energy `× (1-α)`, amplitude `× √(1-α)` **на полосу** |
| Energy | `energy = amplitude²` |
| Порядок N | число отражений; lattice enumeration для rect |
| Полярность | по умолчанию energy envelope (без знака); signed IR — опция |

## 6. Метрики

| Метрика | Определение |
|---------|-------------|
| **RT60 Schroeder** | backward integration, T30: линейный фит −5…−35 дБ |
| **RT60 Sabine** | `T = 0.161 · V / (S · ᾱ)` |
| **RT60 Eyring** | с поправкой на ln(1−ᾱ) |
| **EDT** | фит 0 → −10 дБ |
| **EarlyReflections** | top-K пиков в первые 80 мс, (опц.) path polyline |

## 7. API для методов

| Метод | Путь | Назначение |
|-------|------|------------|
| GET | `/v1/engines` | список `EngineDescriptor` |
| POST | `/v1/simulations` | `params.engine` + `params.methodParams` |
| GET | `/v1/simulations/{id}/paths` | polylines лучей (для 3D GUI) |

`methodParams` валидируется по `paramsSchema` конкретного engine.

## 8. Шаблон (появится в P0)

```
engines/_template/
├── CMakeLists.txt
├── include/av/engines/template_engine.hpp
├── src/template_engine.cpp
└── README.md
```

Репозиторий сейчас на стадии архитектуры — исходников ещё нет.
