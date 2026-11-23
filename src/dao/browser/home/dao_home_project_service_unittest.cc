// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_project_service.h"

#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/task_environment.h"
#include "base/test/test_future.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(DaoHomeProjectServiceTest, CollectionCacheBoundsExpiryAndScope) {
  base::test::TaskEnvironment time(
      base::test::TaskEnvironment::TimeSource::MOCK_TIME);
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  DaoHomeProjectService service(profile.GetPath());
  HomeCollectionKey key{"", "version", "feed", "fingerprint", "input"};
  const base::Value value("fixture");
  service.CacheCollection(key, value);
  auto draft = key;
  draft.draft_id = "draft";
  EXPECT_FALSE(service.GetCachedCollection(draft));
  auto changed = key;
  changed.fingerprint = "changed";
  EXPECT_FALSE(service.GetCachedCollection(changed));
  service.CacheCollection(draft, value);
  service.EndDraftPreview("draft");
  EXPECT_FALSE(service.GetCachedCollection(draft));
  EXPECT_EQ(service.GetCachedCollection(key), value);
  time.FastForwardBy(base::Minutes(10));
  EXPECT_FALSE(service.GetCachedCollection(key));

  for (int i = 0; i < 64; ++i) {
    key.input_hash = base::NumberToString(i);
    service.CacheCollection(key, value);
  }
  key.input_hash = "0";
  EXPECT_EQ(service.GetCachedCollection(key), value);
  key.input_hash = "64";
  service.CacheCollection(key, value);
  key.input_hash = "1";
  EXPECT_FALSE(service.GetCachedCollection(key));
  key.input_hash = "0";
  EXPECT_EQ(service.GetCachedCollection(key), value);

  base::Value large(std::string(4 * 1024 * 1024, 'a'));
  service.CacheCollection(key, large);
  service.CacheCollection(draft, large);
  EXPECT_FALSE(service.GetCachedCollection(key));
  service.CacheCollection(draft, base::Value("dao-media:fixture"));
  EXPECT_FALSE(service.GetCachedCollection(draft));
}

TEST(DaoHomeProjectServiceTest, MutationsInvalidateCachedCollections) {
  base::test::TaskEnvironment time;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  DaoHomeProjectService service(profile.GetPath());
  HomeFileSnapshot files = {
      {"manifest.json",
       R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[]})"},
      {"index.html", "<main>Home</main>"}};
  HomeCollectionKey key{"", "", "feed", "fingerprint", "input"};
  std::string first_revision;
  for (int i = 0; i < 2; ++i) {
    files["index.html"] += " ";
    base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
    service.StageSnapshot(key.revision, files, "Edit Home",
                          staged.GetCallback());
    ASSERT_TRUE(staged.Get().has_value());
    service.CacheCollection(key, base::Value("before publish"));
    base::test::TestFuture<base::expected<HomeVersion, HomeError>> published;
    service.Publish(staged.Get()->id, HomeRevisionKind::kUserRequest,
                    published.GetCallback(), nullptr);
    ASSERT_TRUE(published.Get().has_value());
    EXPECT_FALSE(service.GetCachedCollection(key));
    key.revision = published.Get()->id;
    if (i == 0) {
      first_revision = key.revision;
    }
  }
  service.CacheCollection(key, base::Value("before discard"));
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
  service.StageSnapshot(key.revision, files, "Draft Home",
                        staged.GetCallback());
  ASSERT_TRUE(staged.Get().has_value());
  auto draft = key;
  draft.draft_id = staged.Get()->id;
  service.CacheCollection(draft, base::Value("draft"));
  base::test::TestFuture<base::expected<void, HomeError>> discarded;
  service.DiscardDraft(draft.draft_id, discarded.GetCallback());
  ASSERT_TRUE(discarded.Get().has_value());
  EXPECT_FALSE(service.GetCachedCollection(draft));
  EXPECT_TRUE(service.GetCachedCollection(key));

  base::test::TestFuture<base::expected<HomeVersion, HomeError>> rollback;
  service.Rollback(key.revision, first_revision, "Restore Home",
                   rollback.GetCallback(), nullptr);
  ASSERT_TRUE(rollback.Get().has_value());
  EXPECT_FALSE(service.GetCachedCollection(key));
  key.revision = rollback.Get()->id;
  service.CacheCollection(key, base::Value("before reset"));
  base::test::TestFuture<base::expected<void, HomeError>> reset;
  service.Reset(key.revision, reset.GetCallback(), nullptr);
  ASSERT_TRUE(reset.Get().has_value());
  EXPECT_FALSE(service.GetCachedCollection(key));
}

TEST(DaoHomeProjectServiceTest, InvalidatedPreviewCannotPublishSnapshot) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  DaoHomeProjectService service(profile.GetPath());
  HomeFileSnapshot files = {
      {"manifest.json",
       R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[]})"},
      {"index.html", "<main>Home</main>"}};
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
  service.StageSnapshot("", files, "Stage Home", staged.GetCallback());
  ASSERT_TRUE(staged.Get().has_value());
  HomeDraft draft = staged.Get().value();
  service.MarkDraftPreviewed(draft.id);
  service.InvalidateDraftPreview(draft.id);
  base::test::TestFuture<base::expected<HomeVersion, HomeError>> rejected;
  service.PublishPreviewedDraft(draft, HomeRevisionKind::kInitial,
                                rejected.GetCallback(), nullptr);
  EXPECT_EQ(HomeError::kPermissionRequired, rejected.Get().error());
  service.MarkDraftPreviewed(draft.id);
  base::test::TestFuture<base::expected<HomeVersion, HomeError>> published;
  service.PublishPreviewedDraft(draft, HomeRevisionKind::kInitial,
                                published.GetCallback(), nullptr);
  ASSERT_TRUE(published.Get().has_value());
  base::test::TestFuture<base::expected<HomeFileSnapshot, HomeError>> snapshot;
  service.GetFileSnapshot(published.Get()->id, snapshot.GetCallback());
  ASSERT_TRUE(snapshot.Get().has_value());
  EXPECT_EQ(files, snapshot.Get().value());
}

TEST(DaoHomeProjectServiceTest, ReusesOnlyCurrentSourceGrantsForDraftRuntime) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  const HomeFileSnapshot files = {
      {"manifest.json",
       R"({"format_version":1,"entry":"index.html","routes":["/"],
        "connectors":[{"id":"feed","module":"connectors/feed.js",
          "schema":"schemas/feed.json","permissions":{
            "origins":["https://fixture.test"],"paths":["/feed"],
            "capabilities":["read_dom"],"mode":"read"}}]})"},
      {"index.html", "<main>Home</main>"},
      {"connectors/feed.js", "export default {collect: async () => []};"},
      {"schemas/feed.json", R"({"type":"array"})"}};
  std::string revision;
  {
    DaoHomeProjectStore store(profile.GetPath());
    ASSERT_TRUE(store.Initialize().has_value());
    auto staged = store.StageSnapshot("", files, "Create Home");
    ASSERT_TRUE(staged.has_value());
    auto published = store.PublishWithGrant(
        staged->id, "feed", HomeRevisionKind::kInitial, nullptr);
    ASSERT_TRUE(published.has_value());
    revision = published->id;
  }
  DaoHomeProjectService service(profile.GetPath());
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
  service.StageSnapshot(revision, files, "Edit Home", staged.GetCallback());
  ASSERT_TRUE(staged.Get().has_value());
  const std::string draft_id = staged.Get()->id;
  EXPECT_FALSE(service.IsDraftConnectorApproved(draft_id, "feed"));
  base::test::TestFuture<base::expected<void, HomeError>> approved;
  service.AuthorizeGrantedDraftConnectors(draft_id, approved.GetCallback());
  ASSERT_TRUE(approved.Get().has_value());
  EXPECT_TRUE(service.IsDraftConnectorApproved(draft_id, "feed"));
  HomeCollectionKey key{"", revision, "feed", "fingerprint", "input"};
  service.CacheCollection(key, base::Value("published result"));
  auto other = key;
  other.connector_id = "other";
  service.CacheCollection(other, base::Value("other source"));
  auto draft_key = key;
  draft_key.draft_id = draft_id;
  service.CacheCollection(draft_key, base::Value("draft result"));
  base::test::TestFuture<base::expected<void, HomeError>> revoked;
  service.RevokeConnector("feed", revoked.GetCallback());
  ASSERT_TRUE(revoked.Get().has_value());
  EXPECT_FALSE(service.GetCachedCollection(key));
  EXPECT_FALSE(service.GetCachedCollection(draft_key));
  EXPECT_TRUE(service.GetCachedCollection(other));
  EXPECT_FALSE(service.IsDraftConnectorApproved(draft_id, "feed"));
  base::test::TestFuture<base::expected<void, HomeError>> rechecked;
  service.AuthorizeGrantedDraftConnectors(draft_id, rechecked.GetCallback());
  ASSERT_TRUE(rechecked.Get().has_value());
  EXPECT_FALSE(service.IsDraftConnectorApproved(draft_id, "feed"));
}

TEST(DaoHomeProjectServiceTest, ChangedSourceRequiresTestBeforePublication) {
  base::test::TaskEnvironment task_environment;
  base::ScopedTempDir profile;
  ASSERT_TRUE(profile.CreateUniqueTempDir());
  HomeFileSnapshot files = {
      {"manifest.json",
       R"({"format_version":1,"entry":"index.html","routes":["/"],
        "connectors":[{"id":"feed","module":"connectors/feed.js",
          "schema":"schemas/feed.json","permissions":{
            "origins":["https://fixture.test"],"paths":["/feed"],
            "capabilities":["read_dom"],"mode":"read"}}]})"},
      {"index.html", "<main>Home</main>"},
      {"connectors/feed.js", "export default {collect: async () => []};"},
      {"schemas/feed.json", R"({"type":"array"})"}};
  std::string revision;
  {
    DaoHomeProjectStore store(profile.GetPath());
    ASSERT_TRUE(store.Initialize().has_value());
    auto staged = store.StageSnapshot("", files, "Create Home");
    ASSERT_TRUE(staged.has_value());
    auto published = store.PublishWithGrant(
        staged->id, "feed", HomeRevisionKind::kInitial, nullptr);
    ASSERT_TRUE(published.has_value());
    revision = published->id;
  }
  DaoHomeProjectService service(profile.GetPath());
  for (const bool changed_source : {false, true}) {
    if (changed_source)
      files["connectors/feed.js"] += "\n";
    files["index.html"] += "\n";
    base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
    service.StageSnapshot(revision, files, "Edit Home", staged.GetCallback());
    ASSERT_TRUE(staged.Get().has_value());
    const HomeDraft draft = *staged.Get();
    base::test::TestFuture<base::expected<void, HomeError>> approved;
    service.AuthorizeGrantedDraftConnectors(draft.id, approved.GetCallback());
    ASSERT_TRUE(approved.Get().has_value());
    service.MarkDraftPreviewed(draft.id);
    base::test::TestFuture<base::expected<HomeVersion, HomeError>> published;
    service.PublishApprovedDraft(draft, HomeRevisionKind::kUserRequest,
                                 published.GetCallback(), nullptr);
    if (!changed_source) {
      ASSERT_TRUE(published.Get().has_value());
      revision = published.Get()->id;
    } else {
      EXPECT_EQ(HomeError::kPermissionRequired, published.Get().error());
      ASSERT_TRUE(
          service.MarkDraftConnectorTested(draft.id, "feed").has_value());
      base::test::TestFuture<base::expected<HomeVersion, HomeError>> tested;
      service.PublishApprovedDraft(draft, HomeRevisionKind::kUserRequest,
                                   tested.GetCallback(), nullptr);
      EXPECT_TRUE(tested.Get().has_value());
    }
  }
}

}  // namespace
}  // namespace dao
