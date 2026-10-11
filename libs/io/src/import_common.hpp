#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace av::detail {

// SI meters at import (AGENTS.md decision 14: convert only on import).
// Shared by every mesh importer so OBJ and glTF cannot drift apart.
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

// file:///C:/... leaves a leading slash before the Windows drive; strip the
// scheme and that slash so the result is a plain filesystem path.
// Shared by loadMeshRoom and fillLoadedMesh (ADR 36 URI policy).
inline std::filesystem::path meshUriToPath(const std::string& uri) {
  std::string value = uri;
  if (value.rfind("file://", 0) == 0) {
    value = value.substr(7);
    if (value.size() >= 3 && value[0] == '/' && value[2] == ':') {
      value = value.substr(1);
    }
  }
  return std::filesystem::path(value);
}

}  // namespace av::detail
