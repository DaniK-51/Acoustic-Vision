# GUI (P3)

Электронное приложение с 3D-редактором помещений.

**Статус:** placeholder. Реализация — фаза P3 (после API P1 и mesh P2).

## Идея

- three.js viewport, примитивы box/extrude, gizmos источников/приёмников.
- Палитра материалов; импорт OBJ/glTF.
- Связь с ядром **только** через REST/WS (`/v1`).
- Панель метода рендерится из `paramsSchema` (`GET /v1/engines`).

См. `docs/idea.md` §7.
