#include <gtest/gtest.h>

#include <av/io/io.hpp>
#include <av/scene/scene_model.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

namespace {

std::filesystem::path tempPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / ("av_test_scene_" + name);
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

av::Wall makeWall() {
  av::Wall wall;
  wall.id = "wall-north";
  wall.name = "North";
  wall.start = {0.0, 0.0, 0.0};
  wall.end = {6.0, 0.0, 0.0};
  wall.height = 2.8;
  wall.thickness = 0.2;
  wall.faces.inner = "mat-brick";
  wall.faces.outer = "mat-brick";
  wall.faces.side = "mat-plaster";
  av::WallOpening door;
  door.type = av::WallOpening::Type::Door;
  door.offset = 1.0;
  door.width = 0.9;
  door.height = 2.1;
  door.sill = 0.0;
  wall.openings.push_back(door);
  return wall;
}

av::SolidBox makeSolidBox() {
  av::SolidBox box;
  box.id = "block-1";
  box.name = "Block";
  box.origin = {1.0, 0.0, 1.0};
  box.size = {0.5, 1.2, 0.5};
  box.materialId = "mat-brick";
  return box;
}

av::SolidExtrude makeSolidExtrude() {
  av::SolidExtrude extrude;
  extrude.id = "l-wall";
  extrude.name = "L wall";
  extrude.polygon = {{0.0, 0.0}, {2.0, 0.0}, {2.0, 0.3}, {0.3, 0.3},
                     {0.3, 2.0}, {0.0, 2.0}};
  extrude.height = 2.8;
  extrude.materialId = "mat-plaster";
  return extrude;
}

av::Void makeVoid() {
  av::Void voidEntity;
  voidEntity.id = "void-1";
  voidEntity.name = "Niche";
  voidEntity.origin = {0.0, 0.0, 0.0};
  voidEntity.size = {0.5, 0.5, 0.1};
  return voidEntity;
}

av::SourceRef makeSource() {
  av::SourceRef source;
  source.id = "src-1";
  source.name = "Speaker";
  source.position = {1.2, 1.5, 1.0};
  source.powerDb = 80.0;
  return source;
}

av::ReceiverRef makeReceiver() {
  av::ReceiverRef receiver;
  receiver.id = "rcv-1";
  receiver.position = {4.0, 1.5, 3.0};
  return receiver;
}

av::Scene makeScene() {
  av::Scene scene;
  scene.schemaVersion = 1;
  scene.id = "demo-scene";
  scene.units = "m";
  scene.materials.push_back(makeMaterial("mat-brick"));
  scene.materials.push_back(makeMaterial("mat-plaster"));
  scene.entities.push_back(makeWall());
  scene.entities.push_back(makeSolidBox());
  scene.entities.push_back(makeSolidExtrude());
  scene.entities.push_back(makeVoid());
  scene.entities.push_back(makeSource());
  scene.entities.push_back(makeReceiver());
  scene.meshSettings.surface.algorithm =
      av::MeshSettings::SurfaceAlgorithm::Earclip;
  scene.meshSettings.surface.targetEdge = 0.2;
  scene.meshSettings.surface.maxEdge = 0.5;
  scene.meshSettings.surface.quality = av::MeshSettings::Quality::Standard;
  scene.meshSettings.volume.algorithm = av::MeshSettings::VolumeAlgorithm::HexBox;
  scene.meshSettings.volume.targetSize = 0.15;
  scene.meshSettings.volume.enabled = true;
  return scene;
}

bool hasErrorContaining(const std::vector<std::string>& errors,
                        const std::string& needle) {
  for (const std::string& error : errors) {
    if (error.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(SceneModel, EntityHelpers) {
  const av::Scene scene = makeScene();
  EXPECT_EQ(av::entityType(scene.entities[0]), "wall");
  EXPECT_EQ(av::entityType(scene.entities[1]), "solid_box");
  EXPECT_EQ(av::entityType(scene.entities[2]), "solid_extrude");
  EXPECT_EQ(av::entityType(scene.entities[3]), "void");
  EXPECT_EQ(av::entityType(scene.entities[4]), "source");
  EXPECT_EQ(av::entityType(scene.entities[5]), "receiver");
  EXPECT_EQ(av::entityId(scene.entities[0]), "wall-north");
}

TEST(SceneModel, ValidSceneHasNoErrors) {
  const av::Scene scene = makeScene();
  EXPECT_TRUE(av::validateScene(scene).empty());
}

TEST(SceneModel, JsonRoundTrip) {
  const av::Scene scene = makeScene();
  const nlohmann::json json = av::sceneToJson(scene);
  const av::Scene back = av::sceneFromJson(json);

  EXPECT_EQ(back.schemaVersion, scene.schemaVersion);
  EXPECT_EQ(back.id, scene.id);
  EXPECT_EQ(back.units, scene.units);
  ASSERT_EQ(back.materials.size(), scene.materials.size());
  for (std::size_t i = 0; i < back.materials.size(); ++i) {
    EXPECT_EQ(back.materials[i].id, scene.materials[i].id);
    EXPECT_EQ(back.materials[i].absorption, scene.materials[i].absorption);
    EXPECT_DOUBLE_EQ(back.materials[i].scattering,
                     scene.materials[i].scattering);
  }

  ASSERT_EQ(back.entities.size(), scene.entities.size());
  for (std::size_t i = 0; i < back.entities.size(); ++i) {
    EXPECT_EQ(av::entityType(back.entities[i]), av::entityType(scene.entities[i]));
    EXPECT_EQ(av::entityId(back.entities[i]), av::entityId(scene.entities[i]));
  }

  const auto& wall = std::get<av::Wall>(back.entities[0]);
  EXPECT_DOUBLE_EQ(wall.height, 2.8);
  EXPECT_DOUBLE_EQ(wall.thickness, 0.2);
  EXPECT_DOUBLE_EQ(wall.start.x, 0.0);
  EXPECT_DOUBLE_EQ(wall.end.x, 6.0);
  EXPECT_EQ(wall.faces.inner, "mat-brick");
  EXPECT_EQ(wall.faces.side, "mat-plaster");
  ASSERT_EQ(wall.openings.size(), 1U);
  EXPECT_EQ(wall.openings[0].type, av::WallOpening::Type::Door);
  EXPECT_DOUBLE_EQ(wall.openings[0].offset, 1.0);
  EXPECT_DOUBLE_EQ(wall.openings[0].width, 0.9);
  EXPECT_DOUBLE_EQ(wall.openings[0].height, 2.1);
  EXPECT_DOUBLE_EQ(wall.openings[0].sill, 0.0);

  const auto& box = std::get<av::SolidBox>(back.entities[1]);
  EXPECT_DOUBLE_EQ(box.size.y, 1.2);
  EXPECT_EQ(box.materialId, "mat-brick");

  const auto& extrude = std::get<av::SolidExtrude>(back.entities[2]);
  ASSERT_EQ(extrude.polygon.size(), 6U);
  EXPECT_DOUBLE_EQ(extrude.polygon[1].x, 2.0);
  EXPECT_DOUBLE_EQ(extrude.height, 2.8);

  const auto& voidEntity = std::get<av::Void>(back.entities[3]);
  EXPECT_DOUBLE_EQ(voidEntity.size.z, 0.1);

  const auto& source = std::get<av::SourceRef>(back.entities[4]);
  EXPECT_DOUBLE_EQ(source.powerDb, 80.0);
  EXPECT_DOUBLE_EQ(source.position.y, 1.5);

  const auto& receiver = std::get<av::ReceiverRef>(back.entities[5]);
  EXPECT_DOUBLE_EQ(receiver.position.x, 4.0);

  EXPECT_EQ(back.meshSettings.surface.algorithm,
            av::MeshSettings::SurfaceAlgorithm::Earclip);
  ASSERT_TRUE(back.meshSettings.surface.targetEdge.has_value());
  ASSERT_TRUE(back.meshSettings.surface.maxEdge.has_value());
  EXPECT_DOUBLE_EQ(*back.meshSettings.surface.targetEdge, 0.2);
  EXPECT_DOUBLE_EQ(*back.meshSettings.surface.maxEdge, 0.5);
  EXPECT_EQ(back.meshSettings.surface.quality, av::MeshSettings::Quality::Standard);
  EXPECT_EQ(back.meshSettings.volume.algorithm,
            av::MeshSettings::VolumeAlgorithm::HexBox);
  EXPECT_DOUBLE_EQ(back.meshSettings.volume.targetSize, 0.15);
  EXPECT_TRUE(back.meshSettings.volume.enabled);
}

TEST(SceneModel, JsonWireShapeMatchesSketch) {
  const av::Scene scene = makeScene();
  const nlohmann::json json = av::sceneToJson(scene);
  EXPECT_EQ(json.at("schemaVersion"), 1);
  EXPECT_EQ(json.at("units"), "m");
  ASSERT_TRUE(json.at("entities").is_array());
  ASSERT_TRUE(json.at("sources").is_array());
  ASSERT_TRUE(json.at("receivers").is_array());
  // Geometry stays in entities; S/R placements move to top-level arrays.
  EXPECT_EQ(json.at("entities").size(), 4U);
  EXPECT_EQ(json.at("sources").size(), 1U);
  EXPECT_EQ(json.at("receivers").size(), 1U);
  EXPECT_EQ(json.at("entities").at(0).at("type"), "wall");
  EXPECT_EQ(json.at("sources").at(0).at("id"), "src-1");
  EXPECT_FALSE(json.at("sources").at(0).contains("type"));
  EXPECT_EQ(json.at("receivers").at(0).at("id"), "rcv-1");
  ASSERT_TRUE(json.contains("meshSettings"));
  EXPECT_EQ(json.at("meshSettings").at("surface").at("quality"), "standard");
  EXPECT_EQ(json.at("meshSettings").at("volume").at("algorithm"), "hex-box");
}

TEST(SceneModel, TypedSourceReceiverInsideEntitiesAlsoParse) {
  nlohmann::json json;
  json["schemaVersion"] = 1;
  json["units"] = "m";
  json["materials"] = nlohmann::json::array();
  json["entities"] = nlohmann::json::array();
  nlohmann::json source;
  source["type"] = "source";
  source["id"] = "src-inline";
  source["position"] = nlohmann::json::array({0.0, 1.0, 0.0});
  source["powerDb"] = 70.0;
  json["entities"].push_back(source);
  nlohmann::json receiver;
  receiver["type"] = "receiver";
  receiver["id"] = "rcv-inline";
  receiver["position"] = nlohmann::json::array({1.0, 1.0, 1.0});
  json["entities"].push_back(receiver);

  const av::Scene scene = av::sceneFromJson(json);
  ASSERT_EQ(scene.entities.size(), 2U);
  EXPECT_EQ(av::entityType(scene.entities[0]), "source");
  EXPECT_EQ(av::entityType(scene.entities[1]), "receiver");
  // Round-trip still lands them in the sketch homes.
  const nlohmann::json back = av::sceneToJson(scene);
  EXPECT_EQ(back.at("sources").size(), 1U);
  EXPECT_EQ(back.at("receivers").size(), 1U);
  EXPECT_TRUE(back.at("entities").empty());
}

TEST(SceneModel, FileRoundTrip) {
  const av::Scene scene = makeScene();
  const std::filesystem::path path = tempPath("round_trip.json");
  av::saveScene(scene, path);
  const av::Scene back = av::loadScene(path);
  EXPECT_EQ(av::sceneToJson(back), av::sceneToJson(scene));
  std::filesystem::remove(path);
}

TEST(SceneModel, ValidationRejectsNonPositiveSizes) {
  av::Scene scene = makeScene();
  std::get<av::Wall>(scene.entities[0]).height = 0.0;
  std::get<av::SolidBox>(scene.entities[1]).size.y = -1.0;
  std::get<av::SolidExtrude>(scene.entities[2]).height = 0.0;
  std::get<av::Void>(scene.entities[3]).size.z = 0.0;
  const std::vector<std::string> errors = av::validateScene(scene);
  EXPECT_TRUE(hasErrorContaining(errors, "wall 'wall-north': height"));
  EXPECT_TRUE(hasErrorContaining(errors, "solid_box 'block-1': size"));
  EXPECT_TRUE(hasErrorContaining(errors, "solid_extrude 'l-wall': height"));
  EXPECT_TRUE(hasErrorContaining(errors, "void 'void-1': size"));
}

TEST(SceneModel, ValidationRejectsDuplicateEntityIds) {
  av::Scene scene = makeScene();
  std::get<av::SolidBox>(scene.entities[1]).id = "wall-north";
  const std::vector<std::string> errors = av::validateScene(scene);
  EXPECT_TRUE(hasErrorContaining(errors, "duplicate entity id 'wall-north'"));
}

TEST(SceneModel, ValidationRejectsUnknownMaterialId) {
  av::Scene scene = makeScene();
  std::get<av::SolidBox>(scene.entities[1]).materialId = "mat-missing";
  std::get<av::Wall>(scene.entities[0]).faces.side = "mat-also-missing";
  const std::vector<std::string> errors = av::validateScene(scene);
  EXPECT_TRUE(hasErrorContaining(errors, "solid_box 'block-1': unknown materialId 'mat-missing'"));
  EXPECT_TRUE(
      hasErrorContaining(errors, "faces.side: unknown materialId 'mat-also-missing'"));
}

TEST(SceneModel, ValidationRejectsOpeningOutsideWall) {
  av::Scene scene = makeScene();
  auto& wall = std::get<av::Wall>(scene.entities[0]);
  wall.openings[0].offset = 5.5;
  wall.openings[0].width = 1.0;  // 6.5 > baseline 6.0
  wall.openings.push_back(av::WallOpening{av::WallOpening::Type::Window, 0.5,
                                          1.0, 1.0, 2.5});  // 2.5+1.0 > 2.8
  const std::vector<std::string> errors = av::validateScene(scene);
  EXPECT_TRUE(hasErrorContaining(errors, "openings[0]"));
  EXPECT_TRUE(hasErrorContaining(errors, "does not fit along the wall baseline"));
  EXPECT_TRUE(hasErrorContaining(errors, "openings[1]"));
  EXPECT_TRUE(hasErrorContaining(errors, "does not fit in the wall height"));
}

TEST(SceneModel, ValidationRejectsDegenerateWallAndPolygon) {
  av::Scene scene = makeScene();
  std::get<av::Wall>(scene.entities[0]).end = {0.0, 0.0, 0.0};
  std::get<av::SolidExtrude>(scene.entities[2]).polygon = {{0.0, 0.0},
                                                           {1.0, 0.0}};
  const std::vector<std::string> errors = av::validateScene(scene);
  EXPECT_TRUE(hasErrorContaining(errors, "start and end must differ"));
  EXPECT_TRUE(hasErrorContaining(errors, "polygon needs at least 3 points"));
}

TEST(SceneModel, ValidationRejectsBadUnitsAndMeshSettings) {
  av::Scene scene = makeScene();
  scene.units = "mm";
  scene.meshSettings.surface.targetEdge = 1.5;
  scene.meshSettings.surface.maxEdge = 1.0;
  scene.meshSettings.volume.targetSize = 0.0;
  const std::vector<std::string> errors = av::validateScene(scene);
  EXPECT_TRUE(hasErrorContaining(errors, "units must be 'm'"));
  EXPECT_TRUE(hasErrorContaining(errors, "targetEdge must not exceed maxEdge"));
  EXPECT_TRUE(hasErrorContaining(errors, "targetSize must be positive"));
}

TEST(SceneModel, ParseRejectsUnknownTypeAndBadOpening) {
  nlohmann::json json;
  json["schemaVersion"] = 1;
  json["units"] = "m";
  json["materials"] = nlohmann::json::array();
  json["entities"] = nlohmann::json::array();
  nlohmann::json entity;
  entity["type"] = "blob";
  entity["id"] = "x";
  json["entities"].push_back(entity);
  EXPECT_THROW(av::sceneFromJson(json), std::runtime_error);

  const nlohmann::json full = av::sceneToJson(makeScene());
  nlohmann::json wallJson = full.at("entities").at(0);
  wallJson["openings"][0]["type"] = "hatch";
  json["entities"] = nlohmann::json::array({wallJson});
  json["materials"] = full.at("materials");
  EXPECT_THROW(av::sceneFromJson(json), std::runtime_error);
}

TEST(SceneModel, LoadMissingFileThrows) {
  EXPECT_THROW(av::loadScene(tempPath("does-not-exist.json")),
               std::runtime_error);
}

TEST(SceneModel, ResolveSurfaceMeshSettingsQualityPresets) {
  av::MeshSettings settings;

  settings.surface.quality = av::MeshSettings::Quality::Draft;
  av::ResolvedSurfaceMeshSettings draft = av::resolveSurfaceMeshSettings(settings);
  EXPECT_DOUBLE_EQ(draft.targetEdge, 0.5);
  EXPECT_DOUBLE_EQ(draft.maxEdge, 1.0);
  EXPECT_EQ(draft.algorithm, av::MeshSettings::SurfaceAlgorithm::Auto);

  settings.surface.quality = av::MeshSettings::Quality::Standard;
  av::ResolvedSurfaceMeshSettings standard =
      av::resolveSurfaceMeshSettings(settings);
  EXPECT_DOUBLE_EQ(standard.targetEdge, 0.2);
  EXPECT_DOUBLE_EQ(standard.maxEdge, 0.5);

  settings.surface.quality = av::MeshSettings::Quality::Fine;
  av::ResolvedSurfaceMeshSettings fine = av::resolveSurfaceMeshSettings(settings);
  EXPECT_DOUBLE_EQ(fine.targetEdge, 0.08);
  EXPECT_DOUBLE_EQ(fine.maxEdge, 0.2);
}

TEST(SceneModel, ResolveSurfaceMeshSettingsExplicitOverridesWin) {
  av::MeshSettings settings;
  settings.surface.quality = av::MeshSettings::Quality::Fine;
  settings.surface.targetEdge = 0.15;
  settings.surface.algorithm = av::MeshSettings::SurfaceAlgorithm::Earclip;

  const av::ResolvedSurfaceMeshSettings resolved =
      av::resolveSurfaceMeshSettings(settings);
  // targetEdge is explicit -> wins; maxEdge is unset -> preset (fine = 0.2).
  EXPECT_DOUBLE_EQ(resolved.targetEdge, 0.15);
  EXPECT_DOUBLE_EQ(resolved.maxEdge, 0.2);
  EXPECT_EQ(resolved.algorithm, av::MeshSettings::SurfaceAlgorithm::Earclip);
}

TEST(SceneModel, UnsetEdgesStayUnsetAcrossJson) {
  av::Scene scene = makeScene();
  scene.meshSettings.surface.targetEdge.reset();
  scene.meshSettings.surface.maxEdge.reset();
  const nlohmann::json json = av::sceneToJson(scene);
  EXPECT_FALSE(json.at("meshSettings").at("surface").contains("targetEdge"));
  EXPECT_FALSE(json.at("meshSettings").at("surface").contains("maxEdge"));
  const av::Scene back = av::sceneFromJson(json);
  EXPECT_FALSE(back.meshSettings.surface.targetEdge.has_value());
  EXPECT_FALSE(back.meshSettings.surface.maxEdge.has_value());
}
