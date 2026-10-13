#include <av/io/io.hpp>

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "json_common.hpp"

namespace av {
namespace {

using detail::vec3FromJson;
using detail::vec3ToJson;

Vec2 vec2FromJson(const nlohmann::json& json) {
  if (!json.is_array() || json.size() != 2) {
    throw std::runtime_error("expected a [x, y] array");
  }
  return Vec2{json.at(0).get<double>(), json.at(1).get<double>()};
}

nlohmann::json vec2ToJson(const Vec2& v) {
  return nlohmann::json::array({v.x, v.y});
}

nlohmann::json materialToJson(const Material& material) {
  nlohmann::json json;
  json["id"] = material.id;
  if (!material.name.empty()) {
    json["name"] = material.name;
  }
  if (!material.bandsHz.empty()) {
    json["bandsHz"] = material.bandsHz;
  }
  json["absorption"] = material.absorption;
  json["scattering"] = material.scattering;
  return json;
}

Material materialFromJson(const nlohmann::json& json) {
  Material material;
  material.id = json.at("id").get<std::string>();
  material.name = json.value("name", std::string{});
  material.bandsHz = json.value("bandsHz", std::vector<double>{});
  material.absorption = json.at("absorption").get<std::vector<double>>();
  material.scattering = json.value("scattering", 0.0);
  return material;
}

nlohmann::json openingToJson(const WallOpening& opening) {
  nlohmann::json json;
  json["type"] = opening.type == WallOpening::Type::Door ? "door" : "window";
  json["offset"] = opening.offset;
  json["width"] = opening.width;
  json["height"] = opening.height;
  json["sill"] = opening.sill;
  return json;
}

WallOpening openingFromJson(const nlohmann::json& json) {
  WallOpening opening;
  const std::string type = json.at("type").get<std::string>();
  if (type == "door") {
    opening.type = WallOpening::Type::Door;
  } else if (type == "window") {
    opening.type = WallOpening::Type::Window;
  } else {
    throw std::runtime_error("opening.type must be 'door' or 'window'");
  }
  opening.offset = json.at("offset").get<double>();
  opening.width = json.at("width").get<double>();
  opening.height = json.at("height").get<double>();
  opening.sill = json.value("sill", 0.0);
  return opening;
}

nlohmann::json wallToJson(const Wall& wall) {
  nlohmann::json json;
  json["type"] = "wall";
  json["id"] = wall.id;
  if (!wall.name.empty()) {
    json["name"] = wall.name;
  }
  json["start"] = vec3ToJson(wall.start);
  json["end"] = vec3ToJson(wall.end);
  json["height"] = wall.height;
  json["thickness"] = wall.thickness;
  nlohmann::json faces;
  faces["inner"] = wall.faces.inner;
  faces["outer"] = wall.faces.outer;
  faces["side"] = wall.faces.side;
  json["faces"] = std::move(faces);
  nlohmann::json openings = nlohmann::json::array();
  for (const WallOpening& opening : wall.openings) {
    openings.push_back(openingToJson(opening));
  }
  if (!openings.empty()) {
    json["openings"] = std::move(openings);
  }
  return json;
}

Wall wallFromJson(const nlohmann::json& json) {
  Wall wall;
  wall.id = json.at("id").get<std::string>();
  wall.name = json.value("name", std::string{});
  wall.start = vec3FromJson(json.at("start"));
  wall.end = vec3FromJson(json.at("end"));
  wall.height = json.at("height").get<double>();
  wall.thickness = json.at("thickness").get<double>();
  const nlohmann::json& faces = json.at("faces");
  wall.faces.inner = faces.at("inner").get<std::string>();
  wall.faces.outer = faces.at("outer").get<std::string>();
  wall.faces.side = faces.at("side").get<std::string>();
  if (json.contains("openings")) {
    for (const nlohmann::json& opening : json.at("openings")) {
      wall.openings.push_back(openingFromJson(opening));
    }
  }
  return wall;
}

nlohmann::json solidBoxToJson(const SolidBox& box) {
  nlohmann::json json;
  json["type"] = "solid_box";
  json["id"] = box.id;
  if (!box.name.empty()) {
    json["name"] = box.name;
  }
  json["origin"] = vec3ToJson(box.origin);
  json["size"] = vec3ToJson(box.size);
  json["materialId"] = box.materialId;
  return json;
}

SolidBox solidBoxFromJson(const nlohmann::json& json) {
  SolidBox box;
  box.id = json.at("id").get<std::string>();
  box.name = json.value("name", std::string{});
  box.origin = vec3FromJson(json.at("origin"));
  box.size = vec3FromJson(json.at("size"));
  box.materialId = json.at("materialId").get<std::string>();
  return box;
}

nlohmann::json solidExtrudeToJson(const SolidExtrude& extrude) {
  nlohmann::json json;
  json["type"] = "solid_extrude";
  json["id"] = extrude.id;
  if (!extrude.name.empty()) {
    json["name"] = extrude.name;
  }
  nlohmann::json polygon = nlohmann::json::array();
  for (const Vec2& point : extrude.polygon) {
    polygon.push_back(vec2ToJson(point));
  }
  json["polygon"] = std::move(polygon);
  json["height"] = extrude.height;
  json["materialId"] = extrude.materialId;
  return json;
}

SolidExtrude solidExtrudeFromJson(const nlohmann::json& json) {
  SolidExtrude extrude;
  extrude.id = json.at("id").get<std::string>();
  extrude.name = json.value("name", std::string{});
  extrude.polygon.reserve(json.at("polygon").size());
  for (const nlohmann::json& point : json.at("polygon")) {
    extrude.polygon.push_back(vec2FromJson(point));
  }
  extrude.height = json.at("height").get<double>();
  extrude.materialId = json.at("materialId").get<std::string>();
  return extrude;
}

nlohmann::json voidToJson(const Void& voidEntity) {
  nlohmann::json json;
  json["type"] = "void";
  json["id"] = voidEntity.id;
  if (!voidEntity.name.empty()) {
    json["name"] = voidEntity.name;
  }
  json["origin"] = vec3ToJson(voidEntity.origin);
  json["size"] = vec3ToJson(voidEntity.size);
  return json;
}

Void voidFromJson(const nlohmann::json& json) {
  Void voidEntity;
  voidEntity.id = json.at("id").get<std::string>();
  voidEntity.name = json.value("name", std::string{});
  voidEntity.origin = vec3FromJson(json.at("origin"));
  voidEntity.size = vec3FromJson(json.at("size"));
  return voidEntity;
}

// Body shared by entities[] entries and top-level sources[].
nlohmann::json sourceBodyToJson(const SourceRef& source) {
  nlohmann::json json;
  json["id"] = source.id;
  if (!source.name.empty()) {
    json["name"] = source.name;
  }
  json["position"] = vec3ToJson(source.position);
  json["powerDb"] = source.powerDb;
  return json;
}

SourceRef sourceFromJson(const nlohmann::json& json) {
  SourceRef source;
  source.id = json.at("id").get<std::string>();
  source.name = json.value("name", std::string{});
  source.position = vec3FromJson(json.at("position"));
  source.powerDb = json.value("powerDb", 0.0);
  return source;
}

nlohmann::json receiverBodyToJson(const ReceiverRef& receiver) {
  nlohmann::json json;
  json["id"] = receiver.id;
  if (!receiver.name.empty()) {
    json["name"] = receiver.name;
  }
  json["position"] = vec3ToJson(receiver.position);
  return json;
}

ReceiverRef receiverFromJson(const nlohmann::json& json) {
  ReceiverRef receiver;
  receiver.id = json.at("id").get<std::string>();
  receiver.name = json.value("name", std::string{});
  receiver.position = vec3FromJson(json.at("position"));
  return receiver;
}

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

MeshSettings::SurfaceAlgorithm surfaceAlgorithmFromJson(
    const std::string& value) {
  if (value == "auto") {
    return MeshSettings::SurfaceAlgorithm::Auto;
  }
  if (value == "earclip") {
    return MeshSettings::SurfaceAlgorithm::Earclip;
  }
  if (value == "advancing-front") {
    return MeshSettings::SurfaceAlgorithm::AdvancingFront;
  }
  throw std::runtime_error(
      "meshSettings.surface.algorithm must be 'auto', 'earclip' or "
      "'advancing-front'");
}

std::string qualityToString(MeshSettings::Quality quality) {
  switch (quality) {
    case MeshSettings::Quality::Standard:
      return "standard";
    case MeshSettings::Quality::Fine:
      return "fine";
    case MeshSettings::Quality::Draft:
    default:
      return "draft";
  }
}

MeshSettings::Quality qualityFromJson(const std::string& value) {
  if (value == "draft") {
    return MeshSettings::Quality::Draft;
  }
  if (value == "standard") {
    return MeshSettings::Quality::Standard;
  }
  if (value == "fine") {
    return MeshSettings::Quality::Fine;
  }
  throw std::runtime_error(
      "meshSettings.surface.quality must be 'draft', 'standard' or 'fine'");
}

std::string volumeAlgorithmToString(MeshSettings::VolumeAlgorithm algorithm) {
  switch (algorithm) {
    case MeshSettings::VolumeAlgorithm::HexBox:
      return "hex-box";
    case MeshSettings::VolumeAlgorithm::Tet:
    default:
      return "tet";
  }
}

MeshSettings::VolumeAlgorithm volumeAlgorithmFromJson(const std::string& value) {
  if (value == "tet") {
    return MeshSettings::VolumeAlgorithm::Tet;
  }
  if (value == "hex-box") {
    return MeshSettings::VolumeAlgorithm::HexBox;
  }
  throw std::runtime_error("meshSettings.volume.algorithm must be 'tet' or 'hex-box'");
}

nlohmann::json meshSettingsToJson(const MeshSettings& settings) {
  nlohmann::json surface;
  surface["algorithm"] = surfaceAlgorithmToString(settings.surface.algorithm);
  surface["targetEdge"] = settings.surface.targetEdge;
  surface["maxEdge"] = settings.surface.maxEdge;
  surface["quality"] = qualityToString(settings.surface.quality);

  nlohmann::json volume;
  volume["algorithm"] = volumeAlgorithmToString(settings.volume.algorithm);
  volume["targetSize"] = settings.volume.targetSize;
  volume["enabled"] = settings.volume.enabled;

  nlohmann::json json;
  json["surface"] = std::move(surface);
  json["volume"] = std::move(volume);
  return json;
}

MeshSettings meshSettingsFromJson(const nlohmann::json& json) {
  MeshSettings settings;
  if (json.contains("surface")) {
    const nlohmann::json& surface = json.at("surface");
    if (surface.contains("algorithm")) {
      settings.surface.algorithm =
          surfaceAlgorithmFromJson(surface.at("algorithm").get<std::string>());
    }
    settings.surface.targetEdge = surface.value("targetEdge", 0.5);
    settings.surface.maxEdge = surface.value("maxEdge", 1.0);
    if (surface.contains("quality")) {
      settings.surface.quality =
          qualityFromJson(surface.at("quality").get<std::string>());
    }
  }
  if (json.contains("volume")) {
    const nlohmann::json& volume = json.at("volume");
    if (volume.contains("algorithm")) {
      settings.volume.algorithm =
          volumeAlgorithmFromJson(volume.at("algorithm").get<std::string>());
    }
    settings.volume.targetSize = volume.value("targetSize", 0.15);
    settings.volume.enabled = volume.value("enabled", false);
  }
  return settings;
}

// Parses one entity object (geometry or typed source/receiver).
SceneEntity entityFromJson(const nlohmann::json& json) {
  if (!json.is_object() || !json.contains("type")) {
    throw std::runtime_error("scene entity must be an object with a 'type'");
  }
  const std::string type = json.at("type").get<std::string>();
  if (type == "wall") {
    return wallFromJson(json);
  }
  if (type == "solid_box") {
    return solidBoxFromJson(json);
  }
  if (type == "solid_extrude") {
    return solidExtrudeFromJson(json);
  }
  if (type == "void") {
    return voidFromJson(json);
  }
  if (type == "source") {
    return sourceFromJson(json);
  }
  if (type == "receiver") {
    return receiverFromJson(json);
  }
  throw std::runtime_error("unknown scene entity type '" + type + "'");
}

}  // namespace

nlohmann::json sceneToJson(const Scene& scene) {
  nlohmann::json json;
  json["schemaVersion"] = scene.schemaVersion;
  if (!scene.id.empty()) {
    json["id"] = scene.id;
  }
  json["units"] = scene.units;

  nlohmann::json materials = nlohmann::json::array();
  for (const Material& material : scene.materials) {
    materials.push_back(materialToJson(material));
  }
  json["materials"] = std::move(materials);

  // Sketch home: geometry in `entities`, S/R placements in top-level arrays.
  nlohmann::json entities = nlohmann::json::array();
  nlohmann::json sources = nlohmann::json::array();
  nlohmann::json receivers = nlohmann::json::array();
  for (const SceneEntity& entity : scene.entities) {
    if (const auto* wall = std::get_if<Wall>(&entity)) {
      entities.push_back(wallToJson(*wall));
    } else if (const auto* box = std::get_if<SolidBox>(&entity)) {
      entities.push_back(solidBoxToJson(*box));
    } else if (const auto* extrude = std::get_if<SolidExtrude>(&entity)) {
      entities.push_back(solidExtrudeToJson(*extrude));
    } else if (const auto* voidEntity = std::get_if<Void>(&entity)) {
      entities.push_back(voidToJson(*voidEntity));
    } else if (const auto* source = std::get_if<SourceRef>(&entity)) {
      sources.push_back(sourceBodyToJson(*source));
    } else if (const auto* receiver = std::get_if<ReceiverRef>(&entity)) {
      receivers.push_back(receiverBodyToJson(*receiver));
    }
  }
  json["entities"] = std::move(entities);
  json["sources"] = std::move(sources);
  json["receivers"] = std::move(receivers);
  json["meshSettings"] = meshSettingsToJson(scene.meshSettings);
  return json;
}

Scene sceneFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("scene JSON must be an object");
  }
  if (!json.contains("units")) {
    throw std::runtime_error("scene is missing 'units'");
  }
  if (!json.contains("materials")) {
    throw std::runtime_error("scene is missing 'materials'");
  }
  if (!json.contains("entities")) {
    throw std::runtime_error("scene is missing 'entities'");
  }

  Scene scene;
  scene.schemaVersion = json.value("schemaVersion", 1);
  scene.id = json.value("id", std::string{});
  scene.units = json.at("units").get<std::string>();
  for (const nlohmann::json& material : json.at("materials")) {
    scene.materials.push_back(materialFromJson(material));
  }
  for (const nlohmann::json& entity : json.at("entities")) {
    scene.entities.push_back(entityFromJson(entity));
  }
  if (json.contains("sources")) {
    for (const nlohmann::json& source : json.at("sources")) {
      scene.entities.push_back(sourceFromJson(source));
    }
  }
  if (json.contains("receivers")) {
    for (const nlohmann::json& receiver : json.at("receivers")) {
      scene.entities.push_back(receiverFromJson(receiver));
    }
  }
  if (json.contains("meshSettings")) {
    scene.meshSettings = meshSettingsFromJson(json.at("meshSettings"));
  }
  return scene;
}

Scene loadScene(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open scene file: " + path.string());
  }
  nlohmann::json json;
  try {
    input >> json;
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error("invalid JSON in " + path.string() + ": " +
                             error.what());
  }
  return sceneFromJson(json);
}

void saveScene(const Scene& scene, const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot write scene file: " + path.string());
  }
  output << sceneToJson(scene).dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("failed writing scene file: " + path.string());
  }
}

}  // namespace av
