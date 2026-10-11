#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/geometry/mesh.hpp>
#include <av/geometry/vec3.hpp>
#include <av/materials/material.hpp>
#include <av/units.hpp>

namespace av {

struct RectRoom {
  double w = 0.0;
  double h = 0.0;
  double d = 0.0;
  // Face name ("floor"|"ceiling"|"x+"|"x-"|"z+"|"z-") -> material id.
  std::map<std::string, std::string> faces;
};

struct MeshRoom {
  std::string uri;
  std::string unit = "m";  // "m"|"mm"|"cm"|"in"
  double scale = 1.0;
  std::map<std::string, std::string> materialOverrides;
};

struct Room {
  enum class Type { Rect, Mesh };

  Type type = Type::Rect;
  RectRoom rect;
  MeshRoom mesh;
};

struct Source {
  std::string id;
  Vec3 position;
  double powerDb = 0.0;
};

struct Receiver {
  std::string id;
  Vec3 position;
};

struct SimParams {
  std::string engine;
  int maxReflectionOrder = 0;
  int sampleRate = 48000;
  double durationSec = 1.0;
  std::vector<double> bandsHz;
  double speedOfSound = kSpeedOfSound;
  bool capturePaths = false;
  bool airAbsorption = false;
  bool signedIr = false;
  nlohmann::json methodParams = nlohmann::json::object();
};

struct Scenario {
  int schemaVersion = 1;
  std::string name;
  Room room;
  std::vector<Material> materials;
  std::vector<Source> sources;
  std::vector<Receiver> receivers;
  SimParams params;
  // Runtime-only mesh for mesh rooms: the caller loads room.mesh.uri and hands
  // the geometry to engines here. Not serialized to scenario JSON (the uri
  // stays the on-disk reference); filled by JobManager/CLI/API in P2c.
  std::shared_ptr<const Mesh> loadedMesh;
};

struct RectFace {
  std::string name;
  std::string materialId;
};

// Six faces in fixed order: floor, ceiling, x+, x-, z+, z-.
[[nodiscard]] std::vector<RectFace> rectFaces(const RectRoom& room);

[[nodiscard]] const Material* findMaterial(const Scenario& scenario,
                                           std::string_view id);
[[nodiscard]] Material* findMaterial(Scenario& scenario, std::string_view id);

}  // namespace av
