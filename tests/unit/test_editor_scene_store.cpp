#include <gtest/gtest.h>

#include <av/scene/scene_model.hpp>

#include <string>
#include <utility>

#include "editor_scene_store.hpp"

namespace {

av::Scene makeScene(std::string id = "doc-1") {
  av::Scene scene;
  scene.id = std::move(id);
  scene.units = "m";
  return scene;
}

av::Mesh makeMesh() {
  av::Mesh mesh;
  mesh.positions = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
  mesh.triangles.push_back(av::Triangle{0, 1, 2, 0});
  mesh.materialSlots = {"mat-a"};
  return mesh;
}

av::MeshReport makeReport() {
  av::MeshReport report;
  report.stats.vertices = 3;
  report.stats.triangles = 1;
  report.stats.zones = {"mat-a"};
  return report;
}

}  // namespace

TEST(EditorSceneStoreTest, CreateAssignsIdAndVersionOne) {
  av::api::EditorSceneStore store;
  const av::api::EditorSceneRecord record = store.create(makeScene());
  EXPECT_FALSE(record.sceneId.empty());
  EXPECT_EQ(record.sceneVersion, 1u);
  EXPECT_EQ(record.scene.id, "doc-1");
  EXPECT_FALSE(record.mesh.has_value());
  EXPECT_FALSE(record.report.has_value());
  EXPECT_FALSE(record.meshJob.has_value());
}

TEST(EditorSceneStoreTest, CreateGivesUniqueIds) {
  av::api::EditorSceneStore store;
  const auto a = store.create(makeScene());
  const auto b = store.create(makeScene());
  EXPECT_NE(a.sceneId, b.sceneId);
}

TEST(EditorSceneStoreTest, GetUnknownSceneIsEmpty) {
  av::api::EditorSceneStore store;
  EXPECT_FALSE(store.get("nope").has_value());
}

TEST(EditorSceneStoreTest, GetReturnsIndependentCopy) {
  av::api::EditorSceneStore store;
  const auto created = store.create(makeScene());
  auto loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  loaded->scene.id = "mutated";
  const auto again = store.get(created.sceneId);
  ASSERT_TRUE(again.has_value());
  EXPECT_EQ(again->scene.id, "doc-1");
}

TEST(EditorSceneStoreTest, ReplaceBumpsVersionAndDropsMesh) {
  av::api::EditorSceneStore store;
  const auto created = store.create(makeScene());
  ASSERT_TRUE(store.beginMeshJob(created.sceneId, "meshjob-1"));
  ASSERT_TRUE(
      store.finishMeshJob(created.sceneId, "meshjob-1", makeMesh(), makeReport()));

  av::Scene updated = makeScene("doc-2");
  const auto version = store.replace(created.sceneId, std::move(updated));
  ASSERT_TRUE(version.has_value());
  EXPECT_EQ(*version, 2u);

  const auto loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->sceneVersion, 2u);
  EXPECT_EQ(loaded->scene.id, "doc-2");
  EXPECT_FALSE(loaded->mesh.has_value());
  EXPECT_FALSE(loaded->report.has_value());
  EXPECT_FALSE(loaded->meshJob.has_value());
}

TEST(EditorSceneStoreTest, ReplaceUnknownSceneFails) {
  av::api::EditorSceneStore store;
  EXPECT_FALSE(store.replace("nope", makeScene()).has_value());
}

TEST(EditorSceneStoreTest, MeshJobLifecycle) {
  av::api::EditorSceneStore store;
  const auto created = store.create(makeScene());

  EXPECT_TRUE(store.beginMeshJob(created.sceneId, "meshjob-1"));
  auto loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  ASSERT_TRUE(loaded->meshJob.has_value());
  EXPECT_EQ(loaded->meshJob->jobId, "meshjob-1");
  EXPECT_EQ(loaded->meshJob->state, av::JobState::Running);
  EXPECT_FALSE(loaded->mesh.has_value());

  EXPECT_TRUE(
      store.finishMeshJob(created.sceneId, "meshjob-1", makeMesh(), makeReport()));
  loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->meshJob->state, av::JobState::Succeeded);
  EXPECT_TRUE(loaded->mesh.has_value());
  EXPECT_TRUE(loaded->report.has_value());
  EXPECT_EQ(loaded->report->stats.triangles, 1u);
}

TEST(EditorSceneStoreTest, FailMeshJobKeepsError) {
  av::api::EditorSceneStore store;
  const auto created = store.create(makeScene());
  ASSERT_TRUE(store.beginMeshJob(created.sceneId, "meshjob-1"));
  EXPECT_TRUE(store.failMeshJob(created.sceneId, "meshjob-1", "boom"));
  const auto loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->meshJob->state, av::JobState::Failed);
  EXPECT_EQ(loaded->meshJob->error, "boom");
  EXPECT_FALSE(loaded->mesh.has_value());
}

TEST(EditorSceneStoreTest, StaleMeshJobDoesNotClobberNewerGeneration) {
  av::api::EditorSceneStore store;
  const auto created = store.create(makeScene());
  ASSERT_TRUE(store.beginMeshJob(created.sceneId, "meshjob-1"));
  ASSERT_TRUE(store.beginMeshJob(created.sceneId, "meshjob-2"));
  // Worker 1 finishes late — must not overwrite the current job's outcome.
  EXPECT_FALSE(
      store.finishMeshJob(created.sceneId, "meshjob-1", makeMesh(), makeReport()));
  EXPECT_TRUE(
      store.finishMeshJob(created.sceneId, "meshjob-2", makeMesh(), makeReport()));
  const auto loaded = store.get(created.sceneId);
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->meshJob->jobId, "meshjob-2");
  EXPECT_EQ(loaded->meshJob->state, av::JobState::Succeeded);
}

TEST(EditorSceneStoreTest, MeshJobOnUnknownSceneFails) {
  av::api::EditorSceneStore store;
  EXPECT_FALSE(store.beginMeshJob("nope", "meshjob-1"));
  EXPECT_FALSE(store.finishMeshJob("nope", "meshjob-1", makeMesh(), makeReport()));
  EXPECT_FALSE(store.failMeshJob("nope", "meshjob-1", "x"));
}

TEST(EditorSceneStoreTest, MeshJobIdsUseTheirOwnNamespace) {
  const std::string id = av::api::makeMeshJobId();
  EXPECT_NE(id.find("meshjob-"), std::string::npos);
  EXPECT_NE(id, av::api::makeMeshJobId());
}
