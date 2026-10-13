#include <av/io/io.hpp>

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace av {

std::string encodeObj(const Mesh& mesh) {
  std::ostringstream out;
  out << "# Acoustic-Vision generated mesh (units meters)\n";
  out << "# Winding matches tests/fixtures/box.obj.\n";
  out << std::setprecision(15);

  for (const Vec3& p : mesh.positions) {
    out << "v " << p.x << ' ' << p.y << ' ' << p.z << '\n';
  }

  // Faces grouped by material zone so importers get the same materialSlots
  // names as box.obj (g floor / g ceiling / g walls). usemtl carries the same
  // name: loadObj prefers the material name over the group name, so both
  // paths agree, and DCC tools that only read usemtl still see the zones.
  for (std::size_t slot = 0; slot < mesh.materialSlots.size(); ++slot) {
    bool any = false;
    for (const Triangle& triangle : mesh.triangles) {
      if (triangle.materialSlot != slot) {
        continue;
      }
      if (!any) {
        out << "g " << mesh.materialSlots[slot] << '\n';
        out << "usemtl " << mesh.materialSlots[slot] << '\n';
        any = true;
      }
      out << "f " << (triangle.v0 + 1) << ' ' << (triangle.v1 + 1) << ' '
          << (triangle.v2 + 1) << '\n';
    }
  }
  // Faces pointing past materialSlots (defensive; generators always assign).
  for (const Triangle& triangle : mesh.triangles) {
    if (triangle.materialSlot < mesh.materialSlots.size()) {
      continue;
    }
    out << "f " << (triangle.v0 + 1) << ' ' << (triangle.v1 + 1) << ' '
        << (triangle.v2 + 1) << '\n';
  }
  return out.str();
}

void saveObj(const std::filesystem::path& path, const Mesh& mesh) {
  std::ofstream out(path);
  if (!out) {
    throw std::runtime_error("cannot write OBJ file: " + path.string());
  }
  out << encodeObj(mesh);
  if (!out) {
    throw std::runtime_error("failed writing OBJ file: " + path.string());
  }
}

}  // namespace av
