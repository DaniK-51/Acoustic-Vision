#include <gtest/gtest.h>

#include <av/scene/scene_mesher.hpp>
#include <av/scene/scene_model.hpp>

#include <algorithm>
#include <cmath>
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
