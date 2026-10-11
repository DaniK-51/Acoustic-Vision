#include <av/io/io.hpp>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "import_common.hpp"

// Use the nlohmann::json already pulled in by av/io/io.hpp so tinygltf's
// bundled copy cannot fight it over the include guard; no image decode.
#define TINYGLTF_NO_INCLUDE_JSON
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_IMPLEMENTATION
#include <tiny_gltf.h>

namespace av {
namespace {

const std::uint8_t* accessorBytes(const tinygltf::Model& model,
                                  const tinygltf::Accessor& accessor) {
  if (accessor.bufferView < 0 ||
      static_cast<std::size_t>(accessor.bufferView) >= model.bufferViews.size()) {
    throw std::runtime_error("glTF accessor has no bufferView");
  }
  const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  if (view.buffer < 0 ||
      static_cast<std::size_t>(view.buffer) >= model.buffers.size()) {
    throw std::runtime_error("glTF bufferView has no buffer");
  }
  const tinygltf::Buffer& buffer = model.buffers[view.buffer];
  const std::size_t offset =
      static_cast<std::size_t>(view.byteOffset) +
      static_cast<std::size_t>(accessor.byteOffset);
  if (offset > buffer.data.size()) {
    throw std::runtime_error("glTF accessor offset out of range");
  }
  return buffer.data.data() + offset;
}

std::size_t componentByteSize(int componentType) {
  switch (componentType) {
    case TINYGLTF_COMPONENT_TYPE_BYTE:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return 1;
    case TINYGLTF_COMPONENT_TYPE_SHORT:
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
      return 2;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
    case TINYGLTF_COMPONENT_TYPE_FLOAT:
      return 4;
    default:
      throw std::runtime_error("glTF accessor has unsupported componentType");
  }
}

// 0 for tightly packed accessors (glTF default); otherwise the view stride.
std::size_t elementStride(const tinygltf::Model& model,
                          const tinygltf::Accessor& accessor,
                          std::size_t packedSize) {
  if (accessor.bufferView < 0) {
    return packedSize;
  }
  const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
  return view.byteStride == 0 ? packedSize
                              : static_cast<std::size_t>(view.byteStride);
}

// Zone naming matches OBJ: material name -> mesh name -> "default".
std::string zoneNameForPrimitive(const tinygltf::Model& model,
                                 const tinygltf::Mesh& mesh,
                                 const tinygltf::Primitive& primitive) {
  if (primitive.material >= 0 &&
      static_cast<std::size_t>(primitive.material) < model.materials.size()) {
    const std::string& name = model.materials[primitive.material].name;
    if (!name.empty()) {
      return name;
    }
  }
  return mesh.name.empty() ? "default" : mesh.name;
}

void readPositions(const tinygltf::Model& model,
                   const tinygltf::Accessor& accessor, double toMeters,
                   std::vector<Vec3>& out) {
  if (accessor.type != TINYGLTF_TYPE_VEC3 ||
      accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT) {
    throw std::runtime_error(
        "glTF POSITION accessor must be float VEC3 (spec)");
  }
  if (accessor.sparse.isSparse) {
    throw std::runtime_error("glTF sparse POSITION accessors are not supported");
  }
  const std::uint8_t* data = accessorBytes(model, accessor);
  const std::size_t stride = elementStride(model, accessor, 3 * sizeof(float));
  for (std::size_t i = 0; i < accessor.count; ++i) {
    const auto* xyz = reinterpret_cast<const float*>(data + i * stride);
    out.push_back({static_cast<double>(xyz[0]) * toMeters,
                   static_cast<double>(xyz[1]) * toMeters,
                   static_cast<double>(xyz[2]) * toMeters});
  }
}

std::uint32_t readIndex(const tinygltf::Model& model,
                        const tinygltf::Accessor& accessor,
                        std::size_t element) {
  const std::uint8_t* data = accessorBytes(model, accessor);
  const std::size_t stride =
      elementStride(model, accessor, componentByteSize(accessor.componentType));
  const std::uint8_t* at = data + element * stride;
  switch (accessor.componentType) {
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
      return *at;
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
      std::uint16_t value = 0;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }
    case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
      std::uint32_t value = 0;
      std::memcpy(&value, at, sizeof(value));
      return value;
    }
    default:
      throw std::runtime_error(
          "glTF index accessor must be unsigned byte/short/int (spec)");
  }
}

}  // namespace

Mesh loadGltf(const std::filesystem::path& path, const std::string& unit,
              double scale) {
  if (!(scale > 0.0)) {
    throw std::runtime_error("mesh scale must be > 0");
  }
  const double toMeters = detail::unitToMeters(unit) * scale;

  tinygltf::Model model;
  tinygltf::TinyGLTF loader;
  std::string error;
  std::string warning;
  // GLB is identified by the "glTF" magic (spec), not by the file extension.
  bool loaded = false;
  {
    std::ifstream probe(path, std::ios::binary);
    char magic[4] = {};
    probe.read(magic, 4);
    const bool isBinary = probe.gcount() == 4 && std::memcmp(magic, "glTF", 4) == 0;
    if (isBinary) {
      loaded = loader.LoadBinaryFromFile(&model, &error, &warning, path.string());
    } else {
      loaded = loader.LoadASCIIFromFile(&model, &error, &warning, path.string());
    }
  }
  if (!loaded) {
    throw std::runtime_error("glTF parse failed (" + path.string() +
                             "): " + error);
  }

  Mesh mesh;
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

  // Mesh primitives are read as authored; node transforms are not applied,
  // so exporters must bake placement into vertex positions for v1 rooms.
  // Shared POSITION accessors contribute vertices once (OBJ-like topology).
  std::map<int, std::uint32_t> baseByPositionAccessor;
  for (const tinygltf::Mesh& gltfMesh : model.meshes) {
    for (const tinygltf::Primitive& primitive : gltfMesh.primitives) {
      if (primitive.mode != TINYGLTF_MODE_TRIANGLES) {
        throw std::runtime_error(
            "glTF primitive mode must be TRIANGLES (mesh '" + gltfMesh.name +
            "')");
      }
      const auto positionIt = primitive.attributes.find("POSITION");
      if (positionIt == primitive.attributes.end()) {
        throw std::runtime_error("glTF primitive missing POSITION (mesh '" +
                                 gltfMesh.name + "')");
      }
      if (static_cast<std::size_t>(positionIt->second) >=
          model.accessors.size()) {
        throw std::runtime_error("glTF POSITION accessor index out of range");
      }
      const tinygltf::Accessor& positionAccessor =
          model.accessors[positionIt->second];

      std::uint32_t base = 0;
      const auto baseIt = baseByPositionAccessor.find(positionIt->second);
      if (baseIt != baseByPositionAccessor.end()) {
        base = baseIt->second;
      } else {
        base = static_cast<std::uint32_t>(mesh.positions.size());
        readPositions(model, positionAccessor, toMeters, mesh.positions);
        baseByPositionAccessor.emplace(positionIt->second, base);
      }
      const std::uint32_t vertexCount =
          static_cast<std::uint32_t>(positionAccessor.count);

      const std::uint32_t slot =
          slotOf(zoneNameForPrimitive(model, gltfMesh, primitive));

      auto pushTriangle = [&](std::uint32_t i0, std::uint32_t i1,
                              std::uint32_t i2) {
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
          throw std::runtime_error("glTF index out of range (mesh '" +
                                   gltfMesh.name + "')");
        }
        Triangle triangle;
        triangle.v0 = base + i0;
        triangle.v1 = base + i1;
        triangle.v2 = base + i2;
        triangle.materialSlot = slot;
        mesh.triangles.push_back(triangle);
      };

      if (primitive.indices >= 0) {
        if (static_cast<std::size_t>(primitive.indices) >=
            model.accessors.size()) {
          throw std::runtime_error("glTF index accessor index out of range");
        }
        const tinygltf::Accessor& indexAccessor =
            model.accessors[primitive.indices];
        if (indexAccessor.type != TINYGLTF_TYPE_SCALAR) {
          throw std::runtime_error("glTF index accessor must be SCALAR");
        }
        if (indexAccessor.count % 3 != 0) {
          throw std::runtime_error(
              "glTF index count is not a multiple of 3 (mesh '" +
              gltfMesh.name + "')");
        }
        for (std::size_t i = 0; i < indexAccessor.count; i += 3) {
          pushTriangle(readIndex(model, indexAccessor, i),
                       readIndex(model, indexAccessor, i + 1),
                       readIndex(model, indexAccessor, i + 2));
        }
      } else {
        if (vertexCount % 3 != 0) {
          throw std::runtime_error(
              "glTF unindexed primitive vertex count is not a multiple of 3");
        }
        for (std::uint32_t i = 0; i < vertexCount; i += 3) {
          pushTriangle(i, i + 1, i + 2);
        }
      }
    }
  }

  return mesh;
}

}  // namespace av
