#pragma once

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

struct Aabb {
  Vec3 min{};
  Vec3 max{};
};

}  // namespace av
