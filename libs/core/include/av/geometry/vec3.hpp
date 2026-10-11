#pragma once

#include <cmath>

namespace av {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] constexpr Vec3 operator+(const Vec3& a, const Vec3& b) {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr Vec3 operator-(const Vec3& a, const Vec3& b) {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] constexpr Vec3 operator*(const Vec3& a, double s) {
  return {a.x * s, a.y * s, a.z * s};
}

[[nodiscard]] constexpr Vec3 operator*(double s, const Vec3& a) {
  return a * s;
}

[[nodiscard]] constexpr Vec3 operator/(const Vec3& a, double s) {
  return {a.x / s, a.y / s, a.z / s};
}

[[nodiscard]] constexpr Vec3 operator-(const Vec3& a) {
  return {-a.x, -a.y, -a.z};
}

[[nodiscard]] constexpr bool operator==(const Vec3& a, const Vec3& b) {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

[[nodiscard]] constexpr double dot(const Vec3& a, const Vec3& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] constexpr double lengthSquared(const Vec3& a) { return dot(a, a); }

[[nodiscard]] inline double length(const Vec3& a) {
  return std::sqrt(lengthSquared(a));
}

[[nodiscard]] inline Vec3 normalized(const Vec3& a) {
  const double len = length(a);
  return len > 0.0 ? a / len : Vec3{};
}

struct Aabb {
  Vec3 min{};
  Vec3 max{};
};

// Degenerate point box at p is a valid seed before expanding over a set.
[[nodiscard]] constexpr Aabb aabbFromPoint(const Vec3& p) { return {p, p}; }

[[nodiscard]] constexpr Aabb aabbUnion(const Aabb& a, const Aabb& b) {
  return {{a.min.x < b.min.x ? a.min.x : b.min.x,
           a.min.y < b.min.y ? a.min.y : b.min.y,
           a.min.z < b.min.z ? a.min.z : b.min.z},
          {a.max.x > b.max.x ? a.max.x : b.max.x,
           a.max.y > b.max.y ? a.max.y : b.max.y,
           a.max.z > b.max.z ? a.max.z : b.max.z}};
}

[[nodiscard]] constexpr Aabb aabbExpand(const Aabb& box, const Vec3& p) {
  return aabbUnion(box, aabbFromPoint(p));
}

[[nodiscard]] constexpr Vec3 aabbCentroid(const Aabb& box) {
  return (box.min + box.max) * 0.5;
}

// Distance-style parametric ray: point(t) = origin + t * direction.
// t is a distance when direction is normalized.
struct Ray {
  Vec3 origin{};
  Vec3 direction{};
};

}  // namespace av
