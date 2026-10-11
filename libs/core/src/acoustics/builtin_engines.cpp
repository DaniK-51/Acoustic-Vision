#include <av/acoustics/builtin_engines.hpp>

namespace av {

// Defined in the engine translation units; referencing them pulls those TUs
// out of the static library so their REGISTER_ENGINE initializers run.
void forceLinkSabineEyringEngine();
void forceLinkImageSourceEngine();
void forceLinkRayTracingEngine();

void linkBuiltinEngines() {
  forceLinkSabineEyringEngine();
  forceLinkImageSourceEngine();
  forceLinkRayTracingEngine();
}

}  // namespace av
