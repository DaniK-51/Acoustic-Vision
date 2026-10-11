#include <av/geometry/bvh.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace av {
namespace {

constexpr std::uint32_t kLeafSize = 4;

// Möller–Trumbore, two-sided (no backface cull): open meshes from DCC tools
// are common, and a one-sided test would silently drop inverted faces before
// import validation exists.
bool intersectTriangle(const Ray& ray, double tMin, double tMax, const Vec3& p0,
                       const Vec3& p1, const Vec3& p2, double& tOut) {
  constexpr double kEps = 1e-12;
  const Vec3 edge1 = p1 - p0;
  const Vec3 edge2 = p2 - p0;
  const Vec3 pvec = cross(ray.direction, edge2);
  const double det = dot(edge1, pvec);
  if (det > -kEps && det < kEps) {
    return false;
  }
  const double invDet = 1.0 / det;
  const Vec3 tvec = ray.origin - p0;
  const double u = dot(tvec, pvec) * invDet;
  if (u < 0.0 || u > 1.0) {
    return false;
  }
  const Vec3 qvec = cross(tvec, edge1);
  const double v = dot(ray.direction, qvec) * invDet;
  if (v < 0.0 || u + v > 1.0) {
    return false;
  }
  const double t = dot(edge2, qvec) * invDet;
  if (t < tMin || t > tMax) {
    return false;
  }
  tOut = t;
  return true;
}

// Slab test; zero-direction axes are compared directly to avoid 0 * inf = NaN.
bool intersectAabb(const Ray& ray, const Aabb& box, double tMin, double tMax) {
  const std::array<double, 3> origin{ray.origin.x, ray.origin.y, ray.origin.z};
  const std::array<double, 3> dir{ray.direction.x, ray.direction.y,
                                  ray.direction.z};
  const std::array<double, 3> lo{box.min.x, box.min.y, box.min.z};
  const std::array<double, 3> hi{box.max.x, box.max.y, box.max.z};

  double tNear = tMin;
  double tFar = tMax;
  for (int axis = 0; axis < 3; ++axis) {
    if (std::abs(dir[axis]) < 1e-15) {
      if (origin[axis] < lo[axis] || origin[axis] > hi[axis]) {
        return false;
      }
      continue;
    }
    double t0 = (lo[axis] - origin[axis]) / dir[axis];
    double t1 = (hi[axis] - origin[axis]) / dir[axis];
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    tNear = t0 > tNear ? t0 : tNear;
    tFar = t1 < tFar ? t1 : tFar;
    if (tNear > tFar) {
      return false;
    }
  }
  return true;
}

Vec3 triangleCentroid(const Mesh& mesh, std::uint32_t triangleIndex) {
  const Triangle& tri = mesh.triangles[triangleIndex];
  return (mesh.positions[tri.v0] + mesh.positions[tri.v1] +
          mesh.positions[tri.v2]) /
         3.0;
}

Aabb triangleBounds(const Mesh& mesh, std::uint32_t triangleIndex) {
  const Triangle& tri = mesh.triangles[triangleIndex];
  Aabb box = aabbFromPoint(mesh.positions[tri.v0]);
  box = aabbExpand(box, mesh.positions[tri.v1]);
  box = aabbExpand(box, mesh.positions[tri.v2]);
  return box;
}

double centroidAxis(const Mesh& mesh, std::uint32_t triangleIndex, int axis) {
  const Vec3 c = triangleCentroid(mesh, triangleIndex);
  return axis == 0 ? c.x : (axis == 1 ? c.y : c.z);
}

}  // namespace

void Bvh::build(const Mesh& mesh) {
  m_mesh = &mesh;
  m_indices.resize(mesh.triangles.size());
  for (std::uint32_t i = 0; i < m_indices.size(); ++i) {
    m_indices[i] = i;
  }
  if (m_indices.empty()) {
    m_nodes.clear();
    return;
  }
  m_nodes.clear();
  m_nodes.reserve(m_indices.size() * 2);
  buildNode(0, static_cast<std::uint32_t>(m_indices.size()));
}

std::uint32_t Bvh::buildNode(std::uint32_t first, std::uint32_t count) {
  const std::uint32_t nodeIndex = static_cast<std::uint32_t>(m_nodes.size());
  m_nodes.push_back(Node{});

  Aabb box = triangleBounds(*m_mesh, m_indices[first]);
  Aabb centroidBox = aabbFromPoint(triangleCentroid(*m_mesh, m_indices[first]));
  for (std::uint32_t i = first + 1; i < first + count; ++i) {
    box = aabbUnion(box, triangleBounds(*m_mesh, m_indices[i]));
    centroidBox =
        aabbExpand(centroidBox, triangleCentroid(*m_mesh, m_indices[i]));
  }

  if (count <= kLeafSize) {
    m_nodes[nodeIndex].bounds = box;
    m_nodes[nodeIndex].first = first;
    m_nodes[nodeIndex].triangleCount = count;
    return nodeIndex;
  }

  const Vec3 extent = centroidBox.max - centroidBox.min;
  int axis = 0;
  if (extent.y > extent.x) {
    axis = 1;
  }
  if (extent.z > (axis == 0 ? extent.x : extent.y)) {
    axis = 2;
  }

  std::nth_element(
      m_indices.begin() + static_cast<std::ptrdiff_t>(first),
      m_indices.begin() + static_cast<std::ptrdiff_t>(first + count / 2),
      m_indices.begin() + static_cast<std::ptrdiff_t>(first + count),
      [&](std::uint32_t a, std::uint32_t b) {
        return centroidAxis(*m_mesh, a, axis) < centroidAxis(*m_mesh, b, axis);
      });

  const std::uint32_t leftCount = count / 2;
  const std::uint32_t leftChild = buildNode(first, leftCount);
  const std::uint32_t rightChild =
      buildNode(first + leftCount, count - leftCount);

  m_nodes[nodeIndex].bounds = box;
  m_nodes[nodeIndex].leftChild = leftChild;
  m_nodes[nodeIndex].rightChild = rightChild;
  return nodeIndex;
}

bool Bvh::intersect(const Ray& ray, double tMin, double tMax,
                    RayHit& hit) const {
  if (m_nodes.empty() || m_mesh == nullptr) {
    return false;
  }

  bool found = false;
  double closest = tMax;
  RayHit best{};

  // Explicit stack: tree depth is O(log n) in practice, but a vector stack
  // avoids any recursion limit on hostile inputs.
  std::vector<std::uint32_t> stack;
  stack.reserve(64);
  stack.push_back(0);

  while (!stack.empty()) {
    const Node node = m_nodes[stack.back()];
    stack.pop_back();
    if (!intersectAabb(ray, node.bounds, tMin, closest)) {
      continue;
    }
    if (node.triangleCount > 0) {
      for (std::uint32_t i = 0; i < node.triangleCount; ++i) {
        const std::uint32_t triIndex = m_indices[node.first + i];
        const Triangle& tri = m_mesh->triangles[triIndex];
        double t = 0.0;
        if (!intersectTriangle(ray, tMin, closest,
                               m_mesh->positions[tri.v0],
                               m_mesh->positions[tri.v1],
                               m_mesh->positions[tri.v2], t)) {
          continue;
        }
        closest = t;
        found = true;
        best.t = t;
        best.triangleIndex = triIndex;
        best.position = ray.origin + ray.direction * t;
        best.normal = faceNormal(*m_mesh, tri);
      }
      continue;
    }
    stack.push_back(node.leftChild);
    stack.push_back(node.rightChild);
  }

  if (found) {
    hit = best;
  }
  return found;
}

}  // namespace av
