#include <gtest/gtest.h>

#include <av/io/io.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path fixture(const std::string& name) {
  return std::filesystem::path(AV_FIXTURES_DIR) / name;
}

}  // namespace

TEST(ObjImport, BoxVertexAndFaceCounts) {
  const av::Mesh mesh = av::loadObj(fixture("box.obj"), "m", 1.0);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
}

TEST(ObjImport, BoxBoundsInMeters) {
  const av::Mesh mesh = av::loadObj(fixture("box.obj"), "m", 1.0);
  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, 0.0);
  EXPECT_DOUBLE_EQ(box.max.x, 1.0);
  EXPECT_DOUBLE_EQ(box.max.y, 1.0);
  EXPECT_DOUBLE_EQ(box.max.z, 1.0);
  EXPECT_NEAR(av::surfaceArea(mesh), 6.0, 1e-9);
}

TEST(ObjImport, MillimetersConvertToMeters) {
  const av::Mesh mesh = av::loadObj(fixture("box.obj"), "mm", 1.0);
  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_NEAR(box.max.x, 0.001, 1e-12);
  EXPECT_NEAR(box.max.y, 0.001, 1e-12);
  EXPECT_NEAR(box.max.z, 0.001, 1e-12);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
}

TEST(ObjImport, ScaleMultipliesAfterUnitConversion) {
  const av::Mesh mesh = av::loadObj(fixture("box.obj"), "cm", 2.0);
  const av::Aabb box = av::boundsOf(mesh);
  // 1 cm-unit * 2.0 scale = 0.02 m edge length.
  EXPECT_NEAR(box.max.x, 0.02, 1e-12);
}

TEST(ObjImport, MaterialZonesFromGroups) {
  const av::Mesh mesh = av::loadObj(fixture("box.obj"), "m", 1.0);
  // box.obj declares groups floor, ceiling, walls.
  ASSERT_EQ(mesh.materialSlots.size(), 3u);
  EXPECT_NE(std::find(mesh.materialSlots.begin(), mesh.materialSlots.end(),
                      "floor"),
            mesh.materialSlots.end());
  EXPECT_NE(std::find(mesh.materialSlots.begin(), mesh.materialSlots.end(),
                      "ceiling"),
            mesh.materialSlots.end());
  EXPECT_NE(std::find(mesh.materialSlots.begin(), mesh.materialSlots.end(),
                      "walls"),
            mesh.materialSlots.end());

  // floor (2) + ceiling (2) + walls (8) = 12; every triangle has a valid slot.
  for (const av::Triangle& triangle : mesh.triangles) {
    EXPECT_LT(triangle.materialSlot, mesh.materialSlots.size());
  }
}

TEST(ObjImport, MaterialOverridesApplied) {
  const av::MeshRoom room{
      .uri = fixture("box.obj").string(),
      .unit = "m",
      .scale = 1.0,
      .materialOverrides = {{"floor", "mat-carpet"}, {"walls", "mat-brick"}},
  };
  const av::Mesh mesh = av::loadMeshRoom(room);

  ASSERT_EQ(mesh.materialSlots.size(), 3u);
  const auto& slots = mesh.materialSlots;
  EXPECT_NE(std::find(slots.begin(), slots.end(), "mat-carpet"), slots.end());
  EXPECT_NE(std::find(slots.begin(), slots.end(), "mat-brick"), slots.end());
  // Unmapped zone keeps its original name.
  EXPECT_NE(std::find(slots.begin(), slots.end(), "ceiling"), slots.end());
  // Overrides must not leave the original names behind.
  EXPECT_EQ(std::find(slots.begin(), slots.end(), "floor"), slots.end());
  EXPECT_EQ(std::find(slots.begin(), slots.end(), "walls"), slots.end());
}

TEST(ObjImport, UnknownUnitThrows) {
  EXPECT_THROW(av::loadObj(fixture("box.obj"), "furlongs", 1.0),
               std::runtime_error);
}

TEST(ObjImport, MissingFileThrows) {
  EXPECT_THROW(av::loadObj(fixture("no-such-file.obj"), "m", 1.0),
               std::runtime_error);
}

TEST(ObjImport, NonPositiveScaleThrows) {
  EXPECT_THROW(av::loadObj(fixture("box.obj"), "m", 0.0), std::runtime_error);
}
