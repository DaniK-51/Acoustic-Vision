#include <gtest/gtest.h>

#include "scene_store.hpp"

#include <string>
#include <vector>

namespace {

using av::api::PatchResult;
using av::api::ScenePatchOp;
using av::api::SceneSnapshot;

av::Scenario sampleScenario() {
  av::Scenario scenario;
  scenario.schemaVersion = 1;
  scenario.name = "scene-store-demo";
  scenario.sources.push_back(av::Source{"src-1", av::Vec3{1.0, 1.5, 1.0}, 80.0});
  scenario.sources.push_back(av::Source{"src-2", av::Vec3{2.0, 1.0, 2.0}, 60.0});
  scenario.receivers.push_back(av::Receiver{"rcv-1", av::Vec3{4.0, 1.5, 3.0}});
  return scenario;
}

ScenePatchOp moveOp(std::string entityType, std::string id,
                    av::Vec3 position) {
  ScenePatchOp op;
  op.kind = ScenePatchOp::Kind::Move;
  op.entityType = std::move(entityType);
  op.id = std::move(id);
  op.position = position;
  return op;
}

ScenePatchOp setPowerOp(std::string id, double powerDb) {
  ScenePatchOp op;
  op.kind = ScenePatchOp::Kind::SetPower;
  op.entityType = "source";
  op.id = std::move(id);
  op.powerDb = powerDb;
  return op;
}

TEST(SceneStoreTest, CreateAssignsIdAndVersionOne) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());
  EXPECT_FALSE(created.sceneId.empty());
  EXPECT_EQ(created.sceneVersion, 1u);
  EXPECT_EQ(created.scenario.name, "scene-store-demo");

  const std::optional<SceneSnapshot> loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->sceneId, created.sceneId);
  EXPECT_EQ(loaded->sceneVersion, 1u);
  EXPECT_EQ(loaded->scenario.sources.size(), 2u);
}

TEST(SceneStoreTest, CreateGivesUniqueIds) {
  av::api::SceneStore store;
  const SceneSnapshot a = store.create(sampleScenario());
  const SceneSnapshot b = store.create(sampleScenario());
  EXPECT_NE(a.sceneId, b.sceneId);
}

TEST(SceneStoreTest, GetUnknownSceneIsEmpty) {
  av::api::SceneStore store;
  EXPECT_FALSE(store.get("scene-nope").has_value());
}

TEST(SceneStoreTest, MoveSourceUpdatesPositionAndBumpsVersion) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());

  const PatchResult result =
      store.patch(created.sceneId,
                  {moveOp("source", "src-1", av::Vec3{9.0, 9.0, 9.0})});
  EXPECT_TRUE(result.ok);
  EXPECT_EQ(result.sceneVersion, 2u);
  EXPECT_EQ(result.applied, 1u);

  const SceneSnapshot loaded = *store.get(created.sceneId);
  EXPECT_EQ(loaded.sceneVersion, 2u);
  ASSERT_EQ(loaded.scenario.sources.size(), 2u);
  EXPECT_EQ(loaded.scenario.sources[0].position.x, 9.0);
  EXPECT_EQ(loaded.scenario.sources[0].position.y, 9.0);
  EXPECT_EQ(loaded.scenario.sources[0].position.z, 9.0);
  // Untouched entity stays put.
  EXPECT_EQ(loaded.scenario.sources[1].position.x, 2.0);
}

TEST(SceneStoreTest, MoveReceiverUpdatesPosition) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());

  const PatchResult result =
      store.patch(created.sceneId,
                  {moveOp("receiver", "rcv-1", av::Vec3{0.5, 1.2, 2.5})});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.sceneVersion, 2u);

  const SceneSnapshot loaded = *store.get(created.sceneId);
  ASSERT_EQ(loaded.scenario.receivers.size(), 1u);
  EXPECT_EQ(loaded.scenario.receivers[0].position.x, 0.5);
  EXPECT_EQ(loaded.scenario.receivers[0].position.y, 1.2);
  EXPECT_EQ(loaded.scenario.receivers[0].position.z, 2.5);
}

TEST(SceneStoreTest, SetPowerUpdatesPowerDb) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());

  const PatchResult result =
      store.patch(created.sceneId, {setPowerOp("src-2", 42.5)});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.sceneVersion, 2u);
  EXPECT_EQ(result.applied, 1u);

  const SceneSnapshot loaded = *store.get(created.sceneId);
  EXPECT_EQ(loaded.scenario.sources[1].powerDb, 42.5);
  EXPECT_EQ(loaded.scenario.sources[0].powerDb, 80.0);
}

TEST(SceneStoreTest, MultiOpPatchBumpsVersionOnce) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());

  const PatchResult result =
      store.patch(created.sceneId,
                  {moveOp("source", "src-1", av::Vec3{5.0, 5.0, 5.0}),
                   moveOp("receiver", "rcv-1", av::Vec3{6.0, 6.0, 6.0}),
                   setPowerOp("src-1", 55.0)});
  ASSERT_TRUE(result.ok);
  EXPECT_EQ(result.sceneVersion, 2u);  // one bump per patch, not per op
  EXPECT_EQ(result.applied, 3u);

  const SceneSnapshot loaded = *store.get(created.sceneId);
  EXPECT_EQ(loaded.sceneVersion, 2u);
  EXPECT_EQ(loaded.scenario.sources[0].position.x, 5.0);
  EXPECT_EQ(loaded.scenario.receivers[0].position.x, 6.0);
  EXPECT_EQ(loaded.scenario.sources[0].powerDb, 55.0);
}

TEST(SceneStoreTest, SequentialPatchesAccumulateVersions) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());
  ASSERT_TRUE(store
                  .patch(created.sceneId,
                         {moveOp("source", "src-1", av::Vec3{1, 1, 1})})
                  .ok);
  ASSERT_TRUE(store
                  .patch(created.sceneId,
                         {moveOp("source", "src-1", av::Vec3{2, 2, 2})})
                  .ok);
  const SceneSnapshot loaded = *store.get(created.sceneId);
  EXPECT_EQ(loaded.sceneVersion, 3u);
  EXPECT_EQ(loaded.scenario.sources[0].position.x, 2.0);
}

TEST(SceneStoreTest, UnknownEntityRejectsWholePatch) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());

  const PatchResult result =
      store.patch(created.sceneId,
                  {moveOp("source", "src-1", av::Vec3{5.0, 5.0, 5.0}),
                   moveOp("source", "src-nope", av::Vec3{1, 1, 1})});
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("src-nope"), std::string::npos);
  EXPECT_EQ(result.applied, 0u);

  // All-or-nothing: the valid first op must not survive the rejection.
  const SceneSnapshot loaded = *store.get(created.sceneId);
  EXPECT_EQ(loaded.sceneVersion, 1u);
  EXPECT_EQ(loaded.scenario.sources[0].position.x, 1.0);
}

TEST(SceneStoreTest, PatchUnknownSceneFails) {
  av::api::SceneStore store;
  const PatchResult result =
      store.patch("scene-nope", {moveOp("source", "src-1", av::Vec3{1, 1, 1})});
  EXPECT_FALSE(result.ok);
  EXPECT_NE(result.error.find("scene-nope"), std::string::npos);
}

TEST(SceneStoreTest, ParsePatchOpsAcceptsMoveAndSetPower) {
  const nlohmann::json ops = nlohmann::json::parse(R"([
    {"op":"move","entityType":"source","id":"src-1","position":[1,2,3]},
    {"op":"move","entityType":"receiver","id":"rcv-1","position":[4,5,6]},
    {"op":"setPower","entityType":"source","id":"src-1","powerDb":72.5}
  ])");
  std::vector<ScenePatchOp> parsed;
  ASSERT_EQ(av::api::parsePatchOps(ops, parsed), "");
  ASSERT_EQ(parsed.size(), 3u);
  EXPECT_EQ(parsed[0].kind, ScenePatchOp::Kind::Move);
  EXPECT_EQ(parsed[0].entityType, "source");
  EXPECT_EQ(parsed[0].id, "src-1");
  EXPECT_EQ(parsed[0].position.y, 2.0);
  EXPECT_EQ(parsed[1].entityType, "receiver");
  EXPECT_EQ(parsed[2].kind, ScenePatchOp::Kind::SetPower);
  EXPECT_EQ(parsed[2].powerDb, 72.5);
}

TEST(SceneStoreTest, ParsePatchOpsRejectsUnknownOp) {
  const nlohmann::json ops = nlohmann::json::parse(
      R"([{"op":"rotate","entityType":"source","id":"src-1","yawDeg":30}])");
  std::vector<ScenePatchOp> parsed;
  const std::string error = av::api::parsePatchOps(ops, parsed);
  EXPECT_NE(error.find("rotate"), std::string::npos);
  EXPECT_TRUE(parsed.empty());
}

TEST(SceneStoreTest, ParsePatchOpsRejectsBadShapes) {
  std::vector<ScenePatchOp> parsed;

  EXPECT_NE(av::api::parsePatchOps(nlohmann::json::object(), parsed)
                .find("array"),
            std::string::npos);

  EXPECT_NE(av::api::parsePatchOps(nlohmann::json::parse(R"([
    {"op":"move","entityType":"source","id":"src-1","position":[1,2]}
  ])"),
                                   parsed)
                .find("position"),
            std::string::npos);

  EXPECT_NE(av::api::parsePatchOps(nlohmann::json::parse(R"([
    {"op":"move","entityType":"wall","id":"src-1","position":[1,2,3]}
  ])"),
                                   parsed)
                .find("entityType"),
            std::string::npos);

  EXPECT_NE(av::api::parsePatchOps(nlohmann::json::parse(R"([
    {"op":"setPower","entityType":"receiver","id":"rcv-1","powerDb":1}
  ])"),
                                   parsed)
                .find("setPower"),
            std::string::npos);

  EXPECT_NE(av::api::parsePatchOps(nlohmann::json::parse(R"([
    {"op":"setPower","entityType":"source","id":"src-1","powerDb":"loud"}
  ])"),
                                   parsed)
                .find("powerDb"),
            std::string::npos);

  EXPECT_NE(av::api::parsePatchOps(nlohmann::json::parse(R"([
    {"op":"move","entityType":"source","position":[1,2,3]}
  ])"),
                                   parsed)
                .find("id"),
            std::string::npos);
}

TEST(SceneStoreTest, GetReturnsIndependentCopy) {
  av::api::SceneStore store;
  const SceneSnapshot created = store.create(sampleScenario());
  SceneSnapshot copy = *store.get(created.sceneId);
  copy.scenario.sources[0].position = av::Vec3{99, 99, 99};
  copy.sceneVersion = 999;

  const SceneSnapshot loaded = *store.get(created.sceneId);
  EXPECT_EQ(loaded.sceneVersion, 1u);
  EXPECT_EQ(loaded.scenario.sources[0].position.x, 1.0);
}

}  // namespace
