// In-app engine documentation (plan-v0.1.1 E1–E2), authored from docs/engines.md.
// Descriptor fields (id/name/version/artifacts/paramsSchema) still come from
// GET /v1/engines; this map adds the prose the API does not carry: what the
// method computes, when to use it, and short parameter hints.

export interface EngineDoc {
  /** 1–2 short paragraphs: what the method computes (not a physics treatise). */
  summary: string[];
  /** When to use / limitations (rect vs mesh, LF, noise, cost). */
  usage: string[];
  /** methodParams name → short hint; default/range come from paramsSchema. */
  paramHints: Record<string, string>;
}

/** Result-artifact kind → one-line meaning (docs/engines.md §1, Result). */
export const ARTIFACT_LABELS: Record<string, string> = {
  metrics: 'метрики: RT60 (Schroeder/Sabine/Eyring), EDT, ранние отражения',
  ir: 'импульсная характеристика (IrBuffer)',
  field: 'пространственное поле энергии по ячейкам сетки',
  rays: 'полилинии путей лучей (при capturePaths)',
  findings: 'замечания движка',
};

export const ENGINE_DOCS: Record<string, EngineDoc> = {
  'sabine-eyring': {
    summary: [
      'Аналитическая оценка времени реверберации RT60 по формулам Сэбина T = 0.161·V/(S·ᾱ) и Эйринга (с поправкой ln(1−ᾱ)). Среднее поглощение ᾱ — площадевзвешенное по граням, отдельно для каждой полосы частот.',
      'Не строит ИХ и не считает ранние отражения — только метрики RT60. Работает мгновенно и детерминированно.',
    ],
    usage: [
      'только rect-комнаты (все грани должны иметь материал)',
      'первая оценка RT60 и сверка с image-source / ray-tracing',
      'нет ИХ, EDT, ранних отражений, поля и лучей',
      'предполагается диффузное поле: низкочастотные моды и сильная неравномерность не моделируются',
    ],
    paramHints: {},
  },
  'image-source': {
    summary: [
      'Метод зеркальных источников (ISM): точная геометрия отражений в прямоугольной комнате через решётку образов источника до порядка params.maxReflectionOrder. Задержка t = r/c, затухание ∝ 1/r, отскок ×√(1−α) на полосу (энергия ×(1−α)).',
      'Выдаёт ИХ и метрики по нему (Schroeder T30, EDT, ранние отражения) плюс аналитический RT60 Сэбина/Эйринга для сравнения.',
    ],
    usage: [
      'только rect-комнаты; источники и приёмники — внутри комнаты',
      'эталон для ранних отражений и проверки стохастических методов',
      'порядок ограничен params.maxReflectionOrder — поздний хвост ИХ не достраивается',
      'mesh-геометрия не поддерживается (для неё ray-tracing / hybrid)',
    ],
    paramHints: {},
  },
  'ray-tracing': {
    summary: [
      'Стохастическая трассировка лучей по mesh-геометрии (BVH). Из каждого источника запускается rayCount лучей; каждый отражается до maxBounces, энергия на отскоке ×(1−α). Попадание в сферу радиуса receiverRadius вокруг приёмника депозитит энергию в ИХ.',
      'Тот же проход пишет поле энергии (если включён fieldGrid) и polylines путей лучей. Усреднение Монте-Карло по лучам; результат воспроизводим при одном seed.',
    ],
    usage: [
      'только mesh-комнаты с загруженной геометрией (rect отклоняется)',
      'полный ИХ, поле и визуализация лучей на сложной геометрии',
      'геометрическая акустика: LF-моды и волновые эффекты не моделируются',
      'качество ИХ зависит от rayCount / maxBounces; мало лучей → шумная огибающая',
      'выход — энергетическая огибающая (unsigned), signedIr не выдаётся',
    ],
    paramHints: {
      rayCount: 'число лучей на источник; больше — чище ИХ, дольше расчёт',
      maxBounces: 'максимум отражений на луч; если не задано, берётся params.maxReflectionOrder',
      seed: 'зерно ГПСЧ: одинаковый seed → воспроизводимый результат',
      receiverRadius: 'радиус сферы приёмника для депозита энергии в ИХ',
      capturePathLimit: 'сколько путей лучей сохранить в rays.paths (0 — не сохранять)',
    },
  },
  hybrid: {
    summary: [
      'Один стохастический RT-проход на mesh (как ray-tracing) с разделением early/late. Депозиты с временем пути < earlyTimeSec (по умолчанию 0.08 с) или порядком отражений ≤ earlyOrder (по умолчанию 2) уходят в раннюю часть ИХ плотным суб-сэмпл депозитом; остальное — в позднюю. Сумма — одно IrBuffer.',
      'Опционально useSabineTail заменяет позднюю часть аналитическим хвостом Сэбина. Как и ray-tracing, при capture/grid пишет лучи и поле.',
    ],
    usage: [
      'только mesh-комнаты с загруженной геометрией (rect отклоняется)',
      'нужен «полный» ИХ: плотные ранние отражения + статистический поздний хвост',
      'границу early/late задают earlyTimeSec и earlyOrder',
      'LF-моды не моделируются; поздняя часть — статистика (или хвост Сэбина), не детальные пути',
    ],
    paramHints: {
      earlyTimeSec: 'граница ранней части по времени пути, с',
      earlyOrder: 'граница ранней части по порядку отражений',
      rayCount: 'число лучей на источник; больше — чище ИХ, дольше расчёт',
      maxBounces: 'максимум отражений на луч; если не задано, берётся params.maxReflectionOrder',
      seed: 'зерно ГПСЧ: одинаковый seed → воспроизводимый результат',
      receiverRadius: 'радиус сферы приёмника для депозита энергии в ИХ',
      useSabineTail: 'замена поздней части ИХ аналитическим хвостом Сэбина',
      capturePathLimit: 'сколько путей лучей сохранить в rays.paths (0 — не сохранять)',
    },
  },
};

/** Doc entry for an engine id, or null when the engine has no prose yet. */
export function engineDoc(id: string): EngineDoc | null {
  return ENGINE_DOCS[id] ?? null;
}
