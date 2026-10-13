#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <av/geometry/mesh.hpp>
#include <av/scene/scene_model.hpp>

namespace av {

// Output of generateSurfaceMesh (slice R7: Scene entities -> SurfaceMesh).
struct SurfaceMeshResult {
  // Triangle mesh over all geometry entities. materialSlots hold *material
  // ids* (faces.inner / faces.outer / faces.side / materialId), so RT can
  // resolve them through materialIdForSlot without a zone table.
  Mesh mesh;
  // entity id -> triangles contributed by that entity (picking / mesh-report).
  // Entities that produce no geometry are absent.
  std::map<std::string, std::size_t> entityTriangleCount;
  // Non-fatal generation notes (skipped voids, simple holes, fallbacks).
  std::vector<std::string> warnings;
};

// Scene -> surface mesh (docs/room-editor-mesh.md §3.2, slice R7).
//
// Geometry (v1: explicit quads -> triangles; no CSG, no edge refinement):
// - Wall: slab along start->end, height along +Z, thickness extruded to the
//   *right* of start->end (viewed from +Z). start/end is the inner-face
//   bottom edge: the inner face lies on the baseline and the body extends
//   outward, so a CCW wall loop (viewed from +Z) bounds the room at exactly
//   the baseline dimensions. Without openings the slab is 6 quads / 12
//   triangles. Openings are simple rectangular holes: the inner and outer
//   faces are split into quads around each hole (grid split, robust for
//   several openings). Reveal/jamb faces are not generated and the four slab
//   caps are not cut, so a sill=0 door leaves the bottom cap running under
//   the hole — each opening emits a warning.
// - SolidBox: 8 vertices / 12 triangles, all faces use materialId.
// - SolidExtrude: prism over the XY polygon (base z=0, height along +Z);
//   side quads + ear-clipped caps. CW polygons are reversed to CCW first.
// - Void: no geometry, one warning — wall openings live inline on walls.
// - Source / Receiver: no geometry.
//
// Winding / normals (mesh.hpp "into the room / toward the air" where
// possible): every face is CCW seen from the air side, so faceNormal() points
// out of the solid material and toward the adjacent air:
// - wall inner face  -> into the room (left of start->end, +Z up);
// - wall outer face  -> away from the room (right of start->end);
// - wall caps        -> -Z / +Z / along -dir / +dir (out of the slab);
// - solid box / extrude -> outward on every face (bottom cap -Z, top +Z).
// A room shell assembled from CCW wall loops therefore has its inner faces
// wound into the room air, matching generateBoxMesh floors.
//
// `settings` selects the surface algorithm; targetEdge / maxEdge / quality
// are not applied in v1 (explicit topology only) and AdvancingFront falls
// back with a warning. Degenerate entities are skipped with warnings, never
// thrown — run validateScene() first for strict checking.
[[nodiscard]] SurfaceMeshResult generateSurfaceMesh(const Scene& scene,
                                                    const MeshSettings& settings);

// Output of generateVolumeMesh (slice R8 stub).
struct VolumeMeshResult {
  std::vector<Vec3> nodes;
  // Hex cells (8 node indices each); empty in the v1 stub except one cell
  // per SolidBox under algorithm hex-box.
  std::vector<std::array<std::uint32_t, 8>> cells;
  // Zone (material id) per cell; same length as `cells`.
  std::vector<std::string> zones;
  std::vector<std::string> warnings;
};

// Volume mesh stub (docs/room-editor-mesh.md §3.3, slice R8) — not a real
// volume mesher. Algorithm hex-box emits one hex cell per SolidBox (8 corner
// nodes, zone = materialId); other geometry entities are skipped with a
// warning and `targetSize` is not applied. Algorithm tet produces no cells
// and warns. Callers decide when to run this via meshSettings.volume.enabled.
[[nodiscard]] VolumeMeshResult generateVolumeMesh(const Scene& scene,
                                                  const MeshSettings& settings);

}  // namespace av
