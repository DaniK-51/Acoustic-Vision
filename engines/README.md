# engines/

Зарезервированный каталог под **будущие standalone-модули** методов расчёта (dynamic DLL / вынос движков из core).

Сейчас пуст. Действующие in-repo engines лежат в `libs/core/src/acoustics/`:
`sabine_eyring_engine.cpp`, `image_source_engine.cpp`, `ray_tracing_engine.cpp`,
`hybrid_engine.cpp` (+ общий `rt_core.*`), регистрация — `builtin_engines.cpp`.

Контракт и инструкция по добавлению метода: [`docs/engines.md`](../docs/engines.md).
