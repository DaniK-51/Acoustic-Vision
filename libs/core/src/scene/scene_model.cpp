#include <av/scene/scene_model.hpp>

#include <cmath>
#include <set>
#include <string>
#include <variant>

namespace av {
namespace {

std::string entityLabel(const SceneEntity& entity) {
  return std::string(entityType(entity)) + " '" + entityId(entity) + "'";
}

void validateUniqueIds(const Scene& scene, std::vector<std::string>& errors) {
  std::set<std::string> seen;
  for (const SceneEntity& entity : scene.entities) {
    const std::string& id = entityId(entity);
    if (id.empty()) {
      errors.push_back("entity id must not be empty");
      continue;
    }
    if (!seen.insert(id).second) {
      errors.push_back("duplicate entity id '" + id + "'");
    }
  }
}

void validateMaterialIds(const Scene& scene, std::vector<std::string>& errors) {
  std::set<std::string> known;
  for (const SceneMaterial& material : scene.materials) {
    if (material.id.empty()) {
      errors.push_back("material id must not be empty");
      continue;
    }
    if (!known.insert(material.id).second) {
      errors.push_back("duplicate material id '" + material.id + "'");
    }
  }
  const auto refOk = [&](const std::string& materialId,
                         const std::string& where) {
    if (materialId.empty()) {
      errors.push_back(where + ": materialId must not be empty");
    } else if (!known.contains(materialId)) {
      errors.push_back(where + ": unknown materialId '" + materialId + "'");
    }
  };
  for (const SceneEntity& entity : scene.entities) {
    if (const auto* wall = std::get_if<Wall>(&entity)) {
      const std::string where = entityLabel(entity);
      refOk(wall->faces.inner, where + " faces.inner");
      refOk(wall->faces.outer, where + " faces.outer");
      refOk(wall->faces.side, where + " faces.side");
    } else if (const auto* box = std::get_if<SolidBox>(&entity)) {
      refOk(box->materialId, entityLabel(entity));
    } else if (const auto* extrude = std::get_if<SolidExtrude>(&entity)) {
      refOk(extrude->materialId, entityLabel(entity));
    }
  }
}

void validateWall(const Wall& wall, std::vector<std::string>& errors) {
  const std::string where = entityLabel(wall);
  const double length2 = lengthSquared(wall.end - wall.start);
  if (!(wall.height > 0.0)) {
    errors.push_back(where + ": height must be positive");
  }
  if (!(wall.thickness > 0.0)) {
    errors.push_back(where + ": thickness must be positive");
  }
  if (!(length2 > 0.0)) {
    errors.push_back(where + ": start and end must differ");
    return;
  }
  const double baseline = std::sqrt(length2);
  for (std::size_t i = 0; i < wall.openings.size(); ++i) {
    const WallOpening& opening = wall.openings[i];
    const std::string at = where + " openings[" + std::to_string(i) + "]";
    if (!(opening.width > 0.0)) {
      errors.push_back(at + ": width must be positive");
    }
    if (!(opening.height > 0.0)) {
      errors.push_back(at + ": height must be positive");
    }
    if (opening.offset < 0.0) {
      errors.push_back(at + ": offset must be >= 0");
    }
    if (opening.sill < 0.0) {
      errors.push_back(at + ": sill must be >= 0");
    }
    if (opening.offset >= 0.0 && opening.width > 0.0 &&
        opening.offset + opening.width > baseline) {
      errors.push_back(at + ": opening does not fit along the wall baseline");
    }
    if (opening.sill >= 0.0 && opening.height > 0.0 && wall.height > 0.0 &&
        opening.sill + opening.height > wall.height) {
      errors.push_back(at + ": opening does not fit in the wall height");
    }
  }
}

void validateSolidBox(const SolidBox& box, std::vector<std::string>& errors) {
  const std::string where = entityLabel(box);
  if (!(box.size.x > 0.0) || !(box.size.y > 0.0) || !(box.size.z > 0.0)) {
    errors.push_back(where + ": size must be positive in all axes");
  }
}

void validateSolidExtrude(const SolidExtrude& extrude,
                          std::vector<std::string>& errors) {
  const std::string where = entityLabel(extrude);
  if (extrude.polygon.size() < 3) {
    errors.push_back(where + ": polygon needs at least 3 points");
  }
  if (!(extrude.height > 0.0)) {
    errors.push_back(where + ": height must be positive");
  }
}

void validateVoid(const Void& voidEntity, std::vector<std::string>& errors) {
  const std::string where = entityLabel(voidEntity);
  if (!(voidEntity.size.x > 0.0) || !(voidEntity.size.y > 0.0) ||
      !(voidEntity.size.z > 0.0)) {
    errors.push_back(where + ": size must be positive in all axes");
  }
}

void validateMeshSettings(const MeshSettings& settings,
                          std::vector<std::string>& errors) {
  if (!(settings.surface.targetEdge > 0.0)) {
    errors.push_back("meshSettings.surface.targetEdge must be positive");
  }
  if (!(settings.surface.maxEdge > 0.0)) {
    errors.push_back("meshSettings.surface.maxEdge must be positive");
  }
  if (settings.surface.targetEdge > 0.0 && settings.surface.maxEdge > 0.0 &&
      settings.surface.targetEdge > settings.surface.maxEdge) {
    errors.push_back(
        "meshSettings.surface.targetEdge must not exceed maxEdge");
  }
  if (!(settings.volume.targetSize > 0.0)) {
    errors.push_back("meshSettings.volume.targetSize must be positive");
  }
}

}  // namespace

std::string_view entityType(const SceneEntity& entity) {
  struct Visitor {
    std::string_view operator()(const Wall&) const { return "wall"; }
    std::string_view operator()(const SolidBox&) const { return "solid_box"; }
    std::string_view operator()(const SolidExtrude&) const {
      return "solid_extrude";
    }
    std::string_view operator()(const Void&) const { return "void"; }
    std::string_view operator()(const SourceRef&) const { return "source"; }
    std::string_view operator()(const ReceiverRef&) const { return "receiver"; }
  };
  return std::visit(Visitor{}, entity);
}

const std::string& entityId(const SceneEntity& entity) {
  struct Visitor {
    const std::string& operator()(const Wall& e) const { return e.id; }
    const std::string& operator()(const SolidBox& e) const { return e.id; }
    const std::string& operator()(const SolidExtrude& e) const { return e.id; }
    const std::string& operator()(const Void& e) const { return e.id; }
    const std::string& operator()(const SourceRef& e) const { return e.id; }
    const std::string& operator()(const ReceiverRef& e) const { return e.id; }
  };
  return std::visit(Visitor{}, entity);
}

std::vector<std::string> validateScene(const Scene& scene) {
  std::vector<std::string> errors;
  if (scene.units != "m") {
    errors.push_back("units must be 'm'");
  }
  validateUniqueIds(scene, errors);
  validateMaterialIds(scene, errors);
  for (const SceneEntity& entity : scene.entities) {
    if (const auto* wall = std::get_if<Wall>(&entity)) {
      validateWall(*wall, errors);
    } else if (const auto* box = std::get_if<SolidBox>(&entity)) {
      validateSolidBox(*box, errors);
    } else if (const auto* extrude = std::get_if<SolidExtrude>(&entity)) {
      validateSolidExtrude(*extrude, errors);
    } else if (const auto* voidEntity = std::get_if<Void>(&entity)) {
      validateVoid(*voidEntity, errors);
    }
  }
  validateMeshSettings(scene.meshSettings, errors);
  return errors;
}

}  // namespace av
