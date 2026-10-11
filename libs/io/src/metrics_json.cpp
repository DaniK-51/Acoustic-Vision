#include <av/io/io.hpp>

#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>

#include "json_common.hpp"

namespace av {
namespace {

// Map key for a band center frequency; 0.0 is broadband (Metrics convention).
std::string bandKey(double bandHz) {
  if (bandHz == 0.0) {
    return "0";
  }
  const long long rounded = std::llround(bandHz);
  if (rounded > 0 &&
      std::abs(bandHz - static_cast<double>(rounded)) < 1e-6) {
    return std::to_string(rounded);
  }
  return std::to_string(bandHz);
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

nlohmann::json metricsToJson(const Metrics& metrics) {
  nlohmann::json rt60 = nlohmann::json::object();
  for (const auto& [band, value] : metrics.rt60) {
    nlohmann::json entry;
    entry["schroederT30"] = value.schroederT30;
    entry["sabine"] = value.sabine;
    entry["eyring"] = value.eyring;
    rt60[bandKey(band)] = entry;
  }

  nlohmann::json edt = nlohmann::json::object();
  for (const auto& [band, value] : metrics.edt) {
    edt[bandKey(band)] = value;
  }

  nlohmann::json early = nlohmann::json::array();
  for (const EarlyReflection& reflection : metrics.earlyReflections) {
    nlohmann::json entry;
    entry["t"] = reflection.t;
    entry["amp"] = reflection.amp;
    entry["order"] = reflection.order;
    early.push_back(std::move(entry));
  }

  nlohmann::json json;
  json["rt60"] = std::move(rt60);
  json["edt"] = std::move(edt);
  json["earlyReflections"] = std::move(early);
  return json;
}

void writeMetricsJson(const std::filesystem::path& path, const Result& result) {
  writeJsonFile(
      result.metrics ? metricsToJson(*result.metrics) : metricsToJson(Metrics{}),
      path);
}

nlohmann::json fieldToJson(const SpatialField& field) {
  nlohmann::json json;
  json["nx"] = field.nx;
  json["ny"] = field.ny;
  json["nz"] = field.nz;
  json["origin"] = detail::vec3ToJson(field.origin);
  json["cellSize"] = field.cellSize;
  json["bandHz"] = field.bandHz;
  json["unit"] = field.unit;
  json["values"] = field.values;
  return json;
}

void writeFieldJson(const std::filesystem::path& path, const Result& result) {
  writeJsonFile(result.field ? fieldToJson(*result.field) : nlohmann::json{},
                path);
}

namespace {

// One polyline per captured ray path: world-space vertices plus the
// bounce-factor amplitude and wall-hit count the engine recorded.
nlohmann::json raysToJson(const RayPaths& rays) {
  nlohmann::json paths = nlohmann::json::array();
  for (const RayPolyline& path : rays.paths) {
    nlohmann::json points = nlohmann::json::array();
    for (const Vec3& point : path.points) {
      points.push_back(detail::vec3ToJson(point));
    }
    nlohmann::json entry;
    entry["points"] = std::move(points);
    entry["amplitude"] = path.amplitude;
    entry["order"] = path.order;
    paths.push_back(std::move(entry));
  }
  nlohmann::json json;
  json["paths"] = std::move(paths);
  return json;
}

}  // namespace

nlohmann::json resultToJson(const Result& result) {
  nlohmann::json json;
  json["metrics"] =
      result.metrics ? metricsToJson(*result.metrics) : metricsToJson(Metrics{});
  if (result.ir) {
    nlohmann::json ir;
    ir["sampleRate"] = result.ir->sampleRate;
    ir["durationSec"] = result.ir->durationSec;
    ir["channels"] = result.ir->channels;
    ir["channelNames"] = result.ir->channelNames;
    ir["bandsHz"] = result.ir->bandsHz;
    json["ir"] = std::move(ir);
  }
  if (result.field) {
    json["field"] = fieldToJson(*result.field);
  }
  if (result.rays) {
    json["rays"] = raysToJson(*result.rays);
  }
  return json;
}

void writeResultJson(const std::filesystem::path& path, const Result& result) {
  writeJsonFile(resultToJson(result), path);
}

}  // namespace av
