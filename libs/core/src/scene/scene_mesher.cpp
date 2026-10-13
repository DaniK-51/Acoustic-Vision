#include <av/scene/scene_mesher.hpp>

#include <av/geometry/mesh_builder.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace av {
namespace {

constexpr double kEps = 1e-9;

std::string quoted(const std::string& id) { return "'" + id + "'"; }

// Counts triangles per entity while forwarding into a shared MeshBuilder.
class EntityMesher {
 public:
  explicit EntityMesher(MeshBuilder& builder) : builder_(builder) {}

  std::uint32_t vertex(const Vec3& position) {
    return builder_.addVertex(position);
  }

  void triangle(std::uint32_t v0, std::uint32_t v1, std::uint32_t v2,
                const std::string& zone) {
    builder_.addTriangle(v0, v1, v2, zone);
    ++count_;
  }

  void quad(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d,
            const std::string& zone) {
    builder_.addQuad(a, b, c, d, zone);
    count_ += 2;
  }

  [[nodiscard]] std::size_t count() const { return count_; }

 private:
  MeshBuilder& builder_;
  std::size_t count_ = 0;
};

// ---------------------------------------------------------------------------
// Wall slab
// ---------------------------------------------------------------------------

struct WallFrame {
  Vec3 origin{};
  Vec3 dir{};   // unit, start -> end
  Vec3 up{0.0, 0.0, 1.0};
  Vec3 right{};  // unit, thickness direction (out of the room for a CCW loop)
  double length = 0.0;
  double height = 0.0;
  double thickness = 0.0;

  [[nodiscard]] Vec3 at(double s, double z, double t) const {
    return origin + dir * s + up * z + right * t;
  }
};

bool makeWallFrame(const Wall& wall, WallFrame& frame, std::string& error) {
  if (!(wall.height > 0.0)) {
    error = "height must be positive";
    return false;
  }
  if (!(wall.thickness > 0.0)) {
    error = "thickness must be positive";
    return false;
  }
  const Vec3 axis = wall.end - wall.start;
  const double baseline = length(axis);
  if (!(baseline > kEps)) {
    error = "start and end must differ";
    return false;
  }
  frame.origin = wall.start;
  frame.dir = axis / baseline;
  frame.length = baseline;
  frame.height = wall.height;
  frame.thickness = wall.thickness;
  const Vec3 rightRaw = cross(frame.dir, frame.up);
  const double rightLen = length(rightRaw);
  if (!(rightLen > kEps)) {
    error = "baseline is parallel to +Z; thickness direction undefined";
    return false;
  }
  frame.right = rightRaw / rightLen;
  return true;
}

bool insideAnyOpening(double s, double z, const std::vector<WallOpening>& openings) {
  for (const WallOpening& opening : openings) {
    if (s > opening.offset - kEps &&
        s < opening.offset + opening.width + kEps && z > opening.sill - kEps &&
        z < opening.sill + opening.height + kEps) {
      return true;
    }
  }
  return false;
}

void addWall(const Wall& wall, EntityMesher& mesher,
             std::vector<std::string>& warnings) {
  WallFrame frame;
  std::string error;
  if (!makeWallFrame(wall, frame, error)) {
    warnings.push_back("wall " + quoted(wall.id) + ": skipped (" + error + ")");
    return;
  }

  std::vector<WallOpening> openings;
  for (std::size_t i = 0; i < wall.openings.size(); ++i) {
    const WallOpening& opening = wall.openings[i];
    if (!(opening.width > 0.0) || !(opening.height > 0.0)) {
      warnings.push_back("wall " + quoted(wall.id) + " openings[" +
                         std::to_string(i) + "]: skipped (non-positive size)");
      continue;
    }
    if (opening.offset + opening.width < -kEps ||
        opening.offset > frame.length + kEps || opening.sill + opening.height < -kEps ||
        opening.sill > frame.height + kEps) {
      warnings.push_back("wall " + quoted(wall.id) + " openings[" +
                         std::to_string(i) + "]: skipped (outside the slab)");
      continue;
    }
    openings.push_back(opening);
    warnings.push_back("wall " + quoted(wall.id) + " openings[" +
                       std::to_string(i) +
                       "]: simple hole without reveal faces (v1)");
  }

  // Grid split of the inner/outer faces around the holes. Breaks strictly
  // inside the face rectangle become grid lines; 0 / L / 0 / H are implicit.
  std::vector<double> sBreaks{0.0, frame.length};
  std::vector<double> zBreaks{0.0, frame.height};
  for (const WallOpening& opening : openings) {
    if (opening.offset > kEps && opening.offset < frame.length - kEps) {
      sBreaks.push_back(opening.offset);
    }
    const double sEnd = opening.offset + opening.width;
    if (sEnd > kEps && sEnd < frame.length - kEps) {
      sBreaks.push_back(sEnd);
    }
    if (opening.sill > kEps && opening.sill < frame.height - kEps) {
      zBreaks.push_back(opening.sill);
    }
    const double zEnd = opening.sill + opening.height;
    if (zEnd > kEps && zEnd < frame.height - kEps) {
      zBreaks.push_back(zEnd);
    }
  }
  std::sort(sBreaks.begin(), sBreaks.end());
  std::sort(zBreaks.begin(), zBreaks.end());
  sBreaks.erase(std::unique(sBreaks.begin(), sBreaks.end(),
                           [](double a, double b) {
                             return std::abs(a - b) <= kEps;
                           }),
                sBreaks.end());
  zBreaks.erase(std::unique(zBreaks.begin(), zBreaks.end(),
                           [](double a, double b) {
                             return std::abs(a - b) <= kEps;
                           }),
                zBreaks.end());

  const std::size_t nS = sBreaks.size();
  const std::size_t nZ = zBreaks.size();
  // Corner table (s-index, z-index, side 0=inner 1=outer) shared by faces
  // and caps so a plain wall is 8 vertices, not 24.
  constexpr std::uint32_t kUnset = 0xFFFFFFFFu;
  std::vector<std::uint32_t> corners(nS * nZ * 2, kUnset);
  const auto corner = [&](std::size_t is, std::size_t iz, int side) {
    const std::size_t index = (is * nZ + iz) * 2 + static_cast<std::size_t>(side);
    if (corners[index] == kUnset) {
      const double t = side == 0 ? 0.0 : frame.thickness;
      corners[index] =
          mesher.vertex(frame.at(sBreaks[is], zBreaks[iz], t));
    }
    return corners[index];
  };

  for (std::size_t iz = 0; iz + 1 < nZ; ++iz) {
    for (std::size_t is = 0; is + 1 < nS; ++is) {
      const double sMid = 0.5 * (sBreaks[is] + sBreaks[is + 1]);
      const double zMid = 0.5 * (zBreaks[iz] + zBreaks[iz + 1]);
      if (insideAnyOpening(sMid, zMid, openings)) {
        continue;
      }
      // Inner face: normal -right (left of start->end) = into the room for a
      // CCW wall loop.
      mesher.quad(corner(is, iz, 0), corner(is, iz + 1, 0),
                  corner(is + 1, iz + 1, 0), corner(is + 1, iz, 0),
                  wall.faces.inner);
      // Outer face: normal +right.
      mesher.quad(corner(is, iz, 1), corner(is + 1, iz, 1),
                  corner(is + 1, iz + 1, 1), corner(is, iz + 1, 1),
                  wall.faces.outer);
    }
  }

  // Slab caps (openings do not cut them in v1 — see header).
  // Bottom: normal -up.
  mesher.quad(corner(0, 0, 0), corner(nS - 1, 0, 0), corner(nS - 1, 0, 1),
              corner(0, 0, 1), wall.faces.side);
  // Top: normal +up.
  mesher.quad(corner(0, nZ - 1, 0), corner(0, nZ - 1, 1),
              corner(nS - 1, nZ - 1, 1), corner(nS - 1, nZ - 1, 0),
              wall.faces.side);
  // Start cap: normal -dir.
  mesher.quad(corner(0, 0, 0), corner(0, 0, 1), corner(0, nZ - 1, 1),
              corner(0, nZ - 1, 0), wall.faces.side);
  // End cap: normal +dir.
  mesher.quad(corner(nS - 1, 0, 0), corner(nS - 1, nZ - 1, 0),
              corner(nS - 1, nZ - 1, 1), corner(nS - 1, 0, 1),
              wall.faces.side);
}

// ---------------------------------------------------------------------------
// SolidBox
// ---------------------------------------------------------------------------

void addSolidBox(const SolidBox& box, EntityMesher& mesher,
                 std::vector<std::string>& warnings) {
  if (!(box.size.x > 0.0) || !(box.size.y > 0.0) || !(box.size.z > 0.0)) {
    warnings.push_back("solid_box " + quoted(box.id) +
                       ": skipped (size must be positive in all axes)");
    return;
  }
  const double x0 = box.origin.x;
  const double y0 = box.origin.y;
  const double z0 = box.origin.z;
  const double x1 = x0 + box.size.x;
  const double y1 = y0 + box.size.y;
  const double z1 = z0 + box.size.z;
  const std::uint32_t v0 = mesher.vertex({x0, y0, z0});
  const std::uint32_t v1 = mesher.vertex({x1, y0, z0});
  const std::uint32_t v2 = mesher.vertex({x1, y1, z0});
  const std::uint32_t v3 = mesher.vertex({x0, y1, z0});
  const std::uint32_t v4 = mesher.vertex({x0, y0, z1});
  const std::uint32_t v5 = mesher.vertex({x1, y0, z1});
  const std::uint32_t v6 = mesher.vertex({x1, y1, z1});
  const std::uint32_t v7 = mesher.vertex({x0, y1, z1});

  // Outward windings (toward the air around the solid).
  mesher.quad(v0, v3, v2, v1, box.materialId);  // -z
  mesher.quad(v4, v5, v6, v7, box.materialId);  // +z
  mesher.quad(v0, v4, v7, v3, box.materialId);  // -x
  mesher.quad(v1, v2, v6, v5, box.materialId);  // +x
  mesher.quad(v0, v1, v5, v4, box.materialId);  // -y
  mesher.quad(v3, v7, v6, v2, box.materialId);  // +y
}

// ---------------------------------------------------------------------------
// SolidExtrude (prism + ear-clipped caps)
// ---------------------------------------------------------------------------

double signedArea(const std::vector<Vec2>& polygon) {
  double sum = 0.0;
  for (std::size_t i = 0; i < polygon.size(); ++i) {
    const Vec2& a = polygon[i];
    const Vec2& b = polygon[(i + 1) % polygon.size()];
    sum += a.x * b.y - b.x * a.y;
  }
  return 0.5 * sum;
}

// Ear clipping for a simple CCW polygon. On failure (self-intersection,
// collinear runs) `ok` is false and a fan from vertex 0 is returned.
std::vector<std::array<int, 3>> earClip(const std::vector<Vec2>& polygon,
                                        bool& ok) {
  ok = true;
  std::vector<std::array<int, 3>> out;
  const int n = static_cast<int>(polygon.size());
  if (n < 3) {
    ok = false;
    return out;
  }

  const auto cross2 = [](const Vec2& o, const Vec2& a, const Vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
  };
  const auto insideTriangle = [&](const Vec2& p, const Vec2& a, const Vec2& b,
                                  const Vec2& c) {
    return cross2(a, b, p) >= -kEps && cross2(b, c, p) >= -kEps &&
           cross2(c, a, p) >= -kEps;
  };

  std::vector<int> ring(n);
  for (int i = 0; i < n; ++i) {
    ring[i] = i;
  }

  int remaining = n;
  int guard = 0;
  const int guardMax = n * n + 16;
  while (remaining > 3) {
    bool clipped = false;
    for (int i = 0; i < remaining; ++i) {
      const int i0 = ring[(i + remaining - 1) % remaining];
      const int i1 = ring[i];
      const int i2 = ring[(i + 1) % remaining];
      const Vec2& a = polygon[static_cast<std::size_t>(i0)];
      const Vec2& b = polygon[static_cast<std::size_t>(i1)];
      const Vec2& c = polygon[static_cast<std::size_t>(i2)];
      if (cross2(a, b, c) <= kEps) {
        continue;  // reflex or collinear corner
      }
      bool blocked = false;
      for (int j = 0; j < remaining; ++j) {
        const int ij = ring[j];
        if (ij == i0 || ij == i1 || ij == i2) {
          continue;
        }
        if (insideTriangle(polygon[static_cast<std::size_t>(ij)], a, b, c)) {
          blocked = true;
          break;
        }
      }
      if (blocked) {
        continue;
      }
      out.push_back({i0, i1, i2});
      ring.erase(ring.begin() + i);
      --remaining;
      clipped = true;
      break;
    }
    if (!clipped || ++guard > guardMax) {
      ok = false;
      break;
    }
  }

  if (ok && remaining == 3) {
    out.push_back({ring[0], ring[1], ring[2]});
    return out;
  }

  ok = false;
  out.clear();
  for (int i = 1; i + 1 < n; ++i) {
    out.push_back({0, i, i + 1});
  }
  return out;
}

void addSolidExtrude(const SolidExtrude& extrude, EntityMesher& mesher,
                     std::vector<std::string>& warnings) {
  if (extrude.polygon.size() < 3) {
    warnings.push_back("solid_extrude " + quoted(extrude.id) +
                       ": skipped (polygon needs at least 3 points)");
    return;
  }
  if (!(extrude.height > 0.0)) {
    warnings.push_back("solid_extrude " + quoted(extrude.id) +
                       ": skipped (height must be positive)");
    return;
  }

  std::vector<Vec2> polygon = extrude.polygon;
  const double area = signedArea(polygon);
  if (std::abs(area) < kEps) {
    warnings.push_back("solid_extrude " + quoted(extrude.id) +
                       ": skipped (degenerate polygon area)");
    return;
  }
  if (area < 0.0) {
    std::reverse(polygon.begin(), polygon.end());
  }

  bool clipped = true;
  const std::vector<std::array<int, 3>> caps = earClip(polygon, clipped);
  if (!clipped) {
    warnings.push_back("solid_extrude " + quoted(extrude.id) +
                       ": ear-clip fallback (fan) used for caps");
  }

  const std::size_t n = polygon.size();
  std::vector<std::uint32_t> bottom(n);
  std::vector<std::uint32_t> top(n);
  for (std::size_t i = 0; i < n; ++i) {
    bottom[i] = mesher.vertex({polygon[i].x, polygon[i].y, 0.0});
    top[i] = mesher.vertex({polygon[i].x, polygon[i].y, extrude.height});
  }
  for (std::size_t i = 0; i < n; ++i) {
    const std::size_t j = (i + 1) % n;
    // CCW base ring -> side normals point out of the prism.
    mesher.quad(bottom[i], bottom[j], top[j], top[i], extrude.materialId);
  }
  for (const std::array<int, 3>& tri : caps) {
    // Ear winding is CCW seen from +Z: top keeps it, bottom reverses it.
    mesher.triangle(bottom[static_cast<std::size_t>(tri[0])],
                    bottom[static_cast<std::size_t>(tri[2])],
                    bottom[static_cast<std::size_t>(tri[1])],
                    extrude.materialId);
    mesher.triangle(top[static_cast<std::size_t>(tri[0])],
                    top[static_cast<std::size_t>(tri[1])],
                    top[static_cast<std::size_t>(tri[2])],
                    extrude.materialId);
  }
}

}  // namespace

SurfaceMeshResult generateSurfaceMesh(const Scene& scene,
                                      const MeshSettings& settings) {
  SurfaceMeshResult result;
  if (settings.surface.algorithm ==
      MeshSettings::SurfaceAlgorithm::AdvancingFront) {
    result.warnings.push_back(
        "meshSettings.surface.algorithm advancing-front is not implemented in "
        "v1; using explicit quads / earclip");
  }

  MeshBuilder builder;
  EntityMesher mesher(builder);
  for (const SceneEntity& entity : scene.entities) {
    const std::string& id = entityId(entity);
    const std::size_t before = mesher.count();
    if (const auto* wall = std::get_if<Wall>(&entity)) {
      addWall(*wall, mesher, result.warnings);
    } else if (const auto* box = std::get_if<SolidBox>(&entity)) {
      addSolidBox(*box, mesher, result.warnings);
    } else if (const auto* extrude = std::get_if<SolidExtrude>(&entity)) {
      addSolidExtrude(*extrude, mesher, result.warnings);
    } else if (std::holds_alternative<Void>(entity)) {
      result.warnings.push_back(
          "void " + quoted(id) +
          ": not subtracted in v1 (openings live inline on walls); mesh unchanged");
      continue;
    } else {
      continue;  // source / receiver: no geometry
    }
    const std::size_t added = mesher.count() - before;
    if (added > 0) {
      result.entityTriangleCount[id] = added;
    }
  }

  result.mesh = builder.build();
  return result;
}

VolumeMeshResult generateVolumeMesh(const Scene& scene,
                                    const MeshSettings& settings) {
  VolumeMeshResult result;
  result.warnings.push_back(
      "volume mesh is a v1 stub (docs/room-editor-mesh.md §3.3); "
      "hex-box emits one cell per solid_box, targetSize is not applied");
  if (settings.volume.algorithm == MeshSettings::VolumeAlgorithm::Tet) {
    result.warnings.push_back(
        "meshSettings.volume.algorithm tet is not implemented in v1; no cells");
    return result;
  }

  for (const SceneEntity& entity : scene.entities) {
    const std::string& id = entityId(entity);
    if (const auto* box = std::get_if<SolidBox>(&entity)) {
      if (!(box->size.x > 0.0) || !(box->size.y > 0.0) ||
          !(box->size.z > 0.0)) {
        result.warnings.push_back("solid_box " + quoted(id) +
                                  ": skipped (size must be positive in all "
                                  "axes)");
        continue;
      }
      const double x0 = box->origin.x;
      const double y0 = box->origin.y;
      const double z0 = box->origin.z;
      const double x1 = x0 + box->size.x;
      const double y1 = y0 + box->size.y;
      const double z1 = z0 + box->size.z;
      // Bottom ring CCW seen from +Z, then the matching top ring: a single
      // trilinear hex with outward-consistent winding for later solvers.
      const std::uint32_t base = static_cast<std::uint32_t>(result.nodes.size());
      result.nodes.push_back({x0, y0, z0});
      result.nodes.push_back({x1, y0, z0});
      result.nodes.push_back({x1, y1, z0});
      result.nodes.push_back({x0, y1, z0});
      result.nodes.push_back({x0, y0, z1});
      result.nodes.push_back({x1, y0, z1});
      result.nodes.push_back({x1, y1, z1});
      result.nodes.push_back({x0, y1, z1});
      result.cells.push_back({base + 0, base + 1, base + 2, base + 3, base + 4,
                              base + 5, base + 6, base + 7});
      result.zones.push_back(box->materialId);
    } else if (const auto* wall = std::get_if<Wall>(&entity)) {
      result.warnings.push_back("wall " + quoted(wall->id) +
                                ": no volume cells in the v1 stub");
    } else if (const auto* extrude = std::get_if<SolidExtrude>(&entity)) {
      result.warnings.push_back("solid_extrude " + quoted(extrude->id) +
                                ": no volume cells in the v1 stub");
    } else if (std::holds_alternative<Void>(entity)) {
      result.warnings.push_back("void " + quoted(id) +
                                ": no volume cells in the v1 stub");
    }
  }
  return result;
}

}  // namespace av
