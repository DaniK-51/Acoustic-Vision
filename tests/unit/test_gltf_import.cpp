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

av::Scenario makeMeshScenario(const std::string& uri) {
  av::Scenario scenario;
  scenario.room.type = av::Room::Type::Mesh;
  scenario.room.mesh.uri = uri;
  scenario.room.mesh.unit = "m";
  scenario.room.mesh.scale = 1.0;
  return scenario;
}

}  // namespace

TEST(GltfImport, BoxVertexAndFaceCounts) {
  const av::Mesh mesh = av::loadGltf(fixture("box.gltf"), "m", 1.0);
  // 3 primitives share the 8-vertex POSITION accessor; indices stay in range.
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
}

TEST(GltfImport, BoxBoundsInMeters) {
  const av::Mesh mesh = av::loadGltf(fixture("box.gltf"), "m", 1.0);
  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.min.x, 0.0);
  EXPECT_DOUBLE_EQ(box.max.x, 1.0);
  EXPECT_DOUBLE_EQ(box.max.y, 1.0);
  EXPECT_DOUBLE_EQ(box.max.z, 1.0);
  EXPECT_NEAR(av::surfaceArea(mesh), 6.0, 1e-9);
}

TEST(GltfImport, MillimetersConvertToMeters) {
  const av::Mesh mesh = av::loadGltf(fixture("box.gltf"), "mm", 1.0);
  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_NEAR(box.max.x, 0.001, 1e-12);
  EXPECT_NEAR(box.max.y, 0.001, 1e-12);
  EXPECT_NEAR(box.max.z, 0.001, 1e-12);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
}

TEST(GltfImport, ScaleMultipliesAfterUnitConversion) {
  const av::Mesh mesh = av::loadGltf(fixture("box.gltf"), "cm", 2.0);
  const av::Aabb box = av::boundsOf(mesh);
  // 1 cm-unit * 2.0 scale = 0.02 m edge length.
  EXPECT_NEAR(box.max.x, 0.02, 1e-12);
}

TEST(GltfImport, MaterialZonesFromMaterialNames) {
  const av::Mesh mesh = av::loadGltf(fixture("box.gltf"), "m", 1.0);
  // box.gltf names materials floor, ceiling, walls (OBJ zone philosophy).
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

  std::map<std::string, std::size_t> trianglesPerSlot;
  for (const av::Triangle& triangle : mesh.triangles) {
    ++trianglesPerSlot[mesh.materialSlots[triangle.materialSlot]];
  }
  EXPECT_EQ(trianglesPerSlot["floor"], 2u);
  EXPECT_EQ(trianglesPerSlot["ceiling"], 2u);
  EXPECT_EQ(trianglesPerSlot["walls"], 8u);
}

TEST(GltfImport, MaterialOverridesApplied) {
  const av::MeshRoom room{
      .uri = fixture("box.gltf").string(),
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

TEST(GltfImport, GlbBinaryBoxMatchesGltf) {
  const av::Mesh mesh = av::loadGltf(fixture("box.glb"), "m", 1.0);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
  const av::Aabb box = av::boundsOf(mesh);
  EXPECT_DOUBLE_EQ(box.max.x, 1.0);
  EXPECT_DOUBLE_EQ(box.max.y, 1.0);
  EXPECT_DOUBLE_EQ(box.max.z, 1.0);
  ASSERT_EQ(mesh.materialSlots.size(), 3u);
}

TEST(GltfImport, NonTrianglePrimitiveThrows) {
  try {
    [[maybe_unused]] const av::Mesh mesh =
        av::loadGltf(fixture("points.gltf"), "m", 1.0);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("TRIANGLES"), std::string::npos);
  }
}

TEST(GltfImport, UnknownUnitThrows) {
  EXPECT_THROW(av::loadGltf(fixture("box.gltf"), "furlongs", 1.0),
               std::runtime_error);
}

TEST(GltfImport, MissingFileThrows) {
  EXPECT_THROW(av::loadGltf(fixture("no-such-file.gltf"), "m", 1.0),
               std::runtime_error);
}

TEST(GltfImport, NonPositiveScaleThrows) {
  EXPECT_THROW(av::loadGltf(fixture("box.gltf"), "m", 0.0), std::runtime_error);
}

TEST(MeshImport, DispatchesObjByExtension) {
  const av::Mesh mesh = av::loadMesh(fixture("box.obj"), "m", 1.0);
  EXPECT_EQ(mesh.positions.size(), 8u);
  EXPECT_EQ(mesh.triangles.size(), 12u);
}

TEST(MeshImport, DispatchesGltfAndGlbByExtension) {
  EXPECT_EQ(av::loadMesh(fixture("box.gltf"), "m", 1.0).triangles.size(), 12u);
  EXPECT_EQ(av::loadMesh(fixture("box.glb"), "m", 1.0).triangles.size(), 12u);
}

TEST(MeshImport, ExtensionDispatchIsCaseInsensitive) {
  try {
    [[maybe_unused]] const av::Mesh mesh =
        av::loadMesh(fixture("no-such-file.GLB"), "m", 1.0);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    // Uppercase .GLB must reach the glTF loader, not the unknown-format arm.
    EXPECT_EQ(message.find("unsupported mesh format"), std::string::npos)
        << message;
  }
}

TEST(MeshImport, UnknownExtensionThrows) {
  try {
    [[maybe_unused]] const av::Mesh mesh =
        av::loadMesh(fixture("box-scenario.json"), "m", 1.0);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& error) {
    EXPECT_NE(std::string(error.what()).find("unsupported mesh format"),
              std::string::npos);
  }
}

TEST(FillLoadedMeshGltf, AbsoluteUriFillsMesh) {
  av::Scenario scenario = makeMeshScenario(fixture("box.gltf").string());
  av::fillLoadedMesh(scenario);
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
  EXPECT_EQ(scenario.loadedMesh->positions.size(), 8u);
}

TEST(FillLoadedMeshGltf, RelativeUriResolvesAgainstBaseDir) {
  av::Scenario scenario = makeMeshScenario("box.glb");
  av::fillLoadedMesh(scenario, fixture(""));
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
}

TEST(FillLoadedMeshGltf, AppliesMaterialOverrides) {
  av::Scenario scenario = makeMeshScenario(fixture("box.gltf").string());
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

TEST(FillLoadedMeshGltf, FileUriPrefixStripped) {
  av::Scenario scenario =
      makeMeshScenario("file://" + fixture("box.gltf").string());
  av::fillLoadedMesh(scenario);
  ASSERT_NE(scenario.loadedMesh, nullptr);
  EXPECT_EQ(scenario.loadedMesh->triangles.size(), 12u);
}

TEST(FillLoadedMeshGltf, MissingFileThrows) {
  av::Scenario scenario =
      makeMeshScenario(fixture("no-such-file.gltf").string());
  try {
    av::fillLoadedMesh(scenario);
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error& error) {
    const std::string message = error.what();
    EXPECT_NE(message.find("no-such-file.gltf"), std::string::npos);
    EXPECT_NE(message.find("failed to load mesh"), std::string::npos);
  }
  EXPECT_EQ(scenario.loadedMesh, nullptr);
}
