#include <gtest/gtest.h>

#include <av/acoustics/engine.hpp>

namespace {

class DummyEngine : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    av::EngineDescriptor desc;
    desc.id = "dummy";
    desc.name = "Dummy Engine";
    desc.version = "0.0.0";
    desc.paramsSchema = nlohmann::json::object();
    return desc;
  }

  [[nodiscard]] av::ValidateResult validate(const av::Scenario&) const override {
    return {};
  }

  av::Result run(const av::Scenario&, const av::SimParams&,
                 const av::CancelToken&, av::ProgressFn) override {
    return {};
  }
};

REGISTER_ENGINE(DummyEngine);

}  // namespace

TEST(EngineRegistry, RegisterAndCreateDummyEngine) {
  auto& registry = av::EngineRegistry::instance();
  ASSERT_TRUE(registry.contains("dummy"));

  auto engine = registry.create("dummy");
  ASSERT_NE(engine, nullptr);
  EXPECT_EQ(engine->descriptor().id, "dummy");
  EXPECT_EQ(engine->descriptor().name, "Dummy Engine");

  const auto descs = registry.descriptors();
  bool found = false;
  for (const auto& d : descs) {
    if (d.id == "dummy") {
      found = true;
    }
  }
  EXPECT_TRUE(found);
}

TEST(EngineRegistry, CreateUnknownReturnsNull) {
  auto engine = av::EngineRegistry::instance().create("no-such-engine");
  EXPECT_EQ(engine, nullptr);
}
