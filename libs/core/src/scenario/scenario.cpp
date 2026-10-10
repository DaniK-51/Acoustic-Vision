#include <av/scenario/scenario.hpp>

#include <array>

namespace av {

std::vector<RectFace> rectFaces(const RectRoom& room) {
  // Canonical order matches schemas/scenario.schema.json face keys.
  static constexpr std::array<std::string_view, 6> kNames{
      "floor", "ceiling", "x+", "x-", "z+", "z-"};

  std::vector<RectFace> faces;
  faces.reserve(kNames.size());
  for (std::string_view name : kNames) {
    std::string key{name};
    const auto it = room.faces.find(key);
    faces.push_back(RectFace{
        std::move(key),
        it != room.faces.end() ? it->second : std::string{}});
  }
  return faces;
}

const Material* findMaterial(const Scenario& scenario, std::string_view id) {
  for (const Material& material : scenario.materials) {
    if (material.id == id) {
      return &material;
    }
  }
  return nullptr;
}

Material* findMaterial(Scenario& scenario, std::string_view id) {
  return const_cast<Material*>(
      findMaterial(static_cast<const Scenario&>(scenario), id));
}

}  // namespace av
