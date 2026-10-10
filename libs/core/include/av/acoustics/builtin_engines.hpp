#pragma once

namespace av {

// Call from an executable entry point so the linker keeps built-in engine
// translation units (their REGISTER_ENGINE static initializers run at load).
void linkBuiltinEngines();

}  // namespace av
