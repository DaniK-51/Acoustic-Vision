#include <av/geometry/mesh.hpp>

#include <cmath>

namespace av {

Vec3 faceNormal(const Mesh& mesh, const Triangle& triangle) {
  const Vec3& p0 = mesh.positions[triangle.v0];
  const Vec3& p1 = mesh.positions[triangle.v1];
  const Vec3& p2 = mesh.positions[triangle.v2];
  return normalized(cross(p1 - p0, p2 - p0));
}

Aabb boundsOf(const Mesh& mesh) {
  if (mesh.positions.empty()) {
    return {};
  }
  Aabb box = aabbFromPoint(mesh.positions.front());
  for (const Vec3& p : mesh.positions) {
    box = aabbExpand(box, p);
  }
  return box;
}

double surfaceArea(const Mesh& mesh) {
  double area = 0.0;
  for (const Triangle& triangle : mesh.triangles) {
    const Vec3& p0 = mesh.positions[triangle.v0];
    const Vec3& p1 = mesh.positions[triangle.v1];
    const Vec3& p2 = mesh.positions[triangle.v2];
    area += 0.5 * length(cross(p1 - p0, p2 - p0));
  }
  return area;
}

Vec3 vertexNormal(const Mesh& mesh, const Triangle& triangle, int corner) {
  if (!mesh.normals.empty()) {
    switch (corner) {
      case 0:
        return mesh.normals[triangle.v0];
      case 1:
        return mesh.normals[triangle.v1];
      case 2:
        return mesh.normals[triangle.v2];
      default:
        break;
    }
  }
  return faceNormal(mesh, triangle);
}

}  // namespace av
