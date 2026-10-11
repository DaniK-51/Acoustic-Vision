#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <av/geometry/vec3.hpp>

namespace av {

// Indexed triangle mesh for mesh rooms (ARCHITECTURE.md §2 geometry).
//
// Normal convention: vertex/face normals point *into the room / toward the
// air* (ARCHITECTURE.md). For a closed room shell this is the inward-facing
// normal of the surface; when exporting from DCC tools, flip normals if the
// tool produces outward-facing (away from interior air) normals.
struct Triangle {
  // Indices into Mesh::positions, CCW when viewed from the air side.
  std::uint32_t v0 = 0;
  std::uint32_t v1 = 0;
  std::uint32_t v2 = 0;
  // Index into Mesh::materialSlots (per-face / per-zone material).
  std::uint32_t materialSlot = 0;
};

struct Mesh {
  std::vector<Vec3> positions;
  // Optional per-vertex normals; empty or positions.size(). When empty,
  // faceNormal() supplies geometric normals.
  std::vector<Vec3> normals;
  std::vector<Triangle> triangles;
  // Material zone names (OBJ group / material names), one entry per zone.
  // Triangle::materialSlot indexes this. applyMaterialOverrides() rewrites
  // entries to scenario material ids where the scenario maps the name; the
  // remaining entries keep their names and act as default material ids.
  std::vector<std::string> materialSlots;
};

// Geometric face normal from vertex order (right-hand rule, into-room air
// side). Unit length; zero vector for degenerate triangles.
[[nodiscard]] Vec3 faceNormal(const Mesh& mesh, const Triangle& triangle);

// AABB of all positions (empty mesh yields a zero box at origin).
[[nodiscard]] Aabb boundsOf(const Mesh& mesh);

// Total triangle area, m^2 (SI after unit conversion at import).
[[nodiscard]] double surfaceArea(const Mesh& mesh);

// Per-vertex normal of a triangle corner, or face normal when the mesh has
// no normal array.
[[nodiscard]] Vec3 vertexNormal(const Mesh& mesh, const Triangle& triangle,
                                int corner);

}  // namespace av
