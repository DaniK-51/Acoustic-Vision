#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <av/geometry/vec3.hpp>
#include <av/materials/material.hpp>

namespace av {

// Scene material reuses Scenario Material (id, name, bandsHz, absorption,
// scattering) so to-scenario compile copies them as-is.
using SceneMaterial = Material;

// 2D point in the XY plane (SolidExtrude polygon).
struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

struct WallOpening {
  enum class Type { Door, Window };

  Type type = Type::Door;
  // Offset along the start->end baseline, meters from `start`.
  double offset = 0.0;
  double width = 0.0;
  double height = 0.0;
  // Height of the opening bottom above the wall base, meters.
  double sill = 0.0;
};

// Wall / Slab: baseline segment + height, extruded by `thickness`.
struct Wall {
  std::string id;
  std::string name;
  Vec3 start{};
  Vec3 end{};
  double height = 0.0;
  double thickness = 0.0;
  struct Faces {
    std::string inner;
    std::string outer;
    std::string side;
  } faces;
  std::vector<WallOpening> openings;
};

struct SolidBox {
  std::string id;
  std::string name;
  Vec3 origin{};
  Vec3 size{};
  std::string materialId;
};

// Polygon in XY extruded along +Z by `height`.
struct SolidExtrude {
  std::string id;
  std::string name;
  std::vector<Vec2> polygon;
  double height = 0.0;
  std::string materialId;
};

// Subtractive volume (opening / niche) for mesh generation.
struct Void {
  std::string id;
  std::string name;
  Vec3 origin{};
  Vec3 size{};
};

// Source / receiver placement entities. Kept as distinct variants so a scene
// graph can hold them alongside geometry; JSON puts them into top-level
// `sources` / `receivers` arrays (docs/room-editor-mesh.md sketch).
struct SourceRef {
  std::string id;
  std::string name;
  Vec3 position{};
  double powerDb = 0.0;
};

struct ReceiverRef {
  std::string id;
  std::string name;
  Vec3 position{};
};

using SceneEntity = std::variant<Wall, SolidBox, SolidExtrude, Void, SourceRef,
                                 ReceiverRef>;

struct MeshSettings {
  enum class SurfaceAlgorithm { Auto, Earclip, AdvancingFront };
  enum class Quality { Draft, Standard, Fine };
  enum class VolumeAlgorithm { Tet, HexBox };

  struct Surface {
    SurfaceAlgorithm algorithm = SurfaceAlgorithm::Auto;
    // Optional custom overrides (schemas/scene.schema.json). Unset fields are
    // filled from the `quality` preset by resolveSurfaceMeshSettings; a value
    // that is set wins over the preset for that field.
    std::optional<double> targetEdge;
    std::optional<double> maxEdge;
    Quality quality = Quality::Draft;
  };

  struct Volume {
    VolumeAlgorithm algorithm = VolumeAlgorithm::Tet;
    double targetSize = 0.15;
    bool enabled = false;
  };

  Surface surface;
  Volume volume;
};

// Concrete surface parameters a mesher should use (docs/room-editor-mesh.md
// §3.2, slice R8).
struct ResolvedSurfaceMeshSettings {
  double targetEdge = 0.5;
  double maxEdge = 1.0;
  MeshSettings::SurfaceAlgorithm algorithm = MeshSettings::SurfaceAlgorithm::Auto;
};

// Expands `meshSettings.surface.quality` into (targetEdge, maxEdge) and
// carries `algorithm` through. Preset table (docs/room-editor-mesh.md §3.2):
//   draft    -> targetEdge 0.5,  maxEdge 1.0
//   standard -> targetEdge 0.2,  maxEdge 0.5
//   fine     -> targetEdge 0.08, maxEdge 0.2
// An explicitly set targetEdge / maxEdge in MeshSettings::Surface overrides
// the preset for that field; the preset fills the rest.
[[nodiscard]] ResolvedSurfaceMeshSettings resolveSurfaceMeshSettings(
    const MeshSettings& settings);

// Scene document (schemas/scene.schema.json). Units are meters only.
struct Scene {
  int schemaVersion = 1;
  std::string id;
  std::string units = "m";
  std::vector<SceneMaterial> materials;
  std::vector<SceneEntity> entities;
  MeshSettings meshSettings;
};

// Entity discriminator as used in scene JSON ("wall", "solid_box",
// "solid_extrude", "void", "source", "receiver").
[[nodiscard]] std::string_view entityType(const SceneEntity& entity);
[[nodiscard]] const std::string& entityId(const SceneEntity& entity);

// Semantic checks: positive sizes, unique entity ids, materialId refs resolve,
// openings fit their wall. Empty vector = valid. Structural JSON errors are
// thrown by av_io::sceneFromJson instead.
[[nodiscard]] std::vector<std::string> validateScene(const Scene& scene);

}  // namespace av
