#pragma once

#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include <av/geometry/vec3.hpp>

namespace av::detail {

inline nlohmann::json vec3ToJson(const Vec3& v) {
  return nlohmann::json::array({v.x, v.y, v.z});
}

inline Vec3 vec3FromJson(const nlohmann::json& json) {
  if (!json.is_array() || json.size() != 3) {
    throw std::runtime_error("expected a [x, y, z] array");
  }
  return Vec3{json.at(0).get<double>(), json.at(1).get<double>(),
              json.at(2).get<double>()};
}

inline nlohmann::json stringMapToJson(const std::map<std::string, std::string>& map) {
  nlohmann::json json = nlohmann::json::object();
  for (const auto& [key, value] : map) {
    json[key] = value;
  }
  return json;
}

inline std::map<std::string, std::string> stringMapFromJson(const nlohmann::json& json) {
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

}  // namespace av::detail
