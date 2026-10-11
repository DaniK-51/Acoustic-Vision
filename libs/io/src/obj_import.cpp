#include <av/io/io.hpp>

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "import_common.hpp"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

namespace av {
namespace {

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

}  // namespace

Mesh loadObj(const std::filesystem::path& path, const std::string& unit,
             double scale) {
  if (!(scale > 0.0)) {
    throw std::runtime_error("mesh scale must be > 0");
  }
  const double toMeters = detail::unitToMeters(unit) * scale;

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

}  // namespace av
