#include <gtest/gtest.h>

#include <av/geometry/mesh_builder.hpp>
#include <av/io/io.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::filesystem::path tempObjPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("av-gen-mesh-" + name);
}

}  // namespace

TEST(MeshGen, BoxMatchesFixtureTopology) {
  // Unit cube must match tests/fixtures/box.obj: 8 verts, 12 tris, zones.
  const av::Mesh mesh = av::generateBoxMesh({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
  ASSERT_EQ(mesh.materialSlots.size(), 3u);
  EXPECT_EQ(mesh.materialSlots[0], "floor");
  EXPECT_EQ(mesh.materialSlots[1], "ceiling");
  EXPECT_EQ(mesh.materialSlots[2], "walls");

  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, 0.0);
  EXPECT_DOUBLE_EQ(box.min.y, 0.0);
  EXPECT_DOUBLE_EQ(box.min.z, 0.0);
  EXPECT_DOUBLE_EQ(box.max.x, 1.0);
  EXPECT_DOUBLE_EQ(box.max.y, 1.0);
  EXPECT_DOUBLE_EQ(box.max.z, 1.0);
  EXPECT_NEAR(av::surfaceArea(mesh), 6.0, 1e-12);
}

TEST(MeshGen, BoxVertexLayoutMatchesFixture) {
  const av::Mesh mesh = av::generateBoxMesh({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
  const av::Vec3 expected[8] = {
      {0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0}, {1.0, 0.0, 1.0}, {1.0, 1.0, 1.0}, {0.0, 1.0, 1.0},
  };
  ASSERT_EQ(mesh.positions.size(), 8u);
  for (std::size_t i = 0; i < 8u; ++i) {
    EXPECT_DOUBLE_EQ(mesh.positions[i].x, expected[i].x) << "vertex " << i;
    EXPECT_DOUBLE_EQ(mesh.positions[i].y, expected[i].y) << "vertex " << i;
    EXPECT_DOUBLE_EQ(mesh.positions[i].z, expected[i].z) << "vertex " << i;
  }

  // Triangle indices match box.obj f lines (1-based in OBJ, 0-based here).
  const std::uint32_t expectedTris[12][3] = {
      {0, 1, 2}, {0, 2, 3},  // floor
      {4, 7, 6}, {4, 6, 5},  // ceiling
      {0, 4, 7}, {0, 7, 3},  // x = 0
      {1, 2, 6}, {1, 6, 5},  // x = 1
      {0, 1, 5}, {0, 5, 4},  // y = 0
      {3, 7, 6}, {3, 6, 2},  // y = 1
  };
  ASSERT_EQ(mesh.triangles.size(), 12u);
  for (std::size_t i = 0; i < 12u; ++i) {
    EXPECT_EQ(mesh.triangles[i].v0, expectedTris[i][0]) << "triangle " << i;
    EXPECT_EQ(mesh.triangles[i].v1, expectedTris[i][1]) << "triangle " << i;
    EXPECT_EQ(mesh.triangles[i].v2, expectedTris[i][2]) << "triangle " << i;
  }
}

TEST(MeshGen, BoxOriginAndExtents) {
  const av::Mesh mesh = av::generateBoxMesh({-1.0, 2.0, 0.5}, 6.0, 2.8, 4.5);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);

  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, -1.0);
  EXPECT_DOUBLE_EQ(box.min.y, 2.0);
  EXPECT_DOUBLE_EQ(box.min.z, 0.5);
  EXPECT_DOUBLE_EQ(box.max.x, 5.0);
  EXPECT_DOUBLE_EQ(box.max.y, 6.5);
  EXPECT_DOUBLE_EQ(box.max.z, 3.3);

  // 2*(w*d + w*h + d*h) = 2*(27 + 16.8 + 12.6) = 112.8
  EXPECT_NEAR(av::surfaceArea(mesh), 2.0 * (6.0 * 4.5 + 6.0 * 2.8 + 4.5 * 2.8),
              1e-12);
}

TEST(MeshGen, BoxWindingIsIntoRoomAir) {
  const av::Mesh mesh = av::generateBoxMesh({0.0, 0.0, 0.0}, 1.0, 1.0, 1.0);
  // Floor (first triangle) sits at z = 0; its face normal must point +z
  // (into the room air), same as tests/fixtures/box.obj.
  const av::Vec3 floorNormal = av::faceNormal(mesh, mesh.triangles[0]);
  EXPECT_NEAR(floorNormal.z, 1.0, 1e-12);
  // Ceiling triangle at z = 1 points -z.
  const av::Vec3 ceilingNormal = av::faceNormal(mesh, mesh.triangles[2]);
  EXPECT_NEAR(ceilingNormal.z, -1.0, 1e-12);
}

TEST(MeshGen, BoxRejectsNonPositiveExtents) {
  EXPECT_THROW(av::generateBoxMesh({}, 0.0, 1.0, 1.0), std::invalid_argument);
  EXPECT_THROW(av::generateBoxMesh({}, 1.0, -1.0, 1.0), std::invalid_argument);
  EXPECT_THROW(av::generateBoxMesh({}, 1.0, 1.0, 0.0), std::invalid_argument);
}

TEST(MeshGen, CorridorCountsAndBounds) {
  const av::Mesh mesh = av::generateCorridorMesh({0.0, 0.0, 0.0}, 2.0, 2.5, 10.0);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);

  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.max.x, 2.0);
  EXPECT_DOUBLE_EQ(box.max.y, 10.0);
  EXPECT_DOUBLE_EQ(box.max.z, 2.5);
  EXPECT_NEAR(av::surfaceArea(mesh),
              2.0 * (2.0 * 10.0 + 2.0 * 2.5 + 10.0 * 2.5), 1e-12);
}

TEST(MeshGen, LRoomCountsAndBounds) {
  // L: full 6x4 with notch (3..6)x(3..4) removed; height 2.8.
  const av::Mesh mesh =
      av::generateLRoomMesh({0.0, 0.0, 0.0}, 6.0, 4.0, 3.0, 3.0, 2.8);
  // 6 floor-ring verts + 6 top-ring verts.
  EXPECT_EQ(mesh.positions.size(), 12u);
  // floor 4 + ceiling 4 + walls 6*2 = 20.
  EXPECT_EQ(mesh.triangles.size(), 20u);
  ASSERT_EQ(mesh.materialSlots.size(), 3u);

  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, 0.0);
  EXPECT_DOUBLE_EQ(box.min.y, 0.0);
  EXPECT_DOUBLE_EQ(box.min.z, 0.0);
  EXPECT_DOUBLE_EQ(box.max.x, 6.0);
  EXPECT_DOUBLE_EQ(box.max.y, 4.0);
  EXPECT_DOUBLE_EQ(box.max.z, 2.8);

  // Floor area = 6*3 + 3*1 = 21 m^2; wall perimeter = 6+3+1+1+3+6 = 20 m.
  const double floorArea = 21.0;
  const double perimeter = 20.0;
  EXPECT_NEAR(av::surfaceArea(mesh),
              2.0 * floorArea + perimeter * 2.8, 1e-12);
}

TEST(MeshGen, LRoomRejectsDegenerateNotch) {
  EXPECT_THROW(av::generateLRoomMesh({}, 6.0, 4.0, 6.0, 3.0, 2.8),
               std::invalid_argument);
  EXPECT_THROW(av::generateLRoomMesh({}, 6.0, 4.0, 3.0, 4.0, 2.8),
               std::invalid_argument);
  EXPECT_THROW(av::generateLRoomMesh({}, 6.0, 4.0, 0.0, 3.0, 2.8),
               std::invalid_argument);
}

TEST(MeshGen, SaveObjRoundTrip) {
  const std::filesystem::path path = tempObjPath("box.obj");
  const av::Mesh mesh = av::generateBoxMesh({0.5, -1.0, 2.0}, 6.0, 2.8, 4.5);
  av::saveObj(path, mesh);
  const av::Mesh loaded = av::loadObj(path, "m", 1.0);
  std::filesystem::remove(path);

  EXPECT_EQ(loaded.positions.size(), 8u);
  EXPECT_EQ(loaded.triangles.size(), 12u);
  EXPECT_EQ(loaded.materialSlots.size(), 3u);
  const av::Aabb box = av::boundsOf(loaded);
  // tinyobjloader stores vertex positions as 32-bit float; allow that error.
  constexpr double kEps = 1e-6;
  EXPECT_NEAR(box.min.x, 0.5, kEps);
  EXPECT_NEAR(box.min.y, -1.0, kEps);
  EXPECT_NEAR(box.min.z, 2.0, kEps);
  EXPECT_NEAR(box.max.x, 6.5, kEps);
  EXPECT_NEAR(box.max.y, 3.5, kEps);
  EXPECT_NEAR(box.max.z, 4.8, kEps);
  EXPECT_NEAR(av::surfaceArea(loaded),
              2.0 * (6.0 * 4.5 + 6.0 * 2.8 + 4.5 * 2.8), 1e-4);
}

TEST(MeshGen, SaveObjUnitCubeReloadMatchesFixtureCounts) {
  const std::filesystem::path path = tempObjPath("unit.obj");
  av::saveObj(path, av::generateBoxMesh({}, 1.0, 1.0, 1.0));
  const av::Mesh loaded = av::loadObj(path, "m", 1.0);
  std::filesystem::remove(path);
  EXPECT_EQ(loaded.positions.size(), 8u);
  EXPECT_EQ(loaded.triangles.size(), 12u);
  EXPECT_NEAR(av::surfaceArea(loaded), 6.0, 1e-9);
}

TEST(MeshGen, SaveObjWritesGroupAndUsemtlPerZone) {
  const std::filesystem::path path = tempObjPath("zones.obj");
  av::saveObj(path, av::generateBoxMesh({}, 1.0, 1.0, 1.0));
  std::ifstream in(path);
  std::filesystem::remove(path);
  ASSERT_TRUE(in) << "saveObj output missing";

  std::vector<std::string> lines;
  for (std::string line; std::getline(in, line);) {
    lines.push_back(line);
  }
  for (const char* zone : {"floor", "ceiling", "walls"}) {
    const std::string group = std::string("g ") + zone;
    const std::string usemtl = std::string("usemtl ") + zone;
    EXPECT_NE(std::find(lines.begin(), lines.end(), group), lines.end())
        << group;
    EXPECT_NE(std::find(lines.begin(), lines.end(), usemtl), lines.end())
        << usemtl;
  }
}
