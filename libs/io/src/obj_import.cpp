#include <av/io/io.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace av {
namespace {

// SI meters at import (AGENTS.md decision 14: convert only on import).
double unitToMeters(const std::string& unit) {
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

std::string zoneNameForFace(const tinyobj::shape_t& shape,
                            const std::vector<tinyobj::material_t>& materials,
                            std::size_t face) {
  if (face < shape.mesh.material_ids.size()) {
    const int materialId = shape.mesh.material_ids[face];
    if (materialId >= 0) {
      const auto index = static_cast<std::size_t>(materialId);
      if (index < materials.size() && !materials[index].name.empty()) {
        return materials[index].name;
      }
    }
  }
  return shape.name.empty() ? "default" : shape.name;
}

// file:///C:/... leaves a leading slash before the Windows drive; strip the
// scheme and that slash so the result is a plain filesystem path.
[[nodiscard]] std::filesystem::path meshUriToPath(const std::string& uri) {
  std::string value = uri;
  if (value.rfind("file://", 0) == 0) {
    value = value.substr(7);
    if (value.size() >= 3 && value[0] == '/' && value[2] == ':') {
      value = value.substr(1);
    }
  }
  return std::filesystem::path(value);
}

}  // namespace

Mesh loadObj(const std::filesystem::path& path, const std::string& unit,
             double scale) {
  if (!(scale > 0.0)) {
    throw std::runtime_error("mesh scale must be > 0");
  }
  const double toMeters = unitToMeters(unit) * scale;

  tinyobj::ObjReaderConfig config;
  config.triangulate = true;
  tinyobj::ObjReader reader;
  if (!reader.ParseFromFile(path.string(), config)) {
    throw std::runtime_error("OBJ parse failed (" + path.string() + "): " +
                             reader.Error());
  }

  const tinyobj::attrib_t& attrib = reader.GetAttrib();
  const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
  const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();

  Mesh mesh;
  mesh.positions.reserve(attrib.vertices.size() / 3);
  for (std::size_t i = 0; i + 2 < attrib.vertices.size(); i += 3) {
    mesh.positions.push_back(
        {static_cast<double>(attrib.vertices[i]) * toMeters,
         static_cast<double>(attrib.vertices[i + 1]) * toMeters,
         static_cast<double>(attrib.vertices[i + 2]) * toMeters});
  }

  std::map<std::string, std::uint32_t> slotByName;
  auto slotOf = [&](const std::string& name) -> std::uint32_t {
    const auto it = slotByName.find(name);
    if (it != slotByName.end()) {
      return it->second;
    }
    const auto index = static_cast<std::uint32_t>(mesh.materialSlots.size());
    mesh.materialSlots.push_back(name);
    slotByName.emplace(name, index);
    return index;
  };

  for (const tinyobj::shape_t& shape : shapes) {
    const tinyobj::mesh_t& shapeMesh = shape.mesh;
    std::size_t indexOffset = 0;
    for (std::size_t face = 0; face < shapeMesh.num_face_vertices.size();
         ++face) {
      const int faceVertices = shapeMesh.num_face_vertices[face];
      const std::uint32_t slot = slotOf(zoneNameForFace(shape, materials, face));

      // triangulate = true, but fan-triangulate defensively for robustness.
      for (int corner = 2; corner < faceVertices; ++corner) {
        Triangle triangle;
        const std::size_t corners[3] = {
            indexOffset + 0, indexOffset + static_cast<std::size_t>(corner - 1),
            indexOffset + static_cast<std::size_t>(corner)};
        triangle.v0 = static_cast<std::uint32_t>(
            shapeMesh.indices[corners[0]].vertex_index);
        triangle.v1 = static_cast<std::uint32_t>(
            shapeMesh.indices[corners[1]].vertex_index);
        triangle.v2 = static_cast<std::uint32_t>(
            shapeMesh.indices[corners[2]].vertex_index);
        triangle.materialSlot = slot;
        mesh.triangles.push_back(triangle);
      }
      indexOffset += static_cast<std::size_t>(faceVertices);
    }
  }

  return mesh;
}

void applyMaterialOverrides(
    Mesh& mesh, const std::map<std::string, std::string>& overrides) {
  for (std::string& slot : mesh.materialSlots) {
    const auto it = overrides.find(slot);
    if (it != overrides.end()) {
      slot = it->second;
    }
  }
}

Mesh loadMeshRoom(const MeshRoom& room) {
  Mesh mesh = loadObj(meshUriToPath(room.uri), room.unit, room.scale);
  applyMaterialOverrides(mesh, room.materialOverrides);
  return mesh;
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

  std::filesystem::path path = meshUriToPath(uri);
  if (path.is_relative() && !baseDir.empty()) {
    path = baseDir / path;
  }

  try {
    Mesh mesh = loadObj(path, scenario.room.mesh.unit, scenario.room.mesh.scale);
    if (mesh.triangles.empty() || mesh.positions.empty()) {
      throw std::runtime_error("mesh has no geometry");
    }
    applyMaterialOverrides(mesh, scenario.room.mesh.materialOverrides);
    scenario.loadedMesh =
        std::make_shared<const Mesh>(std::move(mesh));
  } catch (const std::exception& error) {
    throw std::runtime_error("failed to load mesh '" + uri + "' from '" +
                             path.string() + "': " + error.what());
  }
}

}  // namespace av
