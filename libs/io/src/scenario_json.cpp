#include <av/io/io.hpp>

#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace av {
namespace {

nlohmann::json vec3ToJson(const Vec3& v) {
  return nlohmann::json::array({v.x, v.y, v.z});
}

Vec3 vec3FromJson(const nlohmann::json& json) {
  if (!json.is_array() || json.size() != 3) {
    throw std::runtime_error("expected a [x, y, z] array");
  }
  return Vec3{json.at(0).get<double>(), json.at(1).get<double>(),
              json.at(2).get<double>()};
}

nlohmann::json stringMapToJson(const std::map<std::string, std::string>& map) {
  nlohmann::json json = nlohmann::json::object();
  for (const auto& [key, value] : map) {
    json[key] = value;
  }
  return json;
}

std::map<std::string, std::string> stringMapFromJson(const nlohmann::json& json) {
  std::map<std::string, std::string> map;
  if (json.is_null()) {
    return map;
  }
  if (!json.is_object()) {
    throw std::runtime_error("expected an object of string values");
  }
  for (const auto& [key, value] : json.items()) {
    map[key] = value.get<std::string>();
  }
  return map;
}

nlohmann::json materialToJson(const Material& material) {
  nlohmann::json json;
  json["id"] = material.id;
  json["name"] = material.name;
  json["bandsHz"] = material.bandsHz;
  json["absorption"] = material.absorption;
  json["scattering"] = material.scattering;
  return json;
}

Material materialFromJson(const nlohmann::json& json) {
  Material material;
  material.id = json.at("id").get<std::string>();
  material.name = json.value("name", std::string{});
  material.bandsHz =
      json.value("bandsHz", std::vector<double>{});
  material.absorption = json.at("absorption").get<std::vector<double>>();
  material.scattering = json.value("scattering", 0.0);
  return material;
}

nlohmann::json sourceToJson(const Source& source) {
  nlohmann::json json;
  json["id"] = source.id;
  json["position"] = vec3ToJson(source.position);
  json["powerDb"] = source.powerDb;
  return json;
}

Source sourceFromJson(const nlohmann::json& json) {
  Source source;
  source.id = json.at("id").get<std::string>();
  source.position = vec3FromJson(json.at("position"));
  source.powerDb = json.value("powerDb", 0.0);
  return source;
}

nlohmann::json receiverToJson(const Receiver& receiver) {
  nlohmann::json json;
  json["id"] = receiver.id;
  json["position"] = vec3ToJson(receiver.position);
  return json;
}

Receiver receiverFromJson(const nlohmann::json& json) {
  Receiver receiver;
  receiver.id = json.at("id").get<std::string>();
  receiver.position = vec3FromJson(json.at("position"));
  return receiver;
}

nlohmann::json roomToJson(const Room& room) {
  nlohmann::json json;
  json["type"] = room.type == Room::Type::Rect ? "rect" : "mesh";
  if (room.type == Room::Type::Rect) {
    nlohmann::json size;
    size["w"] = room.rect.w;
    size["h"] = room.rect.h;
    size["d"] = room.rect.d;
    json["size"] = std::move(size);
    json["faces"] = stringMapToJson(room.rect.faces);
  } else {
    nlohmann::json mesh;
    mesh["uri"] = room.mesh.uri;
    mesh["unit"] = room.mesh.unit;
    mesh["scale"] = room.mesh.scale;
    mesh["materialOverrides"] = stringMapToJson(room.mesh.materialOverrides);
    json["mesh"] = std::move(mesh);
  }
  return json;
}

Room roomFromJson(const nlohmann::json& json) {
  Room room;
  const std::string type = json.at("type").get<std::string>();
  if (type == "rect") {
    room.type = Room::Type::Rect;
    if (json.contains("size")) {
      const nlohmann::json& size = json.at("size");
      room.rect.w = size.value("w", 0.0);
      room.rect.h = size.value("h", 0.0);
      room.rect.d = size.value("d", 0.0);
    }
    if (json.contains("faces")) {
      room.rect.faces = stringMapFromJson(json.at("faces"));
    }
  } else if (type == "mesh") {
    room.type = Room::Type::Mesh;
    if (json.contains("mesh")) {
      const nlohmann::json& mesh = json.at("mesh");
      room.mesh.uri = mesh.value("uri", std::string{});
      room.mesh.unit = mesh.value("unit", std::string{"m"});
      room.mesh.scale = mesh.value("scale", 1.0);
      if (mesh.contains("materialOverrides")) {
        room.mesh.materialOverrides =
            stringMapFromJson(mesh.at("materialOverrides"));
      }
    }
  } else {
    throw std::runtime_error("room.type must be 'rect' or 'mesh'");
  }
  return room;
}

nlohmann::json paramsToJson(const SimParams& params) {
  nlohmann::json json;
  json["engine"] = params.engine;
  json["maxReflectionOrder"] = params.maxReflectionOrder;
  json["sampleRate"] = params.sampleRate;
  json["durationSec"] = params.durationSec;
  json["bandsHz"] = params.bandsHz;
  json["speedOfSound"] = params.speedOfSound;
  json["capturePaths"] = params.capturePaths;
  json["airAbsorption"] = params.airAbsorption;
  json["signedIr"] = params.signedIr;
  json["methodParams"] = params.methodParams;
  return json;
}

SimParams paramsFromJson(const nlohmann::json& json) {
  if (!json.contains("engine")) {
    throw std::runtime_error("params.engine is required");
  }
  SimParams params;
  params.engine = json.at("engine").get<std::string>();
  params.maxReflectionOrder = json.value("maxReflectionOrder", 0);
  params.sampleRate = json.value("sampleRate", 48000);
  params.durationSec = json.value("durationSec", 1.0);
  params.bandsHz = json.value("bandsHz", std::vector<double>{});
  params.speedOfSound = json.value("speedOfSound", kSpeedOfSound);
  params.capturePaths = json.value("capturePaths", false);
  params.airAbsorption = json.value("airAbsorption", false);
  params.signedIr = json.value("signedIr", false);
  params.methodParams =
      json.value("methodParams", nlohmann::json::object());
  return params;
}

}  // namespace

nlohmann::json scenarioToJson(const Scenario& scenario) {
  nlohmann::json json;
  json["schemaVersion"] = scenario.schemaVersion;
  json["name"] = scenario.name;
  json["room"] = roomToJson(scenario.room);

  nlohmann::json materials = nlohmann::json::array();
  for (const Material& material : scenario.materials) {
    materials.push_back(materialToJson(material));
  }
  json["materials"] = std::move(materials);

  nlohmann::json sources = nlohmann::json::array();
  for (const Source& source : scenario.sources) {
    sources.push_back(sourceToJson(source));
  }
  json["sources"] = std::move(sources);

  nlohmann::json receivers = nlohmann::json::array();
  for (const Receiver& receiver : scenario.receivers) {
    receivers.push_back(receiverToJson(receiver));
  }
  json["receivers"] = std::move(receivers);

  json["params"] = paramsToJson(scenario.params);
  return json;
}

Scenario scenarioFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("scenario JSON must be an object");
  }
  if (!json.contains("room")) {
    throw std::runtime_error("scenario is missing 'room'");
  }
  if (!json.contains("materials")) {
    throw std::runtime_error("scenario is missing 'materials'");
  }
  if (!json.contains("params")) {
    throw std::runtime_error("scenario is missing 'params'");
  }

  Scenario scenario;
  scenario.schemaVersion = json.value("schemaVersion", 1);
  scenario.name = json.value("name", std::string{});
  scenario.room = roomFromJson(json.at("room"));
  for (const nlohmann::json& material : json.at("materials")) {
    scenario.materials.push_back(materialFromJson(material));
  }
  if (json.contains("sources")) {
    for (const nlohmann::json& source : json.at("sources")) {
      scenario.sources.push_back(sourceFromJson(source));
    }
  }
  if (json.contains("receivers")) {
    for (const nlohmann::json& receiver : json.at("receivers")) {
      scenario.receivers.push_back(receiverFromJson(receiver));
    }
  }
  scenario.params = paramsFromJson(json.at("params"));
  return scenario;
}

Scenario loadScenario(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("cannot open scenario file: " + path.string());
  }
  nlohmann::json json;
  try {
    input >> json;
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error("invalid JSON in " + path.string() + ": " +
                             error.what());
  }
  return scenarioFromJson(json);
}

void saveScenario(const Scenario& scenario,
                  const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("cannot write scenario file: " + path.string());
  }
  output << scenarioToJson(scenario).dump(2) << '\n';
  if (!output) {
    throw std::runtime_error("failed writing scenario file: " + path.string());
  }
}

}  // namespace av
