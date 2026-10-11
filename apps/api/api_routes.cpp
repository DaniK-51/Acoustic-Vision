#include "api_routes.hpp"

namespace av::api {

void registerRoutes(crow::SimpleApp& app, JobManager& jobs, EventHub& hub,
                    RunsStore& runs, SceneStore& scenes,
                    const SourceCatalog& sourceCatalog) {
  registerHealthRoutes(app, sourceCatalog);
  registerScenarioRoutes(app, scenes);
  registerSimulationRoutes(app, jobs, runs, scenes);
  registerRunRoutes(app, jobs, runs);
  registerJobRoutes(app, jobs);
  registerEventRoutes(app, hub, scenes);
}

}  // namespace av::api
