# Engines

Acoustics conventions, the Engine API, the four built-in engines, shared
`rt_core`, and `powerDb` semantics. Sources: `libs/core/src/acoustics/*`,
`docs/engines.md` §5–6 (Russian; authoritative for physics), headers under
`libs/core/include/av/`.

## 1. Physics conventions (ISM / RT) — DO NOT CHANGE SILENTLY

These rules are invariants of golden tests (`tests/unit/test_engines.cpp`,
`test_ray_tracing.cpp`, `test_metrics.cpp`).

| Rule | Formula / meaning |
|------|-------------------|
| Delay | `t = r / c`, default `c = 343.0 m/s` (`kSpeedOfSound`, ~20 °C) |
| Geometric spreading | amplitude `∝ 1/r` |
| Bounce | α is **energy** absorption: energy `× (1-α)`, amplitude `× √(1-α)`, **per band** |
| Energy identity | `energy = amplitude²` |
| Order N | reflection count; image-source enumerates a parity lattice over the rect room |
| Polarity | default output is an **unsigned energy envelope**; `signedIr` is accepted but **not produced** (engines warn) |
| ISM lattice | even index `n`: image at `n*L + s`; odd: flip across the far plane (`image-source_engine.cpp`) |
| Face order (rect) | `rectFaces()` = floor, ceiling, x+, x-, z+, z- |

Analytic RT60 (Sabine / Eyring) uses `T = 24·ln(10)·V / (c·S·ᾱ)` and the
Eyring `ln(1-ᾱ)` correction (`materials.hpp`). The familiar
`0.161·V/(S·ᾱ)` is the same formula at `c = 343`.

Metric definitions (`metrics.hpp`):

| Metric | Definition |
|--------|------------|
| Schroeder T30 | backward integration, linear fit −5…−35 dB, extrapolated to 60 dB; time zero = peak sample |
| EDT | fit 0…−10 dB of the Schroeder curve, reported as 60 dB equivalent |
| Early reflections | top-K local maxima in the first 80 ms window (default K = 8) |
| Sabine / Eyring | analytic per band over area-weighted mean α |

## 2. Engine API

```cpp
class IEngine {
  virtual EngineDescriptor descriptor() const = 0;
  virtual ValidateResult validate(const Scenario&) const = 0;
  virtual Result run(const Scenario&, const SimParams&,
                     const CancelToken&, ProgressFn) = 0;
};
```

- `ValidateResult`: `ok`, `errors`, `warnings` (code-based, no exceptions
  inside engines).
- `CancelToken`: atomic flag; engines **must** poll it in loops that can
  exceed ~100 ms. Cancelled run returns an empty/partial `Result` — JobManager
  discards it and marks the job `cancelled`.
- `ProgressFn`: optional callback with `{fraction, stage}`; last write wins,
  throttling is a presentation concern (JobWatcher polls at 10 Hz).

Registration: `REGISTER_ENGINE(EngineType)` (in the engine TU) →
`EngineRegistry::instance().add(factory)`. Duplicate or empty ids are
rejected. Because engines live in a static library, binaries must call
`av::linkBuiltinEngines()` first (`builtin_engines.cpp` force-links each TU).

## 3. Capabilities and artifacts

`Capabilities` (`engine.hpp`):

| Field | Meaning |
|-------|---------|
| `supportsRect` | accepts `Room::Type::Rect` |
| `supportsMesh` | accepts `Room::Type::Mesh` with `loadedMesh` |
| `supportsRayPaths` | can fill `Result.rays` |
| `supportsBands` | per-band metrics |
| `maxOrder` | 0 = N/A. All four built-in engines report **0** today (including `image-source`, which still honors `params.maxReflectionOrder`); a non-zero value would advertise a hard order cap for a future engine |

`artifacts` is the subset of `{"ir","metrics","field","rays","findings"}` the
engine actually produces. GUI/API render only advertised kinds. Today no
engine fills `findings` (`struct Findings {};`).

## 4. Built-in engines

| id | Model | Room | Artifacts | Version |
|----|-------|------|-----------|---------|
| `sabine-eyring` | analytic T60 (Sabine + Eyring per band) | rect | `metrics` | 1.0.0 |
| `image-source` | image sources, order `maxReflectionOrder` | rect | `metrics`, `ir` | 1.0.0 |
| `ray-tracing` | stochastic RT over mesh + BVH | mesh | `metrics`, `ir`, `rays`, `field` | 0.1.0 |
| `hybrid` | one RT pass, early/late IR split | mesh | `metrics`, `ir`, `rays`, `field` | 0.1.0 |

Shared validation gates live in `rt_core`:
`validateRectScenario` (rect room, positive size, resolvable face materials)
and `validateMeshRtScenario` (mesh + `loadedMesh`, sources/receivers,
material-slot warnings, `signedIr` warning, fieldGrid clamping).

`image-source` additionally requires ≥1 source and ≥1 receiver **inside** the
rect room, and builds a broadband IR (face energy reflect `1 - meanAbsorption`).

### Hybrid specifics (`hybrid_engine.cpp`)

One stochastic RT pass. Deposits with `pathTime < earlyTimeSec` **or**
reflection order ≤ `earlyOrder` go to the early part of the IR (dense
sub-sample deposit); the rest is late. Optional `useSabineTail` replaces the
late part with an analytic Sabine tail. Extra `methodParams` on top of the RT
subset: `earlyTimeSec` (default 0.08), `earlyOrder` (default 2),
`useSabineTail` (default false).

## 5. paramsSchema (methodParams)

JSON Schema per engine, served in `EngineDescriptor.paramsSchema`, all with
`"additionalProperties": false`.

**sabine-eyring, image-source**: `{}` (no methodParams).

**ray-tracing** (`RtParams` defaults in `rt_core.hpp`):

| Key | Type | Default |
|-----|------|---------|
| `rayCount` | integer ≥ 1 | 2000 |
| `maxBounces` | integer ≥ 1 | 32 |
| `seed` | integer ≥ 0 | 42 |
| `receiverRadius` | number > 0 | 0.5 |
| `capturePathLimit` | integer ≥ 0 | 64 |

**hybrid**: same RT subset plus `earlyTimeSec` (number ≥ 0, 0.08),
`earlyOrder` (integer ≥ 0, 2), `useSabineTail` (boolean, false).

`SimParams.capturePaths` (and `methodParams.capturePaths`) enable path
capture up to `capturePathLimit` per source/receiver context. When
`maxBounces` is absent from `methodParams`, RT falls back to
`params.maxReflectionOrder`.

## 6. rt_core (internal, not public API)

`libs/core/src/acoustics/rt_core.hpp/.cpp` — shared by `ray-tracing` and
`hybrid` (and the IR-derived metrics helper used by `image-source`). **Do not
include outside `av_core`.** Not part of the engine plugin contract.

Contents:

| Piece | Role |
|-------|------|
| `RtParams`, `parseRtParams` | shared methodParams subset |
| `materialIdForSlot`, `slotAlpha`, `slotAlphaAtBand` | mesh slot → scenario material → α (nearest listed band, no interpolation) |
| `meshVolume`, `triangleArea`, `meshMeanAlpha` | geometry stats (divergence-theorem volume; abs() absorbs winding) |
| `sampleDirection` | uniform unit-sphere direction (Archimedes) |
| `traceRays` | one stochastic pass: BVH specular bounce, receiver-sphere detection at closest approach, optional field deposit, optional path capture; cancel polled per ray and per bounce; decile progress |
| `normalizeFieldEnergy` | Monte Carlo mean per ray **and per cell volume** → energy density |
| `prepareRtRun` | mesh presence, sampleRate/duration/c gates, optional field grid over mesh AABB, zeroed broadband `IrBuffer` with `"src:rcv"` channel names |
| `addIrDerivedMetrics` | schroederT30 + edt at band key 0.0, 8 early peaks / 80 ms |
| `addMeshAnalyticRt60` | per-band Sabine/Eyring over the mesh shell (assigns `.sabine`/`.eyring` only) |

Receiver-sphere deposits: energy with amplitude `∝ 1/L` (total path length,
matching ISM 1/r), `energy = amplitude²`, `order` = wall-hit count so far.

## 7. powerDb semantics

`Source::powerDb` is a **power level in dB**, not a linear watt value and not
an amplitude. Engines convert to a linear **energy** scale:

```cpp
const double sourceEnergy = std::pow(10.0, source.powerDb / 10.0);  // rt_core.cpp
```

So `powerDb = 0` → energy 1, `powerDb = 10` → energy 10, `powerDb = -10` →
0.1. This scale multiplies the per-ray energy budget; geometric 1/r and
bounce factors apply on top. Band curves of a `directivity` are **relative dB
gains**, not absolute levels, and are currently **not** applied by the RT
engines (see `gotchas.md`).

## 8. Where implementations live

| Engine | TU | Shared |
|--------|----|--------|
| sabine-eyring | `sabine_eyring_engine.cpp` | `rtcore::validateRectScenario`, `analyticRt60` |
| image-source | `image_source_engine.cpp` | `rtcore::validateRectScenario`, `addIrDerivedMetrics` |
| ray-tracing | `ray_tracing_engine.cpp` | `rt_core.*` |
| hybrid | `hybrid_engine.cpp` | `rt_core.*` |

`engines/` directory is reserved for future standalone/plugin modules and
currently contains only a README. Dynamic DLL C-ABI loading is phase P2.5 and
is **not implemented**.
