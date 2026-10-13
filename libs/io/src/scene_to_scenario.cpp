#include <av/io/io.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace av {
namespace {

// Geometry placement tolerance for the closed-box detection (meters). The
// rect compile is exact by policy; scenes that are "almost" a box fall back
// to the mesh room rather than guessing dimensions.
constexpr double kBoxEps = 1e-9;

[[nodiscard]] bool near(double a, double b) {
  return std::abs(a - b) <= kBoxEps;
}

struct WallSide {
  const Wall* wall = nullptr;
  // Which rect face the inner material belongs to ("x+"|"x-"|"z+"|"z-"),
  // in scenario (Y-up) naming.
  std::string face;
};

// Recognizes a closed shoebox shell: 4 axis-aligned walls (equal height,
// baselines on z=0 covering the four sides of one rectangle, no openings) and
// floor/ceiling SolidBox slabs exactly over that rectangle. See
// scenarioFromScene for the full contract.
struct ClosedBox {
  double w = 0.0;
  double h = 0.0;
  double d = 0.0;
  double minX = 0.0;
  double minY = 0.0;
  std::map<std::string, std::string> faces;
};

[[nodiscard]] std::optional<ClosedBox> detectClosedBox(const Scene& scene) {
  std::vector<const Wall*> walls;
  std::vector<const SolidBox*> boxes;
  for (const SceneEntity& entity : scene.entities) {
    if (const auto* wall = std::get_if<Wall>(&entity)) {
      walls.push_back(wall);
    } else if (const auto* box = std::get_if<SolidBox>(&entity)) {
      boxes.push_back(box);
    } else if (std::holds_alternative<SourceRef>(entity) ||
               std::holds_alternative<ReceiverRef>(entity)) {
      // Placements do not take part in the shell test.
    } else {
      return std::nullopt;  // void / extrude: not a plain shoebox
    }
  }
  if (walls.size() != 4 || boxes.size() != 2) {
    return std::nullopt;
  }

  // Wall baselines: horizontal at z=0, axis-aligned, equal height, no openings.
  double minX = 0.0;
  double maxX = 0.0;
  double minY = 0.0;
  double maxY = 0.0;
  double height = 0.0;
  for (std::size_t i = 0; i < walls.size(); ++i) {
    const Wall& wall = *walls[i];
    if (!wall.openings.empty() || !(wall.height > 0.0)) {
      return std::nullopt;
    }
    if (!near(wall.start.z, 0.0) || !near(wall.end.z, 0.0)) {
      return std::nullopt;
    }
    const bool degenerate =
        near(wall.start.x, wall.end.x) && near(wall.start.y, wall.end.y);
    const bool axisAligned =
        near(wall.start.x, wall.end.x) || near(wall.start.y, wall.end.y);
    if (!axisAligned || degenerate) {
      return std::nullopt;
    }
    if (i == 0) {
      height = wall.height;
      minX = std::min(wall.start.x, wall.end.x);
      maxX = std::max(wall.start.x, wall.end.x);
      minY = std::min(wall.start.y, wall.end.y);
      maxY = std::max(wall.start.y, wall.end.y);
    } else {
      if (!near(wall.height, height)) {
        return std::nullopt;
      }
      minX = std::min(minX, std::min(wall.start.x, wall.end.x));
      maxX = std::max(maxX, std::max(wall.start.x, wall.end.x));
      minY = std::min(minY, std::min(wall.start.y, wall.end.y));
      maxY = std::max(maxY, std::max(wall.start.y, wall.end.y));
    }
  }
  const double w = maxX - minX;
  const double d = maxY - minY;
  if (!(w > 0.0) || !(d > 0.0) || !(height > 0.0)) {
    return std::nullopt;
  }

  // Each wall must cover exactly one full side of the rectangle, one per side.
  std::optional<WallSide> south;
  std::optional<WallSide> north;
  std::optional<WallSide> west;
  std::optional<WallSide> east;
  for (const Wall* wall : walls) {
    const double x0 = std::min(wall->start.x, wall->end.x);
    const double x1 = std::max(wall->start.x, wall->end.x);
    const double y0 = std::min(wall->start.y, wall->end.y);
    const double y1 = std::max(wall->start.y, wall->end.y);
    if (near(x0, minX) && near(x1, maxX) && near(y0, minY) && near(y1, minY)) {
      if (south.has_value()) return std::nullopt;
      south = WallSide{wall, "z-"};  // scene y=minY -> scenario z=0
    } else if (near(x0, minX) && near(x1, maxX) && near(y0, maxY) &&
               near(y1, maxY)) {
      if (north.has_value()) return std::nullopt;
      north = WallSide{wall, "z+"};
    } else if (near(x0, minX) && near(x1, minX) && near(y0, minY) &&
               near(y1, maxY)) {
      if (west.has_value()) return std::nullopt;
      west = WallSide{wall, "x-"};
    } else if (near(x0, maxX) && near(x1, maxX) && near(y0, minY) &&
               near(y1, maxY)) {
      if (east.has_value()) return std::nullopt;
      east = WallSide{wall, "x+"};
    } else {
      return std::nullopt;
    }
  }
  if (!south.has_value() || !north.has_value() || !west.has_value() ||
      !east.has_value()) {
    return std::nullopt;
  }

  // Floor / ceiling slabs: exact XY footprint of the rectangle, touching
  // z=0 and z=height respectively.
  const SolidBox* floor = nullptr;
  const SolidBox* ceiling = nullptr;
  for (const SolidBox* box : boxes) {
    const bool footprint = near(box->origin.x, minX) &&
                           near(box->origin.y, minY) &&
                           near(box->size.x, w) && near(box->size.y, d) &&
                           box->size.z > 0.0;
    if (!footprint) {
      return std::nullopt;
    }
    const bool atFloor = near(box->origin.z, 0.0) &&
                         !near(box->origin.z + box->size.z, height);
    const bool atCeiling = near(box->origin.z + box->size.z, height) &&
                           !near(box->origin.z, 0.0);
    if (atFloor && floor == nullptr) {
      floor = box;
    } else if (atCeiling && ceiling == nullptr) {
      ceiling = box;
    } else {
      return std::nullopt;  // duplicate role or a full-height solid
    }
  }
  if (floor == nullptr || ceiling == nullptr) {
    return std::nullopt;
  }

  ClosedBox out;
  out.w = w;
  out.h = height;
  out.d = d;
  out.minX = minX;
  out.minY = minY;
  out.faces["floor"] = floor->materialId;
  out.faces["ceiling"] = ceiling->materialId;
  out.faces[south->face] = south->wall->faces.inner;
  out.faces[north->face] = north->wall->faces.inner;
  out.faces[west->face] = west->wall->faces.inner;
  out.faces[east->face] = east->wall->faces.inner;
  return out;
}

// Scene editor is Z-up (wall height along +Z); RectRoom engines are Y-up
// (h along +y, d along +z) — see image-source insideRoom. Mesh rooms keep the
// scene Z-up frame (matches generateSurfaceMesh / ray-tracing).
[[nodiscard]] Vec3 toRectPosition(const Vec3& p, const ClosedBox& box) {
  return Vec3{p.x - box.minX, p.z, p.y - box.minY};
}

void copyPlacements(const Scene& scene, Scenario& scenario, bool rect,
                    const ClosedBox& box) {
  for (const SceneEntity& entity : scene.entities) {
    if (const auto* source = std::get_if<SourceRef>(&entity)) {
      Source out;
      out.id = source->id;
      out.position = rect ? toRectPosition(source->position, box)
                          : source->position;
      out.powerDb = source->powerDb;
      scenario.sources.push_back(std::move(out));
    } else if (const auto* receiver = std::get_if<ReceiverRef>(&entity)) {
      Receiver out;
      out.id = receiver->id;
      out.position = rect ? toRectPosition(receiver->position, box)
                          : receiver->position;
      scenario.receivers.push_back(std::move(out));
    }
  }
}

}  // namespace

Scenario scenarioFromScene(const Scene& scene,
                           const std::filesystem::path& meshPath) {
  Scenario scenario;
  scenario.schemaVersion = 1;
  scenario.name = scene.id;
  scenario.materials = scene.materials;
  scenario.params.sampleRate = 48000;
  scenario.params.durationSec = 1.0;
  scenario.params.bandsHz.assign(kDefaultBandsHz.begin(),
                                 kDefaultBandsHz.end());
  scenario.params.speedOfSound = kSpeedOfSound;

  const std::optional<ClosedBox> box = detectClosedBox(scene);
  if (box.has_value()) {
    scenario.room.type = Room::Type::Rect;
    scenario.room.rect.w = box->w;
    scenario.room.rect.h = box->h;
    scenario.room.rect.d = box->d;
    scenario.room.rect.faces = box->faces;
    copyPlacements(scene, scenario, true, *box);
    scenario.params.engine = "image-source";
    scenario.params.maxReflectionOrder = 10;
    return scenario;
  }

  if (meshPath.empty()) {
    throw std::runtime_error(
        "scene is not a closed box; scenarioFromScene needs a meshPath to "
        "write the surface mesh");
  }
  const SurfaceMeshResult result = generateSurfaceMesh(scene, scene.meshSettings);
  saveObj(meshPath, result.mesh);

  scenario.room.type = Room::Type::Mesh;
  scenario.room.mesh.uri = meshPath.generic_string();
  scenario.room.mesh.unit = "m";
  scenario.room.mesh.scale = 1.0;
  // Mesh slots already carry scene material ids, so no overrides are needed.
  copyPlacements(scene, scenario, false, ClosedBox{});
  scenario.params.engine = "ray-tracing";
  return scenario;
}

}  // namespace av
