#include <gtest/gtest.h>

#include <av/io/io.hpp>
#include <av/scene/scene_mesher.hpp>
#include <av/scene/scene_model.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace {

av::Material makeMaterial(std::string id) {
  av::Material material;
  material.id = id;
  material.name = id;
  material.bandsHz = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};
  material.absorption = {0.10, 0.10, 0.12, 0.15, 0.15, 0.20};
  material.scattering = 0.1;
  return material;
}

// Wall along +x with distinct face materials so normals can be checked per
// zone. Baseline is the inner-face bottom edge (see scene_mesher.hpp).
av::Wall makePlainWall() {
  av::Wall wall;
  wall.id = "wall-plain";
  wall.name = "Plain";
  wall.start = {0.0, 0.0, 0.0};
  wall.end = {6.0, 0.0, 0.0};
  wall.height = 2.8;
  wall.thickness = 0.2;
  wall.faces.inner = "mat-inner";
  wall.faces.outer = "mat-outer";
  wall.faces.side = "mat-side";
  return wall;
}

av::Wall makeWallWithDoor() {
  av::Wall wall = makePlainWall();
  wall.id = "wall-door";
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

av::Scene makeScene() {
  av::Scene scene;
  scene.schemaVersion = 1;
  scene.id = "mesher-scene";
  scene.units = "m";
  scene.materials.push_back(makeMaterial("mat-inner"));
  scene.materials.push_back(makeMaterial("mat-outer"));
  scene.materials.push_back(makeMaterial("mat-side"));
  scene.materials.push_back(makeMaterial("mat-brick"));
  scene.materials.push_back(makeMaterial("mat-plaster"));
  scene.entities.push_back(makeWallWithDoor());
  scene.entities.push_back(makeSolidBox());
  scene.entities.push_back(makeSolidExtrude());
  scene.entities.push_back(makeVoid());
  return scene;
}

std::size_t trianglesWithSlot(const av::Mesh& mesh, const std::string& zone) {
  for (std::size_t slot = 0; slot < mesh.materialSlots.size(); ++slot) {
    if (mesh.materialSlots[slot] == zone) {
      std::size_t count = 0;
      for (const av::Triangle& tri : mesh.triangles) {
        if (tri.materialSlot == slot) {
          ++count;
        }
      }
      return count;
    }
  }
  return 0;
}

av::Vec3 slotNormal(const av::Mesh& mesh, const std::string& zone) {
  for (std::size_t slot = 0; slot < mesh.materialSlots.size(); ++slot) {
    if (mesh.materialSlots[slot] == zone) {
      for (const av::Triangle& tri : mesh.triangles) {
        if (tri.materialSlot == slot) {
          return av::faceNormal(mesh, tri);
        }
      }
    }
  }
  return {};
}

bool hasWarningContaining(const av::SurfaceMeshResult& result,
                          const std::string& needle) {
  for (const std::string& warning : result.warnings) {
    if (warning.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool hasVolumeWarningContaining(const av::VolumeMeshResult& result,
                                const std::string& needle) {
  for (const std::string& warning : result.warnings) {
    if (warning.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(SceneMesher, WallWithoutOpeningsIsTwelveTriangles) {
  av::Scene scene;
  scene.entities.push_back(makePlainWall());

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  EXPECT_TRUE(result.warnings.empty());
  EXPECT_EQ(result.mesh.positions.size(), 8u);
  EXPECT_EQ(result.mesh.triangles.size(), 12u);
  ASSERT_EQ(result.entityTriangleCount.count("wall-plain"), 1u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-plain"), 12u);

  // Zones are material ids (inner / outer / side), 2 quads + 8 side tris.
  ASSERT_EQ(result.mesh.materialSlots.size(), 3u);
  EXPECT_EQ(result.mesh.materialSlots[0], "mat-inner");
  EXPECT_EQ(result.mesh.materialSlots[1], "mat-outer");
  EXPECT_EQ(result.mesh.materialSlots[2], "mat-side");
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-inner"), 2u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-outer"), 2u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-side"), 8u);

  // 2*(L*H) + 2*(L*T) + 2*(H*T) = 2*16.8 + 2*1.2 + 2*0.56 = 37.12
  EXPECT_NEAR(av::surfaceArea(result.mesh), 37.12, 1e-9);
}

TEST(SceneMesher, WallNormalsFollowIntoRoomConvention) {
  // Wall along +x, thickness to the right (-y), room on the left (+y).
  av::Scene scene;
  scene.entities.push_back(makePlainWall());
  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);

  const av::Vec3 inner = slotNormal(result.mesh, "mat-inner");
  EXPECT_NEAR(inner.x, 0.0, 1e-12);
  EXPECT_NEAR(inner.y, 1.0, 1e-12);
  EXPECT_NEAR(inner.z, 0.0, 1e-12);

  const av::Vec3 outer = slotNormal(result.mesh, "mat-outer");
  EXPECT_NEAR(outer.x, 0.0, 1e-12);
  EXPECT_NEAR(outer.y, -1.0, 1e-12);
  EXPECT_NEAR(outer.z, 0.0, 1e-12);

  // Side caps must all point out of the slab (dot with outward > 0).
  for (const av::Triangle& tri : result.mesh.triangles) {
    if (result.mesh.materialSlots[tri.materialSlot] != "mat-side") {
      continue;
    }
    const av::Vec3 n = av::faceNormal(result.mesh, tri);
    const av::Vec3 center =
        (result.mesh.positions[tri.v0] + result.mesh.positions[tri.v1] +
         result.mesh.positions[tri.v2]) /
        3.0;
    // Slab occupies y in [-0.2, 0]; outward from the slab midpoint (0, -0.1).
    const av::Vec3 outward = av::normalized(center - av::Vec3{3.0, -0.1, 1.4});
    EXPECT_GT(av::dot(n, outward), 0.5) << "cap normal not outward";
  }
}

TEST(SceneMesher, WallWithDoorSplitsFacesAroundHole) {
  av::Scene scene;
  scene.entities.push_back(makeWallWithDoor());

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  // Door 0.9 x 2.1 at offset 1.0, sill 0 on a 6 x 2.8 x 0.2 wall:
  // grid 3x2 minus the hole -> 5 quads per big face (10 tris each),
  // plus 4 caps (8 tris) = 28.
  EXPECT_EQ(result.mesh.triangles.size(), 28u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-door"), 28u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-inner"), 10u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-outer"), 10u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-side"), 8u);

  // Inner + outer faces lose the hole area 0.9*2.1 = 1.89 each.
  EXPECT_NEAR(av::surfaceArea(result.mesh), 37.12 - 2.0 * 1.89, 1e-9);
  EXPECT_TRUE(hasWarningContaining(result, "openings[0]"));
  EXPECT_TRUE(hasWarningContaining(result, "simple hole"));
}

TEST(SceneMesher, SolidBoxIsTwelveTrianglesOutward) {
  av::Scene scene;
  scene.entities.push_back(makeSolidBox());

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  EXPECT_TRUE(result.warnings.empty());
  EXPECT_EQ(result.mesh.positions.size(), 8u);
  EXPECT_EQ(result.mesh.triangles.size(), 12u);
  EXPECT_EQ(result.entityTriangleCount.at("block-1"), 12u);
  ASSERT_EQ(result.mesh.materialSlots.size(), 1u);
  EXPECT_EQ(result.mesh.materialSlots[0], "mat-brick");

  // 2*(0.5*1.2 + 0.5*0.5 + 1.2*0.5) = 2.9
  EXPECT_NEAR(av::surfaceArea(result.mesh), 2.9, 1e-9);

  // Every face normal points outward from the box center.
  const av::Vec3 center{1.25, 0.6, 1.25};
  for (const av::Triangle& tri : result.mesh.triangles) {
    const av::Vec3 n = av::faceNormal(result.mesh, tri);
    const av::Vec3 faceCenter =
        (result.mesh.positions[tri.v0] + result.mesh.positions[tri.v1] +
         result.mesh.positions[tri.v2]) /
        3.0;
    EXPECT_GT(av::dot(n, av::normalized(faceCenter - center)), 0.5);
  }
}

TEST(SceneMesher, SolidExtrudeIsPrismWithEarclipCaps) {
  av::Scene scene;
  scene.entities.push_back(makeSolidExtrude());

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  // 6 side quads (12 tris) + 2 caps x 4 ear triangles = 20.
  EXPECT_EQ(result.mesh.triangles.size(), 20u);
  EXPECT_EQ(result.entityTriangleCount.at("l-wall"), 20u);
  ASSERT_EQ(result.mesh.materialSlots.size(), 1u);
  EXPECT_EQ(result.mesh.materialSlots[0], "mat-plaster");

  // Perimeter 8.0 x height 2.8 + 2 * polygon area 1.11 = 22.4 + 2.22.
  EXPECT_NEAR(av::surfaceArea(result.mesh), 22.4 + 2.22, 1e-9);
  EXPECT_FALSE(hasWarningContaining(result, "ear-clip fallback"));

  // Top cap faces +z, bottom cap -z.
  double topArea = 0.0;
  double bottomArea = 0.0;
  for (const av::Triangle& tri : result.mesh.triangles) {
    const av::Vec3 n = av::faceNormal(result.mesh, tri);
    const double area = av::surfaceArea(av::Mesh{
        result.mesh.positions, {}, {tri}, result.mesh.materialSlots});
    if (n.z > 0.5) {
      topArea += area;
    } else if (n.z < -0.5) {
      bottomArea += area;
    }
  }
  EXPECT_NEAR(topArea, 1.11, 1e-9);
  EXPECT_NEAR(bottomArea, 1.11, 1e-9);
}

TEST(SceneMesher, SolidExtrudeReversedPolygonIsNormalized) {
  av::Scene scene;
  av::SolidExtrude extrude = makeSolidExtrude();
  std::reverse(extrude.polygon.begin(), extrude.polygon.end());  // now CW
  scene.entities.push_back(extrude);

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  EXPECT_EQ(result.mesh.triangles.size(), 20u);
  EXPECT_NEAR(av::surfaceArea(result.mesh), 22.4 + 2.22, 1e-9);
}

TEST(SceneMesher, VoidWarnsAndContributesNoTriangles) {
  av::Scene scene;
  scene.entities.push_back(makeVoid());

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  EXPECT_EQ(result.mesh.triangles.size(), 0u);
  EXPECT_TRUE(result.entityTriangleCount.empty());
  ASSERT_EQ(result.warnings.size(), 1u);
  EXPECT_TRUE(hasWarningContaining(result, "void 'void-1'"));
}

TEST(SceneMesher, VoidSpanningWallCutsBothFaces) {
  // Through-void across the slab thickness: same hole on inner and outer
  // faces as an equivalent WallOpening (offset 2.0, width 1.0, sill 1.0,
  // height 0.8 on the plain 6 x 2.8 x 0.2 wall).
  av::Scene scene;
  scene.entities.push_back(makePlainWall());
  av::Void window;
  window.id = "void-window";
  window.origin = {2.0, -0.3, 1.0};
  window.size = {1.0, 0.4, 0.8};  // y spans [-0.3, 0.1] through the slab
  scene.entities.push_back(window);

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  // Grid 3x3 minus one hole cell -> 8 quads per big face (16 tris each),
  // plus 4 caps (8 tris) = 40. entityTriangleCount stays per geometry
  // entity (mesh-report format) — the void adds no triangles of its own.
  EXPECT_EQ(result.mesh.triangles.size(), 40u);
  ASSERT_EQ(result.entityTriangleCount.size(), 1u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-plain"), 40u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-inner"), 16u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-outer"), 16u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-side"), 8u);

  // Full slab 37.12 minus the 1.0 x 0.8 hole on each big face.
  EXPECT_NEAR(av::surfaceArea(result.mesh), 37.12 - 2.0 * 0.8, 1e-9);
  EXPECT_TRUE(hasWarningContaining(result, "void 'void-window'"));
  EXPECT_TRUE(hasWarningContaining(result, "as a simple hole"));
}

TEST(SceneMesher, VoidNicheCutsInnerFaceOnly) {
  // Void reaching the inner face but not the outer one (a niche): only the
  // inner face is split.
  av::Scene scene;
  scene.entities.push_back(makePlainWall());
  av::Void niche;
  niche.id = "void-niche";
  niche.origin = {2.0, -0.1, 1.0};
  niche.size = {1.0, 0.15, 0.8};  // y in [-0.1, 0.05]: reaches inner (y=0)
  scene.entities.push_back(niche);

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  // Inner: 8 quads = 16 tris (one cell removed). Outer: uncut 2 tris.
  // Caps 8 tris. Total 26.
  EXPECT_EQ(result.mesh.triangles.size(), 26u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-plain"), 26u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-inner"), 16u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-outer"), 2u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-side"), 8u);

  // Only the inner face loses the 1.0 x 0.8 hole.
  EXPECT_NEAR(av::surfaceArea(result.mesh), 37.12 - 0.8, 1e-9);
  EXPECT_TRUE(hasWarningContaining(result, "as a simple hole"));
}

TEST(SceneMesher, VoidTouchingWallFromOutsideDoesNotCut) {
  // Zero-volume contact (void flush with the inner face, extending into the
  // room) subtracts nothing — wall stays a plain 12-triangle slab.
  av::Scene scene;
  scene.entities.push_back(makePlainWall());
  av::Void touch = makeVoid();
  touch.origin = {1.0, 0.0, 1.0};
  touch.size = {1.0, 0.5, 0.8};  // y in [0, 0.5], slab is y in [-0.2, 0]
  scene.entities.push_back(touch);

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  EXPECT_EQ(result.mesh.triangles.size(), 12u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-plain"), 12u);
  EXPECT_NEAR(av::surfaceArea(result.mesh), 37.12, 1e-9);
  // Exactly one note: the v1 limitation (no wall cut happened).
  ASSERT_EQ(result.warnings.size(), 1u);
  EXPECT_TRUE(hasWarningContaining(result, "void 'void-1'"));
  EXPECT_TRUE(hasWarningContaining(result, "limitation"));
}

TEST(SceneMesher, EmptyFaceMaterialIdsFallBackToFaceKinds) {
  av::Scene scene;
  av::Wall wall = makePlainWall();
  wall.id = "wall-x";
  wall.faces.inner.clear();
  wall.faces.side.clear();
  scene.entities.push_back(wall);

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);
  ASSERT_EQ(result.mesh.materialSlots.size(), 3u);
  EXPECT_EQ(result.mesh.materialSlots[0], "wall-x-inner");
  EXPECT_EQ(result.mesh.materialSlots[1], "mat-outer");
  EXPECT_EQ(result.mesh.materialSlots[2], "wall-x-side");
}

TEST(SceneMesher, FixtureSceneJsonGeneratesMesh) {
  const std::filesystem::path path =
      std::filesystem::path(AV_FIXTURES_DIR) / "scene.json";
  const av::Scene scene = av::loadScene(path);
  EXPECT_TRUE(av::validateScene(scene).empty());

  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);

  // wall-north: door hole (both faces) + through-window void hole (both
  // faces). Grid s {0,1,1.9,3.5,4.5,6} x z {0,1,1.8,2.1,2.8} = 20 cells
  // minus 4 hole cells -> 16 quads per big face = 32 tris each + 8 caps
  // = 72. block-1: 12. Total 84.
  EXPECT_EQ(result.mesh.triangles.size(), 84u);
  ASSERT_EQ(result.entityTriangleCount.size(), 2u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-north"), 72u);
  EXPECT_EQ(result.entityTriangleCount.at("block-1"), 12u);
  EXPECT_EQ(result.entityTriangleCount.count("void-window"), 0u);

  // Slots are material ids (faces inner=mat-brick, outer=mat-glass,
  // side=mat-plaster) — first triangle is the inner face of wall-north.
  ASSERT_EQ(result.mesh.materialSlots.size(), 3u);
  EXPECT_EQ(result.mesh.materialSlots[0], "mat-brick");
  EXPECT_EQ(result.mesh.materialSlots[1], "mat-glass");
  EXPECT_EQ(result.mesh.materialSlots[2], "mat-plaster");
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-brick"),
            32u + 12u);  // wall inner + whole block
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-glass"), 32u);
  EXPECT_EQ(trianglesWithSlot(result.mesh, "mat-plaster"), 8u);

  // Wall 37.12 - 2*(0.9*2.1 door) - 2*(1.0*0.8 window) = 31.74,
  // block 2*(0.5*1.2 + 0.5*0.5 + 1.2*0.5) = 2.9.
  EXPECT_NEAR(av::surfaceArea(result.mesh), 31.74 + 2.9, 1e-9);

  // Door note + void cut note + void limitation note.
  ASSERT_EQ(result.warnings.size(), 3u);
  EXPECT_TRUE(hasWarningContaining(result, "wall 'wall-north' openings[0]"));
  EXPECT_TRUE(hasWarningContaining(result, "void 'void-window'"));
  EXPECT_TRUE(hasWarningContaining(result, "as a simple hole"));
  EXPECT_TRUE(hasWarningContaining(result, "limitation"));
}

TEST(SceneMesher, FixtureSceneMesh) {
  const av::Scene scene = makeScene();
  const av::SurfaceMeshResult result =
      av::generateSurfaceMesh(scene, scene.meshSettings);

  // wall+door 28 + box 12 + L-extrude 20 = 60.
  EXPECT_EQ(result.mesh.triangles.size(), 60u);
  ASSERT_EQ(result.entityTriangleCount.size(), 3u);
  EXPECT_EQ(result.entityTriangleCount.at("wall-door"), 28u);
  EXPECT_EQ(result.entityTriangleCount.at("block-1"), 12u);
  EXPECT_EQ(result.entityTriangleCount.at("l-wall"), 20u);

  // Door simple-hole note + void not-subtracted note.
  EXPECT_EQ(result.warnings.size(), 2u);
  EXPECT_TRUE(hasWarningContaining(result, "wall 'wall-door' openings[0]"));
  EXPECT_TRUE(hasWarningContaining(result, "void 'void-1'"));

  EXPECT_EQ(av::validateScene(scene).size(), 0u);
}

TEST(SceneMesher, AdvancingFrontFallsBackWithWarning) {
  av::Scene scene;
  scene.entities.push_back(makePlainWall());
  av::MeshSettings settings = scene.meshSettings;
  settings.surface.algorithm = av::MeshSettings::SurfaceAlgorithm::AdvancingFront;

  const av::SurfaceMeshResult result = av::generateSurfaceMesh(scene, settings);
  EXPECT_EQ(result.mesh.triangles.size(), 12u);
  EXPECT_TRUE(hasWarningContaining(result, "advancing-front"));
}

TEST(SceneMesher, VolumeStubHexBoxEmitsOneCellPerSolidBox) {
  av::Scene scene;
  scene.entities.push_back(makeSolidBox());
  av::MeshSettings settings = scene.meshSettings;
  settings.volume.algorithm = av::MeshSettings::VolumeAlgorithm::HexBox;

  const av::VolumeMeshResult result = av::generateVolumeMesh(scene, settings);
  EXPECT_EQ(result.nodes.size(), 8u);
  ASSERT_EQ(result.cells.size(), 1u);
  ASSERT_EQ(result.zones.size(), 1u);
  EXPECT_EQ(result.zones[0], "mat-brick");
  EXPECT_EQ(result.cells[0][0], 0u);
  EXPECT_EQ(result.cells[0][7], 7u);
  EXPECT_TRUE(hasVolumeWarningContaining(result, "v1 stub"));
}

TEST(SceneMesher, VolumeStubTetAndNonBoxGeometryWarn) {
  av::Scene scene = makeScene();
  av::MeshSettings settings = scene.meshSettings;
  settings.volume.algorithm = av::MeshSettings::VolumeAlgorithm::Tet;

  const av::VolumeMeshResult tet = av::generateVolumeMesh(scene, settings);
  EXPECT_TRUE(tet.cells.empty());
  EXPECT_TRUE(hasVolumeWarningContaining(tet, "tet is not implemented"));

  settings.volume.algorithm = av::MeshSettings::VolumeAlgorithm::HexBox;
  const av::VolumeMeshResult hexes = av::generateVolumeMesh(scene, settings);
  // Only the SolidBox produces a cell; wall / extrude / void warn and skip.
  EXPECT_EQ(hexes.cells.size(), 1u);
  EXPECT_TRUE(hasVolumeWarningContaining(hexes, "wall 'wall-door'"));
  EXPECT_TRUE(hasVolumeWarningContaining(hexes, "solid_extrude 'l-wall'"));
  EXPECT_TRUE(hasVolumeWarningContaining(hexes, "void 'void-1'"));
}
