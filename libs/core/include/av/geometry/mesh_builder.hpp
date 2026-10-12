#pragma once

#include <cstdint>
#include <map>
#include <string>

#include <av/geometry/mesh.hpp>
#include <av/geometry/vec3.hpp>

namespace av {

// Accumulates faces into a Mesh with named material zones (OBJ groups).
// Vertices are added explicitly so shapes can share corner indices
// (a box is 8 verts / 12 tris, not 36 unshared verts).
class MeshBuilder {
 public:
  [[nodiscard]] std::uint32_t addVertex(const Vec3& position);

  // Zone names become Mesh::materialSlots; the first occurrence order is kept.
  void addTriangle(std::uint32_t v0, std::uint32_t v1, std::uint32_t v2,
                   const std::string& zone);
  // Quad (a,b,c,d) -> triangles (a,b,c) + (a,c,d).
  void addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c,
               std::uint32_t d, const std::string& zone);

  [[nodiscard]] Mesh build() const;

 private:
  [[nodiscard]] std::uint32_t slotOf(const std::string& zone);

  Mesh mesh_;
  std::map<std::string, std::uint32_t> slotByName_;
};

// Closed rectangular shell. `origin` is the min corner; extents are w along
// +x, h along +z (floor -> ceiling), d along +y. Vertex layout and winding
// match tests/fixtures/box.obj (scaled/translated): 8 positions, 12
// triangles, zones floor / ceiling / walls. Throws std::invalid_argument on
// non-positive extents.
[[nodiscard]] Mesh generateBoxMesh(const Vec3& origin, double w, double h,
                                   double d);

// Closed corridor shell: width w along +x, length along +y, height h along
// +z. Topology identical to generateBoxMesh(origin, w, h, length).
[[nodiscard]] Mesh generateCorridorMesh(const Vec3& origin, double w,
                                        double h, double length);

// L-shaped floor polygon extruded upward by h (units meters). Floor polygon
// in the xy-plane, CCW from +z:
//   (0,0) - (w,0) - (w,d2) - (w2,d2) - (w2,d) - (0,d)
// so the notch (w2..w) x (d2..d) is empty. Requires 0 < w2 < w and
// 0 < d2 < d. Zones floor / ceiling / walls like generateBoxMesh.
[[nodiscard]] Mesh generateLRoomMesh(const Vec3& origin, double w, double d,
                                     double w2, double d2, double h);

}  // namespace av
