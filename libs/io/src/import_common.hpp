#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace av::detail {

// SI meters at import (AGENTS.md decision 14: convert only on import).
// Unit table shared by every mesh importer so OBJ and glTF cannot drift apart.
inline double unitToMeters(const std::string& unit) {
  if (unit == "m") {
    return 1.0;
  }
  if (unit == "mm") {
    return 0.001;
  }
  if (unit == "cm") {
    return 0.01;
  }
  if (unit == "in") {
    return 0.0254;
  }
  throw std::runtime_error("unknown mesh unit: " + unit);
}

// unitToMeters(unit) * scale. Validates scale first so OBJ and glTF reject
// bad input in the same order with the same message.
inline double applyUnitScale(const std::string& unit, double scale) {
  if (!(scale > 0.0)) {
    throw std::runtime_error("mesh scale must be > 0");
  }
  return unitToMeters(unit) * scale;
}

// uri -> filesystem path, relative paths joined with `baseDir` when non-empty
// (CLI passes the scenario directory, API the CWD). Absolute paths and an
// empty baseDir leave the result as parsed. file:///C:/... leaves a leading
// slash before the Windows drive; strip the scheme and that slash so the
// result is a plain filesystem path (ADR 36 URI policy). Single mesh-uri
// entry point: loadMeshRoom and fillLoadedMesh resolve only here.
inline std::filesystem::path resolveMeshUri(
    const std::string& uri, const std::filesystem::path& baseDir = {}) {
  std::string value = uri;
  if (value.rfind("file://", 0) == 0) {
    value = value.substr(7);
    if (value.size() >= 3 && value[0] == '/' && value[2] == ':') {
      value = value.substr(1);
    }
  }
  std::filesystem::path path(value);
  if (path.is_relative() && !baseDir.empty()) {
    path = baseDir / path;
  }
  return path;
}

}  // namespace av::detail
