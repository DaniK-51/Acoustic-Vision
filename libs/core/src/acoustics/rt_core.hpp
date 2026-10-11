#pragma once

// Internal shared core for the mesh engines (ray-tracing, hybrid): sphere
// sampling, BVH specular bounce, energy deposit, cancel and progress, plus
// the validate() and post-run metrics assembly those engines share (the
// IR-derived metrics helper is also used by image-source). Lives beside the
// engine TUs and is *not* part of the public engine contract
// (docs/engines.md section 1) — do not include outside av_core.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <string>

#include <av/acoustics/engine.hpp>
#include <av/geometry/bvh.hpp>
#include <av/geometry/mesh.hpp>
#include <av/metrics/metrics.hpp>

namespace av::rtcore {

// methodParams keys shared by ray-tracing and hybrid (plus capturePaths,
// which SimParams also carries). Hybrid keeps earlyTimeSec / earlyOrder /
// useSabineTail on top of this subset.
struct RtParams {
  int rayCount = 2000;
  int maxBounces = 32;
  std::uint64_t seed = 42;
  double receiverRadius = 0.5;
  int capturePathLimit = 64;
  bool capturePaths = false;
};

// Shared subset parse, including the maxReflectionOrder fallback when
// methodParams has no maxBounces.
[[nodiscard]] RtParams parseRtParams(const SimParams& params);

// Slot name -> scenario material id: explicit override first, otherwise the
// slot name itself (applyMaterialOverrides may already have rewritten it).
[[nodiscard]] std::string materialIdForSlot(const Scenario& scenario,
                                            const std::string& slot);

[[nodiscard]] double slotAlpha(const Scenario& scenario, const Mesh& mesh,
                               std::uint32_t triangleIndex);

// Nearest listed band (simulation uses per-band tables without interpolation);
// mirrors the lookup used by analyticRt60 in metrics.cpp.
[[nodiscard]] double slotAlphaAtBand(const Scenario& scenario,
                                     const Mesh& mesh,
                                     std::uint32_t triangleIndex,
                                     double bandHz);

// Closed-mesh volume via the divergence theorem (sum of signed tetrahedra);
// abs() absorbs the into-room vs out-of-room winding convention.
[[nodiscard]] double meshVolume(const Mesh& mesh);

[[nodiscard]] double triangleArea(const Mesh& mesh, const Triangle& triangle);

// Area-weighted mean absorption over the mesh shell (broadband), used for the
// optional analytic Sabine tail.
[[nodiscard]] double meshMeanAlpha(const Scenario& scenario, const Mesh& mesh);

// Uniform direction on the unit sphere (Archimedes).
[[nodiscard]] Vec3 sampleDirection(std::mt19937_64& rng);

// Receiver-sphere hit at closest approach along the current segment:
// pairIndex = sourceIndex * receiverCount + receiverIndex, t is the arrival
// time in seconds, energy the amplitude^2 deposit, order the wall-hit count
// along the path so far (0 on the direct segment).
using ReceiverHitFn =
    std::function<void(std::size_t pairIndex, double t, double energy,
                       int order)>;

// One stochastic RT pass over every source ray: BVH specular bounce, receiver
// sphere detection (via onReceiverHit), optional field energy deposit,
// optional path capture, with cancel polled per ray and per bounce and
// decile progress. Returns false when cancel fired — outputs must be
// discarded.
[[nodiscard]] bool traceRays(const Scenario& scenario, const Mesh& mesh,
                             const Bvh& bvh, const RtParams& rt,
                             double speedOfSound, const CancelToken& cancel,
                             const ProgressFn& progress, const char* stageLabel,
                             const ReceiverHitFn& onReceiverHit,
                             SpatialField* field, RayPaths& paths);

// Monte Carlo mean over rays, additionally per cell volume so the stored
// value is an energy *density* (linear, not dB).
void normalizeFieldEnergy(SpatialField& field, std::size_t totalRays);

// --- Shared engine-side assembly ---

// Scenario gate for the mesh RT engines (ray-tracing, hybrid). `engineId`
// only prefixes the engine-specific error strings; the remaining checks
// (loaded mesh, sources/receivers, material-slot warnings, signedIr,
// fieldGrid) are identical for both.
[[nodiscard]] ValidateResult validateMeshRtScenario(const Scenario& scenario,
                                                    const std::string& engineId);

// Broadband IR-derived metrics from channel 0: schroederT30 + edt at band
// key 0.0 and early-reflection peaks (8 peaks, 80 ms window). No-op when the
// IR has no channels.
void addIrDerivedMetrics(const IrBuffer& ir, Metrics& metrics);

// Per-band analytic Sabine/Eyring over the mesh shell (area-weighted alpha
// at each band). bandsHz empty => kDefaultBandsHz. Writes the whole Rt60
// entry per band (schroederT30 stays 0), matching the pre-existing engines.
void addMeshAnalyticRt60(const Scenario& scenario, const Mesh& mesh,
                         const std::vector<double>& bandsHz,
                         double speedOfSound, Metrics& metrics);

}  // namespace av::rtcore
