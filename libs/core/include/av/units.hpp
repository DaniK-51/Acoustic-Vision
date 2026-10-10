#pragma once

#include <array>

namespace av {

// m/s at ~20 C; documented default (docs/engines.md section 5).
inline constexpr double kSpeedOfSound = 343.0;

// Default octave-band center frequencies, Hz.
inline constexpr std::array<double, 6> kDefaultBandsHz{
    125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};

}  // namespace av
