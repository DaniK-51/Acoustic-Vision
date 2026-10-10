#include <av/acoustics/engine.hpp>

namespace av {

EngineRegistry& EngineRegistry::instance() {
  static EngineRegistry registry;
  return registry;
}

bool EngineRegistry::add(Factory factory) {
  auto probe = factory();
  if (!probe) {
    return false;
  }
  EngineDescriptor desc = probe->descriptor();
  if (desc.id.empty() || m_factories.contains(desc.id)) {
    return false;
  }
  m_factories.emplace(desc.id, std::move(factory));
  m_descriptors.emplace(desc.id, std::move(desc));
  return true;
}

std::unique_ptr<IEngine> EngineRegistry::create(const std::string& id) const {
  const auto it = m_factories.find(id);
  if (it == m_factories.end()) {
    return nullptr;
  }
  return it->second();
}

std::vector<EngineDescriptor> EngineRegistry::descriptors() const {
  std::vector<EngineDescriptor> out;
  out.reserve(m_descriptors.size());
  for (const auto& [id, desc] : m_descriptors) {
    out.push_back(desc);
  }
  return out;
}

bool EngineRegistry::contains(const std::string& id) const {
  return m_factories.contains(id);
}

}  // namespace av
