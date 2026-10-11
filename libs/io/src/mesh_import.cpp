#include <av/io/io.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "import_common.hpp"

namespace av {
namespace {

// Shared tail of loadMeshRoom / fillLoadedMesh: format dispatch with the
// room's unit/scale, then its materialOverrides. Callers resolve the uri.
Mesh loadMeshForRoom(const std::filesystem::path& path, const MeshRoom& room) {
  Mesh mesh = loadMesh(path, room.unit, room.scale);
  applyMaterialOverrides(mesh, room.materialOverrides);
  return mesh;
}

}  // namespace

void applyMaterialOverrides(
    Mesh& mesh, const std::map<std::string, std::string>& overrides) {
  for (std::string& slot : mesh.materialSlots) {
    const auto it = overrides.find(slot);
    if (it != overrides.end()) {
      slot = it->second;
    }
  }
}

Mesh loadMesh(const std::filesystem::path& path, const std::string& unit,
              double scale) {
  std::string extension = path.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  if (extension == ".obj") {
    return loadObj(path, unit, scale);
  }
  if (extension == ".gltf" || extension == ".glb") {
    return loadGltf(path, unit, scale);
  }
  throw std::runtime_error("unsupported mesh format: " + path.string());
}

Mesh loadMeshRoom(const MeshRoom& room) {
  return loadMeshForRoom(detail::resolveMeshUri(room.uri), room);
}

void fillLoadedMesh(Scenario& scenario, const std::filesystem::path& baseDir) {
  if (scenario.room.type != Room::Type::Mesh) {
    return;
  }
  if (scenario.loadedMesh != nullptr) {
    return;
  }
  const std::string& uri = scenario.room.mesh.uri;
  if (uri.empty()) {
    return;
  }

  const std::filesystem::path path = detail::resolveMeshUri(uri, baseDir);

  try {
    Mesh mesh = loadMeshForRoom(path, scenario.room.mesh);
    if (mesh.triangles.empty() || mesh.positions.empty()) {
      throw std::runtime_error("mesh has no geometry");
    }
    scenario.loadedMesh =
        std::make_shared<const Mesh>(std::move(mesh));
  } catch (const std::exception& error) {
    throw std::runtime_error("failed to load mesh '" + uri + "' from '" +
                             path.string() + "': " + error.what());
  }
}

}  // namespace av
