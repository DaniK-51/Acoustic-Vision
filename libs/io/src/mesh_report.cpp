#include <av/io/io.hpp>

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "json_common.hpp"

namespace av {
namespace {

std::string surfaceAlgorithmToString(MeshSettings::SurfaceAlgorithm algorithm) {
  switch (algorithm) {
    case MeshSettings::SurfaceAlgorithm::Earclip:
      return "earclip";
    case MeshSettings::SurfaceAlgorithm::AdvancingFront:
      return "advancing-front";
    case MeshSettings::SurfaceAlgorithm::Auto:
    default:
      return "auto";
  }
}

void writeJsonFile(const nlohmann::json& json,
                   const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot write file: " + path.string());
  }
  output << json.dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("failed writing file: " + path.string());
  }
}

}  // namespace

MeshReport meshReportFrom(const SurfaceMeshResult& result,
                          const ResolvedSurfaceMeshSettings& settingsUsed) {
  MeshReport report;
  report.stats.vertices = result.mesh.positions.size();
  report.stats.triangles = result.mesh.triangles.size();
  report.stats.zones = result.mesh.materialSlots;
  report.bbox = boundsOf(result.mesh);
  report.warnings = result.warnings;
  report.settingsUsed = settingsUsed;
  report.entityTriangleCount = result.entityTriangleCount;
  return report;
}

nlohmann::json meshReportToJson(const MeshReport& report) {
  nlohmann::json stats;
  stats["vertices"] = report.stats.vertices;
  stats["triangles"] = report.stats.triangles;
  stats["zones"] = report.stats.zones;

  nlohmann::json bbox = nlohmann::json::array(
      {detail::vec3ToJson(report.bbox.min), detail::vec3ToJson(report.bbox.max)});

  nlohmann::json settingsUsed;
  settingsUsed["algorithm"] = surfaceAlgorithmToString(report.settingsUsed.algorithm);
  settingsUsed["targetEdge"] = report.settingsUsed.targetEdge;
  settingsUsed["maxEdge"] = report.settingsUsed.maxEdge;

  nlohmann::json entityTriangleCount = nlohmann::json::object();
  for (const auto& [id, count] : report.entityTriangleCount) {
    entityTriangleCount[id] = count;
  }

  nlohmann::json json;
  json["kind"] = report.kind;
  json["stats"] = std::move(stats);
  json["bbox"] = std::move(bbox);
  json["warnings"] = report.warnings;
  json["settingsUsed"] = std::move(settingsUsed);
  json["entityTriangleCount"] = std::move(entityTriangleCount);
  return json;
}

void writeMeshReport(const std::filesystem::path& path,
                     const MeshReport& report) {
  writeJsonFile(meshReportToJson(report), path);
}

}  // namespace av
