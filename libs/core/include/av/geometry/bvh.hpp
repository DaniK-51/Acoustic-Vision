#pragma once

#include <cstdint>
#include <vector>

#include <av/geometry/mesh.hpp>
#include <av/geometry/vec3.hpp>

namespace av {

struct RayHit {
  // Ray parameter of the hit: position = ray.origin + t * ray.direction.
  double t = 0.0;
  // Index into Mesh::triangles.
  std::uint32_t triangleIndex = 0;
  Vec3 position{};
  // Geometric face normal, into-room convention (see mesh.hpp). Two-sided
  // tests: the normal is not flipped toward the ray.
  Vec3 normal{};
};

// AABB tree over mesh triangles (median split on centroids). Built up front;
// used by ray tracing and mesh visibility queries. IMS-on-rect does not need
// it. The mesh must outlive the Bvh — the tree stores a pointer to it.
class Bvh {
 public:
  Bvh() = default;
  explicit Bvh(const Mesh& mesh) { build(mesh); }

  void build(const Mesh& mesh);

  [[nodiscard]] bool empty() const { return m_nodes.empty(); }

  // Closest hit with t in [tMin, tMax]; false leaves `hit` unchanged.
  // Direction need not be normalized, but t then measures in |direction| units.
  [[nodiscard]] bool intersect(const Ray& ray, double tMin, double tMax,
                               RayHit& hit) const;

 private:
  struct Node {
    Aabb bounds{};
    // Leaf when triangleCount > 0: indices [first, first + count) into
    // m_indices. Inner otherwise.
    std::uint32_t first = 0;
    std::uint32_t triangleCount = 0;
    std::uint32_t leftChild = 0;
    std::uint32_t rightChild = 0;
  };

  std::uint32_t buildNode(std::uint32_t first, std::uint32_t count);

  const Mesh* m_mesh = nullptr;
  std::vector<Node> m_nodes;
  // Triangle indices, permuted at build time.
  std::vector<std::uint32_t> m_indices;
};

}  // namespace av
