#include "api_routes.hpp"

#include <av/acoustics/engine.hpp>
#include <av/units.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

namespace av::api {

void registerHealthRoutes(crow::SimpleApp& app,
                          const SourceCatalog& sourceCatalog) {
  CROW_ROUTE(app, "/v1/health").methods(crow::HTTPMethod::Get)([] {
    nlohmann::json engines = nlohmann::json::array();
    for (const EngineDescriptor& desc :
         EngineRegistry::instance().descriptors()) {
      engines.push_back(desc.id);
    }
    nlohmann::json body;
    body["status"] = "ok";
    body["version"] = AV_VERSION;
    body["engines"] = std::move(engines);
    return jsonRes(200, body);
  });

  CROW_ROUTE(app, "/v1/engines").methods(crow::HTTPMethod::Get)([] {
    nlohmann::json body = nlohmann::json::array();
    for (const EngineDescriptor& desc :
         EngineRegistry::instance().descriptors()) {
      body.push_back(descriptorToJson(desc));
    }
    return jsonRes(200, body);
  });

  CROW_ROUTE(app, "/v1/bands").methods(crow::HTTPMethod::Get)([] {
    nlohmann::json body;
    body["bandsHz"] = std::vector<double>(kDefaultBandsHz.begin(),
                                          kDefaultBandsHz.end());
    return jsonRes(200, body);
  });

  // Read-only built-in device catalog (docs/idea.md decision 6: data as-is,
  // disclaimer required). Always 200 — empty list when no catalog file was
  // found/loaded at startup.
  CROW_ROUTE(app, "/v1/source-catalog")
      .methods(crow::HTTPMethod::Get)([&sourceCatalog] {
        return jsonRes(200, sourceCatalogToJson(sourceCatalog));
      });
}

}  // namespace av::api
