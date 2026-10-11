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

// Second registration in the same TU: catches __LINE__ pasting bugs in
// REGISTER_ENGINE (both would collapse to one symbol and fail to compile).
class DummyEngineB : public av::IEngine {
 public:
  [[nodiscard]] av::EngineDescriptor descriptor() const override {
    av::EngineDescriptor desc;
    desc.id = "dummy-b";
    desc.name = "Dummy Engine B";
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

REGISTER_ENGINE(DummyEngineB);

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

TEST(EngineRegistry, TwoRegistrationsInOneTranslationUnit) {
  auto& registry = av::EngineRegistry::instance();
  EXPECT_TRUE(registry.contains("dummy"));
  EXPECT_TRUE(registry.contains("dummy-b"));
  EXPECT_NE(registry.create("dummy"), nullptr);
  EXPECT_NE(registry.create("dummy-b"), nullptr);
}
