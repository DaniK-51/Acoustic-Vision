#include <gtest/gtest.h>

#include <av/io/io.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
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

namespace {

av::Scenario makeMeshScenario(const std::string& uri) {
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Mesh;
  scenario.room.mesh.uri = uri;
  scenario.room.mesh.unit = "m";
  scenario.room.mesh.scale = 1.0;
  return scenario;
}

}  // namespace

TEST(FillLoadedMesh, AbsoluteUriFillsMesh) {
  av::Scenario scenario = makeMeshScenario(fixture("box.obj").string());
  av::fillLoadedMesh(scenario);
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
  EXPECT_EQ(scenario.loadedMesh->positions.size(), 8u);
}

TEST(FillLoadedMesh, RelativeUriResolvesAgainstBaseDir) {
  av::Scenario scenario = makeMeshScenario("box.obj");
  av::fillLoadedMesh(scenario, fixture(""));
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
}

TEST(FillLoadedMesh, RelativeUriWithoutBaseDirUsesCwd) {
  const std::filesystem::path previous =
      std::filesystem::current_path();
  std::filesystem::current_path(fixture(""));
  av::Scenario scenario = makeMeshScenario("box.obj");
  av::fillLoadedMesh(scenario);
  std::filesystem::current_path(previous);
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
}

TEST(FillLoadedMesh, IdempotentKeepsFirstMesh) {
  av::Scenario scenario = makeMeshScenario(fixture("box.obj").string());
  av::fillLoadedMesh(scenario);
  const auto* first = scenario.loadedMesh.get();
  ASSERT_NE(first, nullptr);
  // A second call must not reload (or clobber) the existing mesh, even if
  // the uri became unloadable meanwhile.
  scenario.room.mesh.uri = "no-such-file.obj";
  av::fillLoadedMesh(scenario);
  EXPECT_EQ(scenario.loadedMesh.get(), first);
}

TEST(FillLoadedMesh, RectRoomLeavesMeshEmpty) {
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Rect;
  scenario.room.mesh.uri = fixture("box.obj").string();
  av::fillLoadedMesh(scenario);
  EXPECT_EQ(scenario.loadedMesh, nullptr);
}

TEST(FillLoadedMesh, EmptyUriLeavesMeshEmpty) {
  av::Scenario scenario = makeMeshScenario("");
  av::fillLoadedMesh(scenario);
  EXPECT_EQ(scenario.loadedMesh, nullptr);
}

TEST(FillLoadedMesh, MissingFileThrows) {
  av::Scenario scenario = makeMeshScenario(fixture("no-such-file.obj").string());
  try {
    av::fillLoadedMesh(scenario);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("no-such-file.obj"), std::string::npos);
    EXPECT_NE(message.find("failed to load mesh"), std::string::npos);
  }
  EXPECT_EQ(scenario.loadedMesh, nullptr);
}

TEST(FillLoadedMesh, AppliesMaterialOverrides) {
  av::Scenario scenario = makeMeshScenario(fixture("box.obj").string());
  scenario.room.mesh.materialOverrides = {{"floor", "mat-carpet"},
                                          {"walls", "mat-brick"}};
  av::fillLoadedMesh(scenario);
  ASSERT_NE(scenario.loadedMesh, nullptr);
  const auto& slots = scenario.loadedMesh->materialSlots;
  EXPECT_NE(std::find(slots.begin(), slots.end(), "mat-carpet"), slots.end());
  EXPECT_NE(std::find(slots.begin(), slots.end(), "mat-brick"), slots.end());
  EXPECT_EQ(std::find(slots.begin(), slots.end(), "floor"), slots.end());
  EXPECT_EQ(std::find(slots.begin(), slots.end(), "walls"), slots.end());
}

TEST(FillLoadedMesh, FileUriPrefixStripped) {
  av::Scenario scenario = makeMeshScenario("file://" + fixture("box.obj").string());
  av::fillLoadedMesh(scenario);
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
}
