#include <av/acoustics/builtin_engines.hpp>
#include <av/acoustics/engine.hpp>
#include <av/geometry/mesh_builder.hpp>
#include <av/io/io.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void printUsage(std::ostream& out) {
  out << "usage:\n"
      << "  av-cli engines\n"
      << "  av-cli run <scenario.json> [-o outdir]\n"
      << "  av-cli gen-mesh box -o room.obj --w <m> --h <m> --d <m>\n"
      << "      [--origin x,y,z] [--scenario room-scenario.json]\n"
      << "  av-cli gen-mesh corridor -o corr.obj --w <m> --h <m> --d <m>\n"
      << "      [--origin x,y,z] [--scenario corr-scenario.json]  (--len = --d)\n"
      << "  av-cli gen-mesh lroom -o l.obj --w <m> --d <m> --w2 <m> --d2 <m>\n"
      << "      --h <m> [--origin x,y,z] [--scenario l-scenario.json]\n";
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

double parseDimension(const std::string& text, const std::string& flag) {
  std::size_t used = 0;
  double value = 0.0;
  try {
    value = std::stod(text, &used);
  } catch (const std::exception&) {
    throw std::runtime_error(flag + " expects a number, got '" + text + "'");
  }
  if (used != text.size()) {
    throw std::runtime_error(flag + " expects a number, got '" + text + "'");
  }
  return value;
}

av::Vec3 parseOrigin(const std::string& text) {
  av::Vec3 origin;
  std::size_t start = 0;
  double* components[3] = {&origin.x, &origin.y, &origin.z};
  const char* names[3] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) {
    const std::size_t comma = text.find(',', start);
    const std::string part =
        text.substr(start, comma == std::string::npos ? comma : comma - start);
    *components[i] = parseDimension(part, std::string("origin ") + names[i]);
    if (comma == std::string::npos) {
      if (i != 2) {
        throw std::runtime_error("--origin expects x,y,z");
      }
      break;
    }
    if (i == 2) {
      throw std::runtime_error("--origin expects x,y,z");
    }
    start = comma + 1;
  }
  return origin;
}

av::Material placeholderMaterial(const std::string& id, const std::string& name,
                                 std::vector<double> absorption) {
  av::Material material;
  material.id = id;
  material.name = name;
  material.bandsHz = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};
  material.absorption = std::move(absorption);
  material.scattering = 0.1;
  return material;
}

// Runnable placeholder: mesh room + three stock materials + one source /
// receiver pair inside the shell + ray-tracing with a field grid. Positions
// are supplied by the caller: for L-rooms the bbox midpoint can fall in the
// empty notch, so generateMesh picks points in the filled arms instead.
void writePlaceholderScenario(const std::filesystem::path& objPath,
                              const std::filesystem::path& scenarioPath,
                              const std::string& name,
                              const av::Vec3& sourcePos,
                              const av::Vec3& receiverPos) {
  av::Scenario scenario;
  scenario.schemaVersion = 1;
  scenario.name = name;
  scenario.room.type = av::Room::Type::Mesh;
  // Sibling uri: resolve the OBJ relative to the scenario file's directory
  // (same policy as av-cli run's fillLoadedMesh baseDir).
  std::filesystem::path uri =
      std::filesystem::relative(objPath, scenarioPath.parent_path());
  if (uri.empty()) {
    uri = objPath.filename();
  }
  scenario.room.mesh.uri = uri.generic_string();
  scenario.room.mesh.unit = "m";
  scenario.room.mesh.scale = 1.0;
  scenario.room.mesh.materialOverrides = {
      {"floor", "mat-carpet"},
      {"ceiling", "mat-plaster"},
      {"walls", "mat-brick"},
  };

  scenario.materials.push_back(placeholderMaterial(
      "mat-carpet", "Carpet on concrete", {0.1, 0.2, 0.3, 0.4, 0.4, 0.4}));
  scenario.materials.push_back(placeholderMaterial(
      "mat-plaster", "Plasterboard", {0.1, 0.1, 0.15, 0.2, 0.2, 0.2}));
  scenario.materials.push_back(placeholderMaterial(
      "mat-brick", "Painted brick", {0.1, 0.1, 0.12, 0.15, 0.15, 0.2}));

  av::Source source;
  source.id = "src-1";
  source.powerDb = 80.0;
  source.position = sourcePos;
  scenario.sources.push_back(source);

  av::Receiver receiver;
  receiver.id = "rcv-1";
  receiver.position = receiverPos;
  scenario.receivers.push_back(receiver);

  scenario.params.engine = "ray-tracing";
  scenario.params.sampleRate = 8000;
  scenario.params.durationSec = 0.5;
  scenario.params.bandsHz = {125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0};
  scenario.params.speedOfSound = 343.0;
  scenario.params.capturePaths = true;
  scenario.params.fieldGrid.enabled = true;
  scenario.params.fieldGrid.nx = 16;
  scenario.params.fieldGrid.ny = 8;
  scenario.params.fieldGrid.nz = 12;
  scenario.params.methodParams = {
      {"rayCount", 2000},
      {"maxBounces", 48},
      {"seed", 3},
      {"receiverRadius", 0.25},
      {"capturePathLimit", 16},
  };

  av::saveScenario(scenario, scenarioPath);
  std::cout << "wrote " << scenarioPath.string() << '\n';
}

int generateMesh(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    std::cerr << "error: gen-mesh requires a shape (box|corridor|lroom)\n";
    printUsage(std::cerr);
    return 1;
  }
  const std::string shape = args[1];

  std::filesystem::path outPath;
  std::filesystem::path scenarioPath;
  av::Vec3 origin;
  double w = 0.0;
  double h = 0.0;
  double d = 0.0;
  double length = 0.0;
  double w2 = 0.0;
  double d2 = 0.0;

  for (std::size_t i = 2; i < args.size(); ++i) {
    const std::string& arg = args[i];
    auto takeValue = [&](const std::string& flag) -> std::string {
      if (i + 1 >= args.size()) {
        throw std::runtime_error(flag + " requires a value");
      }
      return args[++i];
    };
    if (arg == "-o" || arg == "--out") {
      outPath = takeValue(arg);
    } else if (arg == "--scenario") {
      scenarioPath = takeValue(arg);
    } else if (arg == "--origin") {
      origin = parseOrigin(takeValue(arg));
    } else if (arg == "--w") {
      w = parseDimension(takeValue(arg), arg);
    } else if (arg == "--h") {
      h = parseDimension(takeValue(arg), arg);
    } else if (arg == "--d") {
      d = parseDimension(takeValue(arg), arg);
    } else if (arg == "--len") {
      length = parseDimension(takeValue(arg), arg);
    } else if (arg == "--w2") {
      w2 = parseDimension(takeValue(arg), arg);
    } else if (arg == "--d2") {
      d2 = parseDimension(takeValue(arg), arg);
    } else {
      std::cerr << "error: unexpected argument '" << arg << "'\n";
      printUsage(std::cerr);
      return 1;
    }
  }

  if (outPath.empty()) {
    std::cerr << "error: gen-mesh requires -o <out.obj>\n";
    printUsage(std::cerr);
    return 1;
  }

  av::Mesh mesh;
  av::Vec3 sourcePos;
  av::Vec3 receiverPos;
  if (shape == "box") {
    if (w <= 0.0 || h <= 0.0 || d <= 0.0) {
      std::cerr << "error: box requires --w, --h and --d\n";
      return 1;
    }
    mesh = av::generateBoxMesh(origin, w, h, d);
    sourcePos = {origin.x + 0.3 * w, origin.y + 0.3 * d, origin.z + 0.5 * h};
    receiverPos = {origin.x + 0.7 * w, origin.y + 0.7 * d, origin.z + 0.5 * h};
  } else if (shape == "corridor") {
    // --d is the architect-spec name (corridor length along +y); --len is a
    // readability alias. Prefer an explicit --len when both are given.
    const double corridorLength = length > 0.0 ? length : d;
    if (w <= 0.0 || h <= 0.0 || corridorLength <= 0.0) {
      std::cerr << "error: corridor requires --w, --h and --d (or --len)\n";
      return 1;
    }
    mesh = av::generateCorridorMesh(origin, w, h, corridorLength);
    sourcePos = {origin.x + 0.5 * w, origin.y + 0.2 * corridorLength,
                 origin.z + 0.5 * h};
    receiverPos = {origin.x + 0.5 * w, origin.y + 0.8 * corridorLength,
                   origin.z + 0.5 * h};
  } else if (shape == "lroom") {
    if (w <= 0.0 || d <= 0.0 || h <= 0.0 || w2 <= 0.0 || d2 <= 0.0) {
      std::cerr << "error: lroom requires --w, --d, --w2, --d2 and --h\n";
      return 1;
    }
    mesh = av::generateLRoomMesh(origin, w, d, w2, d2, h);
    // Keep both points in the filled arms: source in the long (w x d2) arm,
    // receiver in the tall (w2 x (d-d2)) arm — never in the notch
    // (w2..w) x (d2..d).
    sourcePos = {origin.x + 0.5 * w, origin.y + 0.5 * d2, origin.z + 0.5 * h};
    receiverPos = {origin.x + 0.5 * w2, origin.y + d2 + 0.5 * (d - d2),
                   origin.z + 0.5 * h};
  } else {
    std::cerr << "error: unknown shape '" << shape
              << "' (expected box|corridor|lroom)\n";
    printUsage(std::cerr);
    return 1;
  }

  av::saveObj(outPath, mesh);
  std::cout << "wrote " << outPath.string() << " (" << mesh.positions.size()
            << " vertices, " << mesh.triangles.size() << " triangles)\n";

  if (!scenarioPath.empty()) {
    writePlaceholderScenario(outPath, scenarioPath, outPath.stem().string(),
                             sourcePos, receiverPos);
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
    if (args[0] == "gen-mesh") {
      return generateMesh(args);
    }
    std::cerr << "error: unknown command '" << args[0] << "'\n";
    printUsage(std::cerr);
    return 1;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
