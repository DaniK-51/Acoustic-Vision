#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <av/scenario/directivity.hpp>

namespace av::detail {

// Polar curve JSON layout (schemas): freqsHz[i] owns gainDb[i], each row
// sampled at anglesDeg (row length must equal anglesDeg.size()).

inline nlohmann::json polarCurveToJson(const PolarCurve& curve) {
  nlohmann::json json;
  json["freqsHz"] = curve.freqsHz;
  json["anglesDeg"] = curve.anglesDeg;
  json["gainDb"] = curve.gainDb;
  return json;
}

inline PolarCurve polarCurveFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected a polar curve object");
  }
  PolarCurve curve;
  curve.freqsHz = json.at("freqsHz").get<std::vector<double>>();
  curve.anglesDeg = json.at("anglesDeg").get<std::vector<double>>();
  curve.gainDb = json.at("gainDb").get<std::vector<std::vector<double>>>();
  if (curve.freqsHz.empty()) {
    throw std::runtime_error("polar curve freqsHz must be non-empty");
  }
  if (curve.anglesDeg.empty()) {
    throw std::runtime_error("polar curve anglesDeg must be non-empty");
  }
  if (curve.gainDb.size() != curve.freqsHz.size()) {
    throw std::runtime_error(
        "polar curve gainDb must have one row per freqsHz entry");
  }
  for (const std::vector<double>& row : curve.gainDb) {
    if (row.size() != curve.anglesDeg.size()) {
      throw std::runtime_error(
          "polar curve gainDb rows must match anglesDeg length");
    }
  }
  return curve;
}

// Shared Source.directivity / catalog-card directivity object:
// {type, description?, horizontal?, vertical?}.
inline nlohmann::json directivityToJson(const Directivity& directivity) {
  nlohmann::json json;
  json["type"] = directivity.type;
  if (!directivity.description.empty()) {
    json["description"] = directivity.description;
  }
  if (directivity.horizontal.has_value()) {
    json["horizontal"] = polarCurveToJson(*directivity.horizontal);
  }
  if (directivity.vertical.has_value()) {
    json["vertical"] = polarCurveToJson(*directivity.vertical);
  }
  return json;
}

inline Directivity directivityFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected a directivity object");
  }
  Directivity directivity;
  directivity.type = json.at("type").get<std::string>();
  if (directivity.type != "omni" && directivity.type != "dipole" &&
      directivity.type != "cardioid" && directivity.type != "custom") {
    throw std::runtime_error(
        "directivity.type must be omni|dipole|cardioid|custom");
  }
  directivity.description = json.value("description", std::string{});
  if (json.contains("horizontal")) {
    directivity.horizontal = polarCurveFromJson(json.at("horizontal"));
  }
  if (json.contains("vertical")) {
    directivity.vertical = polarCurveFromJson(json.at("vertical"));
  }
  return directivity;
}

inline nlohmann::json orientationToJson(const SourceOrientation& orientation) {
  nlohmann::json json;
  json["yawDeg"] = orientation.yawDeg;
  json["pitchDeg"] = orientation.pitchDeg;
  return json;
}

inline SourceOrientation orientationFromJson(const nlohmann::json& json) {
  if (!json.is_object()) {
    throw std::runtime_error("expected an orientation object");
  }
  SourceOrientation orientation;
  orientation.yawDeg = json.value("yawDeg", 0.0);
  orientation.pitchDeg = json.value("pitchDeg", 0.0);
  return orientation;
}

}  // namespace av::detail
