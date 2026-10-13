#include <gtest/gtest.h>

#include <av/io/io.hpp>
#include <av/scene/scene_model.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace {

std::filesystem::path tempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() /
         ("av_test_to_scenario_" + name);
}

av::Material makeMaterial(std::string id) {
  av::Material material;
  material.id = id;
  material.name = id;
  material.bandsHz = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};
  material.absorption = {0.10, 0.10, 0.12, 0.15, 0.15, 0.20};
  material.scattering = 0.1;
  return material;
}

av::Wall makeWall(std::string id, av::Vec3 start, av::Vec3 end,
                  std::string inner) {
  av::Wall wall;
  wall.id = std::move(id);
  wall.start = start;
  wall.end = end;
  wall.height = 2.8;
  wall.thickness = 0.2;
  wall.faces.inner = std::move(inner);
  wall.faces.outer = "mat-outer";
  wall.faces.side = "mat-side";
  return wall;
}

av::SolidBox makeSlab(std::string id, av::Vec3 origin, av::Vec3 size,
                      std::string materialId) {
  av::SolidBox box;
  box.id = std::move(id);
  box.origin = origin;
  box.size = size;
  box.materialId = std::move(materialId);
  return box;
}

// Closed shoebox 6 x 4 x 2.8 (scene Z-up: x=6, y=4, z=2.8) — 4 CCW walls
// whose inner faces bound the room plus floor/ceiling slabs. Positions are
// deliberately off-origin to exercise the rect translation.
av::Scene makeClosedBoxScene(double minX = 0.0, double minY = 0.0) {
  const double w = 6.0;
  const double d = 4.0;
  const double h = 2.8;
  av::Scene scene;
  scene.id = "box-scene";
  scene.materials.push_back(makeMaterial("mat-south"));
  scene.materials.push_back(makeMaterial("mat-east"));
  scene.materials.push_back(makeMaterial("mat-north"));
  scene.materials.push_back(makeMaterial("mat-west"));
  scene.materials.push_back(makeMaterial("mat-floor"));
  scene.materials.push_back(makeMaterial("mat-ceiling"));
  scene.materials.push_back(makeMaterial("mat-outer"));
  scene.materials.push_back(makeMaterial("mat-side"));

  // CCW from +Z: south -> east -> north -> west.
  scene.entities.push_back(makeWall(
      "wall-south", {minX, minY, 0.0}, {minX + w, minY, 0.0}, "mat-south"));
  scene.entities.push_back(makeWall(
      "wall-east", {minX + w, minY, 0.0}, {minX + w, minY + d, 0.0}, "mat-east"));
  scene.entities.push_back(makeWall(
      "wall-north", {minX + w, minY + d, 0.0}, {minX, minY + d, 0.0},
      "mat-north"));
  scene.entities.push_back(makeWall(
      "wall-west", {minX, minY + d, 0.0}, {minX, minY, 0.0}, "mat-west"));
  scene.entities.push_back(
      makeSlab("floor", {minX, minY, 0.0}, {w, d, 0.2}, "mat-floor"));
  scene.entities.push_back(
      makeSlab("ceiling", {minX, minY, h - 0.2}, {w, d, 0.2}, "mat-ceiling"));

  av::SourceRef source;
  source.id = "src-1";
  source.position = {minX + 1.5, minY + 2.0, 1.4};
  source.powerDb = 80.0;
  scene.entities.push_back(source);

  av::ReceiverRef receiver;
  receiver.id = "rcv-1";
  receiver.position = {minX + 4.5, minY + 1.0, 1.2};
  scene.entities.push_back(receiver);
  return scene;
}

}  // namespace

TEST(ScenarioFromScene, ClosedBoxCompilesToRect) {
  const av::Scene scene = makeClosedBoxScene();
  const av::Scenario scenario = av::scenarioFromScene(scene);

  EXPECT_EQ(scenario.room.type, av::Room::Type::Rect);
  EXPECT_DOUBLE_EQ(scenario.room.rect.w, 6.0);
  EXPECT_DOUBLE_EQ(scenario.room.rect.h, 2.8);
  EXPECT_DOUBLE_EQ(scenario.room.rect.d, 4.0);
  EXPECT_EQ(scenario.room.rect.faces.at("floor"), "mat-floor");
  EXPECT_EQ(scenario.room.rect.faces.at("ceiling"), "mat-ceiling");
  EXPECT_EQ(scenario.room.rect.faces.at("x+"), "mat-east");
  EXPECT_EQ(scenario.room.rect.faces.at("x-"), "mat-west");
  EXPECT_EQ(scenario.room.rect.faces.at("z+"), "mat-north");
  EXPECT_EQ(scenario.room.rect.faces.at("z-"), "mat-south");
  EXPECT_EQ(scenario.params.engine, "image-source");
  EXPECT_EQ(scenario.name, "box-scene");
  EXPECT_TRUE(scenario.materials.size() == 8u);
}

TEST(ScenarioFromScene, ClosedBoxSwapsAndTranslatesPositions) {
  // Room min corner at (2, 3): rect origin must move there and the scene
  // Z-up height becomes the rect Y-up h.
  const av::Scene scene = makeClosedBoxScene(2.0, 3.0);
  const av::Scenario scenario = av::scenarioFromScene(scene);

  ASSERT_EQ(scenario.sources.size(), 1u);
  ASSERT_EQ(scenario.receivers.size(), 1u);
  const av::Vec3 source = scenario.sources[0].position;
  EXPECT_DOUBLE_EQ(source.x, 1.5);
  EXPECT_DOUBLE_EQ(source.y, 1.4);  // scene z -> rect y
  EXPECT_DOUBLE_EQ(source.z, 2.0);  // scene y -> rect z
  EXPECT_DOUBLE_EQ(scenario.sources[0].powerDb, 80.0);
  EXPECT_EQ(scenario.sources[0].id, "src-1");

  const av::Vec3 receiver = scenario.receivers[0].position;
  EXPECT_DOUBLE_EQ(receiver.x, 4.5);
  EXPECT_DOUBLE_EQ(receiver.y, 1.2);
  EXPECT_DOUBLE_EQ(receiver.z, 1.0);
  EXPECT_EQ(scenario.receivers[0].id, "rcv-1");
}

TEST(ScenarioFromScene, WallOpeningFallsBackToMesh) {
  av::Scene scene = makeClosedBoxScene();
  auto& wall = std::get<av::Wall>(scene.entities[0]);
  av::WallOpening door;
  door.type = av::WallOpening::Type::Door;
  door.offset = 1.0;
  door.width = 0.9;
  door.height = 2.1;
  wall.openings.push_back(door);

  const std::filesystem::path meshPath = tempPath("opening.obj");
  std::filesystem::remove(meshPath);
  const av::Scenario scenario = av::scenarioFromScene(scene, meshPath);

  EXPECT_EQ(scenario.room.type, av::Room::Type::Mesh);
  EXPECT_EQ(scenario.params.engine, "ray-tracing");
  EXPECT_EQ(scenario.room.mesh.uri, meshPath.generic_string());
  EXPECT_EQ(scenario.room.mesh.unit, "m");
  EXPECT_DOUBLE_EQ(scenario.room.mesh.scale, 1.0);
  EXPECT_TRUE(scenario.room.mesh.materialOverrides.empty());
  EXPECT_TRUE(std::filesystem::exists(meshPath));

  // Mesh branch keeps the scene Z-up frame: positions are copied as-is.
  ASSERT_EQ(scenario.sources.size(), 1u);
  EXPECT_DOUBLE_EQ(scenario.sources[0].position.z, 1.4);
  EXPECT_DOUBLE_EQ(scenario.sources[0].position.y, 2.0);

  // The written OBJ is loadable and carries geometry.
  const av::Mesh mesh = av::loadObj(meshPath);
  EXPECT_FALSE(mesh.positions.empty());
  EXPECT_FALSE(mesh.triangles.empty());
  std::filesystem::remove(meshPath);
}

TEST(ScenarioFromScene, MissingFloorFallsBackToMesh) {
  av::Scene scene = makeClosedBoxScene();
  // Drop the floor slab (last-but-two geometry before S/R).
  scene.entities.erase(scene.entities.begin() + 4);

  const std::filesystem::path meshPath = tempPath("no-floor.obj");
  const av::Scenario scenario = av::scenarioFromScene(scene, meshPath);
  EXPECT_EQ(scenario.room.type, av::Room::Type::Mesh);
  EXPECT_EQ(scenario.params.engine, "ray-tracing");
  std::filesystem::remove(meshPath);
}

TEST(ScenarioFromScene, ExtraGeometryFallsBackToMesh) {
  av::Scene scene = makeClosedBoxScene();
  av::SolidExtrude extrude;
  extrude.id = "l-wall";
  extrude.polygon = {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}};
  extrude.height = 2.8;
  extrude.materialId = "mat-side";
  scene.entities.push_back(extrude);

  const std::filesystem::path meshPath = tempPath("extra.obj");
  const av::Scenario scenario = av::scenarioFromScene(scene, meshPath);
  EXPECT_EQ(scenario.room.type, av::Room::Type::Mesh);
  std::filesystem::remove(meshPath);
}

TEST(ScenarioFromScene, WrongSlabFootprintFallsBackToMesh) {
  av::Scene scene = makeClosedBoxScene();
  auto& floor = std::get<av::SolidBox>(scene.entities[4]);
  floor.size.x = 5.0;  // does not cover the wall rectangle

  const std::filesystem::path meshPath = tempPath("bad-footprint.obj");
  const av::Scenario scenario = av::scenarioFromScene(scene, meshPath);
  EXPECT_EQ(scenario.room.type, av::Room::Type::Mesh);
  std::filesystem::remove(meshPath);
}

TEST(ScenarioFromScene, FullHeightSolidIsNotAFloor) {
  av::Scene scene = makeClosedBoxScene();
  auto& floor = std::get<av::SolidBox>(scene.entities[4]);
  floor.size.z = 2.8;  // spans floor to ceiling — ambiguous role

  const std::filesystem::path meshPath = tempPath("full-height.obj");
  const av::Scenario scenario = av::scenarioFromScene(scene, meshPath);
  EXPECT_EQ(scenario.room.type, av::Room::Type::Mesh);
  std::filesystem::remove(meshPath);
}

TEST(ScenarioFromScene, UnequalWallHeightsFallBackToMesh) {
  av::Scene scene = makeClosedBoxScene();
  std::get<av::Wall>(scene.entities[1]).height = 3.0;

  const std::filesystem::path meshPath = tempPath("unequal.obj");
  const av::Scenario scenario = av::scenarioFromScene(scene, meshPath);
  EXPECT_EQ(scenario.room.type, av::Room::Type::Mesh);
  std::filesystem::remove(meshPath);
}

TEST(ScenarioFromScene, MeshPathRequiredWhenNotAClosedBox) {
  av::Scene scene;
  scene.entities.push_back(
      makeWall("wall-1", {0.0, 0.0, 0.0}, {6.0, 0.0, 0.0}, "mat-south"));
  EXPECT_THROW(av::scenarioFromScene(scene), std::runtime_error);
}

TEST(ScenarioFromScene, MaterialsAndParamsSurviveCompile) {
  const av::Scene scene = makeClosedBoxScene();
  const av::Scenario scenario = av::scenarioFromScene(scene);
  ASSERT_EQ(scenario.materials.size(), scene.materials.size());
  for (std::size_t i = 0; i < scene.materials.size(); ++i) {
    EXPECT_EQ(scenario.materials[i].id, scene.materials[i].id);
    EXPECT_EQ(scenario.materials[i].absorption, scene.materials[i].absorption);
    EXPECT_DOUBLE_EQ(scenario.materials[i].scattering,
                     scene.materials[i].scattering);
  }
  EXPECT_EQ(scenario.params.bandsHz.size(), 6u);
  EXPECT_DOUBLE_EQ(scenario.params.speedOfSound, 343.0);
  EXPECT_EQ(scenario.params.sampleRate, 48000);
  EXPECT_TRUE(scenario.loadedMesh == nullptr);
}
