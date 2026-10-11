#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/io/io.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

void printUsage(std::ostream& out) {
  out << "usage:\n"
      << "  av-cli engines\n"
      << "  av-cli run <scenario.json> [-o outdir]\n";
}

int listEngines() {
  av::linkBuiltinEngines();
  const std::vector<av::EngineDescriptor> descriptors =
      av::EngineRegistry::instance().descriptors();
  if (descriptors.empty()) {
    std::cerr << "error: no engines registered\n";
    return 1;
  }
  for (const av::EngineDescriptor& desc : descriptors) {
    std::cout << desc.id << "  " << desc.name << "  v" << desc.version
              << "  artifacts:";
    for (const std::string& artifact : desc.artifacts) {
      std::cout << ' ' << artifact;
    }
    std::cout << '\n';
  }
  return 0;
}

int runScenario(const std::filesystem::path& scenarioPath,
                const std::filesystem::path& outDir) {
  av::linkBuiltinEngines();

  av::Scenario scenario = av::loadScenario(scenarioPath);
  // Mesh uris resolve against the scenario file's directory (P2c policy).
  av::fillLoadedMesh(scenario, scenarioPath.parent_path());

  std::unique_ptr<av::IEngine> engine =
      av::EngineRegistry::instance().create(scenario.params.engine);
  if (!engine) {
    std::cerr << "error: unknown engine '" << scenario.params.engine << "'\n";
    return 1;
  }

  const av::ValidateResult validation = engine->validate(scenario);
  for (const std::string& warning : validation.warnings) {
    std::cerr << "warning: " << warning << '\n';
  }
  if (!validation.ok) {
    for (const std::string& error : validation.errors) {
      std::cerr << "error: " << error << '\n';
    }
    return 1;
  }

  std::error_code ec;
  std::filesystem::create_directories(outDir, ec);
  if (ec) {
    std::cerr << "error: cannot create output directory " << outDir << ": "
              << ec.message() << '\n';
    return 1;
  }

  av::CancelToken cancel;
  const av::Result result = engine->run(
      scenario, scenario.params, cancel, [](const av::Progress& progress) {
        std::cerr << "progress: " << progress.stage << ' '
                  << static_cast<int>(progress.fraction * 100.0) << "%\n";
      });

  if (!result.metrics && !result.ir && !result.field && !result.rays &&
      !result.findings) {
    std::cerr << "error: engine produced no artifacts\n";
    return 1;
  }

  if (result.metrics) {
    const std::filesystem::path path = outDir / "metrics.json";
    av::writeMetricsJson(path, result);
    std::cout << "wrote " << path.string() << '\n';
  }
  if (result.ir) {
    const std::filesystem::path path = outDir / "ir.wav";
    av::writeWav(path, *result.ir);
    std::cout << "wrote " << path.string() << '\n';
  }
  if (result.field) {
    const std::filesystem::path path = outDir / "field.json";
    av::writeFieldJson(path, result);
    std::cout << "wrote " << path.string() << '\n';
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty() || args[0] == "-h" || args[0] == "--help") {
    printUsage(args.empty() ? std::cerr : std::cout);
    return args.empty() ? 1 : 0;
  }

  try {
    if (args[0] == "engines") {
      return listEngines();
    }
    if (args[0] == "run") {
      if (args.size() < 2) {
        std::cerr << "error: run requires a scenario file\n";
        printUsage(std::cerr);
        return 1;
      }
      std::filesystem::path scenarioPath;
      std::filesystem::path outDir = ".";
      for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "-o" || args[i] == "--out") {
          if (i + 1 >= args.size()) {
            std::cerr << "error: " << args[i] << " requires a directory\n";
            return 1;
          }
          outDir = args[++i];
        } else if (scenarioPath.empty()) {
          scenarioPath = args[i];
        } else {
          std::cerr << "error: unexpected argument '" << args[i] << "'\n";
          printUsage(std::cerr);
          return 1;
        }
      }
      if (scenarioPath.empty()) {
        std::cerr << "error: run requires a scenario file\n";
        printUsage(std::cerr);
        return 1;
      }
      return runScenario(scenarioPath, outDir);
    }
    std::cerr << "error: unknown command '" << args[0] << "'\n";
    printUsage(std::cerr);
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
