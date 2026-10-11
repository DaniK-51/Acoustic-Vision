#include <gtest/gtest.h>

#include <av/geometry/bvh.hpp>
#include <av/geometry/mesh.hpp>

#include <cmath>

namespace {

// Unit cube [0,1]^3 as 12 triangles, positions CCW from outside.
av::Mesh makeUnitCube() {
  av::Mesh mesh;
  mesh.positions = {
      {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0},
  };
  const std::uint32_t quads[6][4] = {
      {0, 1, 2, 3},  // z = 0
      {4, 7, 6, 5},  // z = 1
      {0, 4, 5, 1},  // y = 0
      {3, 2, 6, 7},  // y = 1
      {0, 3, 7, 4},  // x = 0
      {1, 5, 6, 2},  // x = 1
  };
  for (const auto& q : quads) {
    mesh.triangles.push_back({q[0], q[1], q[2], 0});
    mesh.triangles.push_back({q[0], q[2], q[3], 0});
  }
  return mesh;
}

}  // namespace

TEST(Mesh, BoxTriangleCountAndBounds) {
  const av::Mesh mesh = makeUnitCube();
  EXPECT_EQ(mesh.positions.size(), 8u);
  ASSERT_EQ(mesh.triangles.size(), 12u);

  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, 0.0);
  EXPECT_DOUBLE_EQ(box.min.y, 0.0);
  EXPECT_DOUBLE_EQ(box.min.z, 0.0);
  EXPECT_DOUBLE_EQ(box.max.x, 1.0);
  EXPECT_DOUBLE_EQ(box.max.y, 1.0);
  EXPECT_DOUBLE_EQ(box.max.z, 1.0);

  EXPECT_NEAR(av::surfaceArea(mesh), 6.0, 1e-12);
}

TEST(Mesh, FaceNormalPointsAlongAxis) {
  const av::Mesh mesh = makeUnitCube();
  // First face is on z = 0; its geometric normal is +/-z of unit length.
  const av::Vec3 n = av::faceNormal(mesh, mesh.triangles[0]);
  EXPECT_NEAR(av::length(n), 1.0, 1e-12);
  EXPECT_NEAR(n.x, 0.0, 1e-12);
  EXPECT_NEAR(n.y, 0.0, 1e-12);
  EXPECT_NEAR(std::abs(n.z), 1.0, 1e-12);
}

TEST(Mesh, EmptyMeshBoundsAreZero) {
  const av::Mesh mesh;
  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, 0.0);
  EXPECT_DOUBLE_EQ(box.max.z, 0.0);
  EXPECT_DOUBLE_EQ(av::surfaceArea(mesh), 0.0);
}

TEST(Bvh, RayHitsKnownFace) {
  const av::Mesh mesh = makeUnitCube();
  const av::Bvh bvh(mesh);
  ASSERT_FALSE(bvh.empty());

  // Ray along +x aimed at the x = 0 face center.
  const av::Ray ray{{ -1.0, 0.5, 0.5}, {1.0, 0.0, 0.0}};
  av::RayHit hit;
  ASSERT_TRUE(bvh.intersect(ray, 0.0, 100.0, hit));
  EXPECT_NEAR(hit.t, 1.0, 1e-9);
  EXPECT_NEAR(hit.position.x, 0.0, 1e-9);
  EXPECT_NEAR(hit.position.y, 0.5, 1e-9);
  EXPECT_NEAR(hit.position.z, 0.5, 1e-9);
  EXPECT_NEAR(std::abs(hit.normal.x), 1.0, 1e-9);
}

TEST(Bvh, RayMissesOutside) {
  const av::Mesh mesh = makeUnitCube();
  const av::Bvh bvh(mesh);

  const av::Ray ray{{2.0, 2.0, 2.0}, {1.0, 0.0, 0.0}};
  av::RayHit hit;
  EXPECT_FALSE(bvh.intersect(ray, 0.0, 100.0, hit));
}

TEST(Bvh, RayRespectsTInterval) {
  const av::Mesh mesh = makeUnitCube();
  const av::Bvh bvh(mesh);

  const av::Ray ray{{-1.0, 0.5, 0.5}, {1.0, 0.0, 0.0}};
  av::RayHit hit;
  // Hit is at t = 1; a window that ends earlier must miss.
  EXPECT_FALSE(bvh.intersect(ray, 0.0, 0.5, hit));
  EXPECT_TRUE(bvh.intersect(ray, 0.5, 1.5, hit));
  EXPECT_NEAR(hit.t, 1.0, 1e-9);
}

TEST(Bvh, ClosestHitWins) {
  const av::Mesh mesh = makeUnitCube();
  const av::Bvh bvh(mesh);

  // From inside the cube toward +x: nearest face is x = 1 at t = 0.5.
  const av::Ray ray{{0.5, 0.5, 0.5}, {1.0, 0.0, 0.0}};
  av::RayHit hit;
  ASSERT_TRUE(bvh.intersect(ray, 0.0, 100.0, hit));
  EXPECT_NEAR(hit.t, 0.5, 1e-9);
  EXPECT_NEAR(hit.position.x, 1.0, 1e-9);
}

TEST(Bvh, EmptyBvhMisses) {
  const av::Bvh bvh;
  EXPECT_TRUE(bvh.empty());
  av::RayHit hit;
  EXPECT_FALSE(bvh.intersect({{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}}, 0.0, 1.0,
                             hit));
}
