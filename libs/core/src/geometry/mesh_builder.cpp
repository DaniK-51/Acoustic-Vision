#include <av/geometry/mesh_builder.hpp>

#include <stdexcept>
#include <utility>

namespace av {

std::uint32_t MeshBuilder::addVertex(const Vec3& position) {
  mesh_.positions.push_back(position);
  return static_cast<std::uint32_t>(mesh_.positions.size() - 1);
}

std::uint32_t MeshBuilder::slotOf(const std::string& zone) {
  const auto it = slotByName_.find(zone);
  if (it != slotByName_.end()) {
    return it->second;
  }
  const auto index = static_cast<std::uint32_t>(mesh_.materialSlots.size());
  mesh_.materialSlots.push_back(zone);
  slotByName_.emplace(zone, index);
  return index;
}

void MeshBuilder::addTriangle(std::uint32_t v0, std::uint32_t v1,
                              std::uint32_t v2, const std::string& zone) {
  mesh_.triangles.push_back(Triangle{v0, v1, v2, slotOf(zone)});
}

void MeshBuilder::addQuad(std::uint32_t a, std::uint32_t b, std::uint32_t c,
                          std::uint32_t d, const std::string& zone) {
  addTriangle(a, b, c, zone);
  addTriangle(a, c, d, zone);
}

Mesh MeshBuilder::build() const { return mesh_; }

namespace {

void requirePositive(double value, const char* what) {
  if (!(value > 0.0)) {
    throw std::invalid_argument(std::string(what) + " must be positive");
  }
}

}  // namespace

Mesh generateBoxMesh(const Vec3& origin, double w, double h, double d) {
  requirePositive(w, "w");
  requirePositive(h, "h");
  requirePositive(d, "d");

  MeshBuilder builder;
  // Vertex order matches tests/fixtures/box.obj (v1..v8).
  const std::uint32_t v0 = builder.addVertex({origin.x, origin.y, origin.z});
  const std::uint32_t v1 =
      builder.addVertex({origin.x + w, origin.y, origin.z});
  const std::uint32_t v2 =
      builder.addVertex({origin.x + w, origin.y + d, origin.z});
  const std::uint32_t v3 =
      builder.addVertex({origin.x, origin.y + d, origin.z});
  const std::uint32_t v4 =
      builder.addVertex({origin.x, origin.y, origin.z + h});
  const std::uint32_t v5 =
      builder.addVertex({origin.x + w, origin.y, origin.z + h});
  const std::uint32_t v6 =
      builder.addVertex({origin.x + w, origin.y + d, origin.z + h});
  const std::uint32_t v7 =
      builder.addVertex({origin.x, origin.y + d, origin.z + h});

  // Winding copied from box.obj so a unit cube round-trips face-for-face.
  builder.addTriangle(v0, v1, v2, "floor");
  builder.addTriangle(v0, v2, v3, "floor");
  builder.addTriangle(v4, v7, v6, "ceiling");
  builder.addTriangle(v4, v6, v5, "ceiling");
  builder.addTriangle(v0, v4, v7, "walls");
  builder.addTriangle(v0, v7, v3, "walls");
  builder.addTriangle(v1, v2, v6, "walls");
  builder.addTriangle(v1, v6, v5, "walls");
  builder.addTriangle(v0, v1, v5, "walls");
  builder.addTriangle(v0, v5, v4, "walls");
  builder.addTriangle(v3, v7, v6, "walls");
  builder.addTriangle(v3, v6, v2, "walls");
  return builder.build();
}

Mesh generateCorridorMesh(const Vec3& origin, double w, double h,
                          double length) {
  return generateBoxMesh(origin, w, h, length);
}

Mesh generateLRoomMesh(const Vec3& origin, double w, double d, double w2,
                       double d2, double h) {
  requirePositive(w, "w");
  requirePositive(d, "d");
  requirePositive(h, "h");
  if (!(w2 > 0.0) || !(w2 < w)) {
    throw std::invalid_argument("w2 must satisfy 0 < w2 < w");
  }
  if (!(d2 > 0.0) || !(d2 < d)) {
    throw std::invalid_argument("d2 must satisfy 0 < d2 < d");
  }

  MeshBuilder builder;
  // Floor polygon CCW from +z; top copy is the same ring raised by h.
  const Vec3 floorCorners[6] = {
      {origin.x, origin.y, origin.z},
      {origin.x + w, origin.y, origin.z},
      {origin.x + w, origin.y + d2, origin.z},
      {origin.x + w2, origin.y + d2, origin.z},
      {origin.x + w2, origin.y + d, origin.z},
      {origin.x, origin.y + d, origin.z},
  };
  std::uint32_t bottom[6];
  std::uint32_t top[6];
  for (int i = 0; i < 6; ++i) {
    bottom[i] = builder.addVertex(floorCorners[i]);
    top[i] = builder.addVertex({floorCorners[i].x, floorCorners[i].y,
                                floorCorners[i].z + h});
  }

  // Floor fan from bottom[0]: valid for this L polygon (see mesh_builder.hpp).
  for (int i = 1; i < 5; ++i) {
    builder.addTriangle(bottom[0], bottom[i], bottom[i + 1], "floor");
  }
  // Ceiling: reverse of the floor fan -> face normal -z (into the room).
  for (int i = 1; i < 5; ++i) {
    builder.addTriangle(top[0], top[i + 1], top[i], "ceiling");
  }
  // Walls: one quad per floor-polygon edge, order (b0,b1,t1,t0). For a CCW
  // floor ring this is the box.obj exterior wall winding (see tests).
  for (int i = 0; i < 6; ++i) {
    const int j = (i + 1) % 6;
    builder.addQuad(bottom[i], bottom[j], top[j], top[i], "walls");
  }
  return builder.build();
}

}  // namespace av
