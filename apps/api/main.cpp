#include <av/acoustics/builtin_engines.hpp>
#include <av/io/source_catalog.hpp>
#include <av/sim/job_manager.hpp>

#include <crow.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "api_routes.hpp"
#include "api_support.hpp"
#include "job_watcher.hpp"
#include "runs_store.hpp"
#include "scene_store.hpp"

namespace {

// Directory of the running executable, used to find source_catalog.json
// beside the binary when AV_SOURCE_CATALOG is unset.
std::filesystem::path exeDirOf(const char* argv0) {
#if defined(_WIN32)
  char buf[MAX_PATH] = {};
  const DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (n > 0 && n < MAX_PATH) {
    return std::filesystem::path(buf).parent_path();
  }
#else
  std::error_code ec;
  const std::filesystem::path self =
      std::filesystem::read_symlink("/proc/self/exe", ec);
  if (!ec) {
    return self.parent_path();
  }
#endif
  return std::filesystem::absolute(argv0).parent_path();
}

}  // namespace

int main(int argc, char** argv) {
  std::string bindAddr = "127.0.0.1";
  int port = 8787;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
      port = std::atoi(argv[++i]);
    } else if (arg == "--bind" && i + 1 < argc) {
      bindAddr = argv[++i];
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "usage: av-api [--port N] [--bind ADDR]\n";
      return 0;
    } else {
      std::cerr << "error: unknown argument '" << arg << "'\n";
      return 1;
    }
  }
  if (port <= 0 || port > 65535) {
    std::cerr << "error: invalid port\n";
    return 1;
  }

  av::linkBuiltinEngines();

  // Source catalog is loaded once at startup (read-only); engines/JobManager
  // never see it.
  const av::SourceCatalog sourceCatalog =
      av::loadDefaultSourceCatalog(exeDirOf(argv[0]));

  av::JobManager jobs;
  av::api::EventHub hub;
  av::api::RunsStore runs;
  av::api::SceneStore scenes;
  av::api::JobWatcher watcher(jobs, hub, runs);

  crow::SimpleApp app;
  av::api::registerRoutes(app, jobs, hub, runs, scenes, sourceCatalog);
  watcher.start();

  std::cout << "av-api listening on http://" << bindAddr << ':' << port
            << "/v1\n";
  app.bindaddr(bindAddr).port(port).run();

  watcher.stop();
  return 0;
}
