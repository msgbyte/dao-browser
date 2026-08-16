// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_project_store.h"

#include <string>
#include <string_view>

#include "base/base64.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/stringprintf.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_manifest.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

constexpr char kInitialPatch[] = R"(*** Begin Patch
*** Add File: manifest.json
+{
+  "format_version": 1,
+  "entry": "index.html",
+  "routes": ["/", "/feed"],
+  "connectors": [{
+    "id": "fixture-feed",
+    "module": "connectors/feed.js",
+    "schema": "schemas/feed.json",
+    "permissions": {
+      "origins": ["https://fixture.test"],
+      "paths": ["/feed"],
+      "capabilities": ["read_dom"],
+      "mode": "read"
+    }
+  }],
+  "limits": {
+    "max_result_bytes": 524288,
+    "max_items_per_connector": 50
+  }
+}
*** Add File: index.html
+<!doctype html><main data-dao-node-id="main">Home</main>
*** Add File: connectors/feed.js
+export default {async collect(page) { return page.queryAll('.item'); }};
*** Add File: schemas/feed.json
+{"type":"array","items":{"type":"object"}}
*** Add File: dao/node-map.json
+{"main":{"file":"index.html","symbol":"main"}}
*** End Patch
)";

constexpr char kStylePatch[] = R"(*** Begin Patch
*** Add File: src/styles.css
+main { color: #1f2c38; }
*** End Patch
)";

constexpr char kIncreaseBudgetPatch[] = R"(*** Begin Patch
*** Update File: manifest.json
@@
-    "max_result_bytes": 524288,
-    "max_items_per_connector": 50
+    "max_result_bytes": 1048576,
+    "max_items_per_connector": 100
*** End of File
*** End Patch
)";

constexpr char kDecreaseBudgetPatch[] = R"(*** Begin Patch
*** Update File: manifest.json
@@
-    "max_result_bytes": 1048576,
-    "max_items_per_connector": 100
+    "max_result_bytes": 262144,
+    "max_items_per_connector": 25
*** End of File
*** End Patch
)";

constexpr char kExpandTwoConnectorsPatch[] = R"(*** Begin Patch
*** Update File: manifest.json
@@
-      "paths": ["/feed"],
+      "paths": ["/"],
@@
-  }],
+  }, {
+    "id": "fixture-profile",
+    "module": "connectors/profile.js",
+    "schema": "schemas/profile.json",
+    "permissions": {
+      "origins": ["https://profile.test"],
+      "paths": ["/profile"],
+      "capabilities": ["read_dom"],
+      "mode": "read"
+    }
+  }],
*** End of File
*** Add File: connectors/profile.js
+export default {async collect(page) { return page.query('.profile'); }};
*** Add File: schemas/profile.json
+{"type":"object"}
*** End Patch
)";

std::string ConnectorProjectPatch(
    std::string_view connector_id,
    std::string_view module_source,
    std::string_view schema_source,
    std::string_view origins,
    std::string_view paths,
    std::string_view capabilities,
    int max_result_bytes = 524288,
    int max_items_per_connector = 50,
    std::string_view experience =
        R"({"kind":"start_surface","primary_actions":["fixture-feed"],"source_slots":["fixture-feed"]})") {
  const std::string connector_id_string(connector_id);
  const std::string module_source_string(module_source);
  const std::string schema_source_string(schema_source);
  const std::string origins_string(origins);
  const std::string paths_string(paths);
  const std::string capabilities_string(capabilities);
  const std::string experience_string(experience);
  return base::StringPrintf(
      R"(*** Begin Patch
*** Add File: manifest.json
+{
+  "format_version": 1,
+  "entry": "index.html",
+  "routes": ["/", "/feed"],
+  "connectors": [{
+    "id": "%s",
+    "module": "connectors/feed.js",
+    "schema": "schemas/feed.json",
+    "permissions": {
+      "origins": [%s],
+      "paths": [%s],
+      "capabilities": [%s],
+      "mode": "read"
+    }
+  }],
+  "limits": {
+    "max_result_bytes": %d,
+    "max_items_per_connector": %d
+  }
+}
*** Add File: index.html
+<!doctype html><main data-dao-node-id="main">Home</main>
*** Add File: connectors/feed.js
+%s
*** Add File: schemas/feed.json
+%s
*** Add File: experience.json
+%s
*** End Patch
)",
      connector_id_string.c_str(), origins_string.c_str(), paths_string.c_str(),
      capabilities_string.c_str(), max_result_bytes, max_items_per_connector,
      module_source_string.c_str(), schema_source_string.c_str(),
      experience_string.c_str());
}

class DaoHomeProjectStoreTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(profile_dir_.CreateUniqueTempDir());
    store_ = std::make_unique<DaoHomeProjectStore>(profile_dir_.GetPath());
    ASSERT_TRUE(store_->Initialize().has_value());
  }

  HomeVersion PublishInitialProject() {
    auto draft =
        store_->ApplyPatch(/*base_revision=*/"", kInitialPatch, "Create Home");
    if (!draft.has_value()) {
      ADD_FAILURE() << "ApplyPatch failed with HomeError "
                    << static_cast<int>(draft.error());
      return HomeVersion();
    }
    auto version =
        store_->Publish(draft->id, HomeRevisionKind::kInitial, nullptr);
    if (!version.has_value()) {
      ADD_FAILURE() << "Publish failed with HomeError "
                    << static_cast<int>(version.error());
      return HomeVersion();
    }
    return std::move(version.value());
  }

  base::ScopedTempDir profile_dir_;
  std::unique_ptr<DaoHomeProjectStore> store_;
};

TEST_F(DaoHomeProjectStoreTest, RejectsUnsafeManifestAndProjectPaths) {
  constexpr char kTraversalPatch[] = R"(*** Begin Patch
*** Add File: ../outside.html
+escaped
*** End Patch
)";
  auto traversal =
      store_->ApplyPatch(/*base_revision=*/"", kTraversalPatch, "Escape");
  ASSERT_FALSE(traversal.has_value());
  EXPECT_EQ(HomeError::kInvalidPath, traversal.error());
  EXPECT_FALSE(
      base::PathExists(profile_dir_.GetPath().AppendASCII("outside.html")));

  constexpr char kBadManifestPatch[] = R"(*** Begin Patch
*** Add File: manifest.json
+{"format_version":2,"entry":"index.html","routes":["/"]}
*** Add File: index.html
+bad version
*** End Patch
)";
  auto bad_manifest = store_->ApplyPatch(/*base_revision=*/"",
                                         kBadManifestPatch, "Bad manifest");
  ASSERT_FALSE(bad_manifest.has_value());
  EXPECT_EQ(HomeError::kUnsupportedFormat, bad_manifest.error());
}

TEST_F(DaoHomeProjectStoreTest, PublishesValidatedRevisionAtomically) {
  HomeVersion initial = PublishInitialProject();

  HomeSnapshot snapshot = store_->GetSnapshot();
  ASSERT_TRUE(snapshot.has_project);
  EXPECT_EQ(initial.id, snapshot.revision);
  ASSERT_TRUE(snapshot.manifest.has_value());
  EXPECT_EQ("index.html", snapshot.manifest->entry);
  EXPECT_EQ((std::vector<std::string>{"/", "/feed"}),
            snapshot.manifest->routes);

  auto body = store_->ReadFile(initial.id, "index.html");
  ASSERT_TRUE(body.has_value());
  EXPECT_EQ("<!doctype html><main data-dao-node-id=\"main\">Home</main>\n",
            body.value());
}

TEST_F(DaoHomeProjectStoreTest, PreparesCanonicalHistoryBootstrapConnectors) {
  auto draft =
      store_->ApplyPatch(/*base_revision=*/"", kInitialPatch, "Create Home");
  ASSERT_TRUE(draft.has_value());

  HomeBootstrapBrief brief;
  HomeLaunchTarget target;
  target.id = "bilibili";
  target.label_hint = "Bilibili";
  target.url = GURL("https://www.bilibili.com/");
  target.category_hint = "video";
  target.source_eligibility = HomeSourceEligibility::kLaunchAndFeed;
  brief.launch_targets.push_back(std::move(target));
  HomeSourceCandidate candidate;
  candidate.launch_target_id = "bilibili";
  candidate.connector_kind_hint = "page_feed";
  candidate.collection_url = GURL("https://t.bilibili.com/");
  candidate.content_intent = "following_feed";
  candidate.content_kinds = {"video"};
  candidate.schema_source = R"({"type":"array"})";
  brief.source_candidates.push_back(std::move(candidate));

  constexpr char kAuthoredModule[] =
      "export default {async collect(page) { return page.queryAll("
      "'.feed-card, article', {title:['.title','text'], "
      "url:[\"a[href*='/video/']\",'href']}); }};";
  const base::FilePath module_path = store_->root_for_testing()
                                         .AppendASCII(".tmp")
                                         .AppendASCII(draft->id)
                                         .AppendASCII("connectors/bilibili.js");
  ASSERT_TRUE(base::CreateDirectory(module_path.DirName()));
  ASSERT_TRUE(base::WriteFile(module_path, kAuthoredModule));

  auto prepared =
      store_->PrepareHistoryBootstrapDraft(draft->id, brief, {"bilibili"});

  ASSERT_TRUE(prepared.has_value());
  ASSERT_EQ(1u, prepared->manifest.connectors.size());
  const HomeConnector& connector = prepared->manifest.connectors.front();
  EXPECT_EQ("bilibili", connector.id);
  EXPECT_EQ("connectors/bilibili.js", connector.module);
  EXPECT_EQ("schemas/bilibili.json", connector.schema);
  ASSERT_EQ(1u, connector.permissions.origins.size());
  EXPECT_EQ("https://t.bilibili.com",
            connector.permissions.origins.front().Serialize());
  EXPECT_EQ((std::vector<std::string>{"/"}), connector.permissions.paths);
  EXPECT_TRUE(connector.permissions.capabilities.contains(
      HomePageCapability::kReadDom));
  EXPECT_EQ((std::vector<std::string>{"bilibili"}),
            prepared->permission_expanded_connector_ids);
  auto module = store_->ReadDraftFile(draft->id, "connectors/bilibili.js");
  ASSERT_TRUE(module.has_value());
  EXPECT_EQ(kAuthoredModule, module.value());
  auto schema = store_->ReadDraftFile(draft->id, "schemas/bilibili.json");
  ASSERT_TRUE(schema.has_value());
  EXPECT_EQ(brief.source_candidates.front().schema_source, schema.value());

  auto final_draft =
      store_->ApplyPatch(/*base_revision=*/"", kInitialPatch, "Create final");
  ASSERT_TRUE(final_draft.has_value());
  auto prepared_final = store_->PrepareHistoryBootstrapFinalDraft(
      final_draft->id, draft->id, {"bilibili"});
  ASSERT_TRUE(prepared_final.has_value());
  ASSERT_EQ(1u, prepared_final->manifest.connectors.size());
  EXPECT_EQ("bilibili", prepared_final->manifest.connectors.front().id);
  auto final_module =
      store_->ReadDraftFile(final_draft->id, "connectors/bilibili.js");
  ASSERT_TRUE(final_module.has_value());
  EXPECT_EQ(kAuthoredModule, final_module.value());
  auto final_schema =
      store_->ReadDraftFile(final_draft->id, "schemas/bilibili.json");
  ASSERT_TRUE(final_schema.has_value());
  EXPECT_EQ(brief.source_candidates.front().schema_source,
            final_schema.value());

  constexpr char kUnscopedModule[] =
      "export default {async collect(page) { return "
      "page.queryAll('a[href]'); }};";
  ASSERT_TRUE(base::WriteFile(module_path, kUnscopedModule));
  auto unscoped =
      store_->PrepareHistoryBootstrapDraft(draft->id, brief, {"bilibili"});
  ASSERT_TRUE(unscoped.has_value());
  EXPECT_TRUE(unscoped->manifest.connectors.empty());

  constexpr char kConstrainedDocumentFallback[] =
      "export default {async collect(page) { return "
      "page.queryAll(\"a[href*='/video/']\"); }};";
  ASSERT_TRUE(base::WriteFile(module_path, kConstrainedDocumentFallback));
  auto constrained_document =
      store_->PrepareHistoryBootstrapDraft(draft->id, brief, {"bilibili"});
  ASSERT_TRUE(constrained_document.has_value());
  EXPECT_TRUE(constrained_document->manifest.connectors.empty());
}

TEST_F(DaoHomeProjectStoreTest,
       ResetDeletesProjectHistoryGrantsAndTemporaryDrafts) {
  HomeVersion initial = PublishInitialProject();
  ASSERT_TRUE(store_->GrantConnector("fixture-feed").has_value());
  auto draft = store_->ApplyPatch(initial.id, kStylePatch, "Pending edit");
  ASSERT_TRUE(draft.has_value());

  auto reset = store_->Reset(initial.id, nullptr);

  ASSERT_TRUE(reset.has_value());
  EXPECT_FALSE(store_->GetSnapshot().has_project);
  EXPECT_TRUE(store_->ListVersions().empty());
  EXPECT_FALSE(store_->HasGrant("fixture-feed"));
  EXPECT_FALSE(store_->GetDraft(draft->id).has_value());
  store_ = std::make_unique<DaoHomeProjectStore>(profile_dir_.GetPath());
  ASSERT_TRUE(store_->Initialize().has_value());
  EXPECT_FALSE(store_->GetSnapshot().has_project);
  EXPECT_TRUE(store_->ListVersions().empty());
}

TEST_F(DaoHomeProjectStoreTest,
       ReplaceFilesCreatesOneValidatedDraftWithoutMutatingPublishedRevision) {
  HomeVersion initial = PublishInitialProject();
  constexpr char kEntryReplacement[] =
      "<!doctype html><main data-dao-node-id=\"main\">Replaced Home</main>";
  constexpr char kConnectorReplacement[] =
      "export default {async collect() { return []; }};";

  auto draft = store_->ReplaceFiles(
      initial.id,
      {{"index.html", kEntryReplacement},
       {"connectors/feed.js", kConnectorReplacement}},
      "Replace related Home files");

  ASSERT_TRUE(draft.has_value());
  EXPECT_EQ(initial.id, draft->base_revision);
  auto draft_entry = store_->ReadDraftFile(draft->id, "index.html");
  ASSERT_TRUE(draft_entry.has_value());
  EXPECT_EQ(kEntryReplacement, draft_entry.value());
  auto draft_connector =
      store_->ReadDraftFile(draft->id, "connectors/feed.js");
  ASSERT_TRUE(draft_connector.has_value());
  EXPECT_EQ(kConnectorReplacement, draft_connector.value());
  auto published_entry = store_->ReadFile(initial.id, "index.html");
  ASSERT_TRUE(published_entry.has_value());
  EXPECT_NE(kEntryReplacement, published_entry.value());
  auto published_connector =
      store_->ReadFile(initial.id, "connectors/feed.js");
  ASSERT_TRUE(published_connector.has_value());
  EXPECT_NE(kConnectorReplacement, published_connector.value());
  EXPECT_EQ(initial.id, store_->GetSnapshot().revision);
}

TEST_F(DaoHomeProjectStoreTest, ReplaceFilesRejectsMissingOrInvalidTargets) {
  HomeVersion initial = PublishInitialProject();

  auto missing = store_->ReplaceFiles(initial.id,
                                      {{"missing.html", "missing"}},
                                      "Replace a missing file");
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(HomeError::kNotFound, missing.error());

  auto invalid = store_->ReplaceFiles(initial.id,
                                      {{"../index.html", "invalid"}},
                                      "Replace an invalid path");
  ASSERT_FALSE(invalid.has_value());
  EXPECT_EQ(HomeError::kInvalidPath, invalid.error());

  auto duplicate = store_->ReplaceFiles(
      initial.id, {{"index.html", "first"}, {"index.html", "second"}},
      "Replace a duplicate target");
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(HomeError::kInvalidArgument, duplicate.error());
}

TEST_F(DaoHomeProjectStoreTest, ResetRejectsInvalidatedMutationLease) {
  HomeVersion initial = PublishInitialProject();
  auto lease = base::MakeRefCounted<DaoHomeMutationLease>();
  lease->Invalidate();

  auto reset = store_->Reset(initial.id, lease);

  ASSERT_FALSE(reset.has_value());
  EXPECT_EQ(HomeError::kCancelled, reset.error());
  EXPECT_EQ(initial.id, store_->GetSnapshot().revision);
  EXPECT_EQ(1u, store_->ListVersions().size());
}

TEST_F(DaoHomeProjectStoreTest, RejectsInvalidatedMutationLeaseBeforeCommit) {
  auto draft =
      store_->ApplyPatch(/*base_revision=*/"", kInitialPatch, "Create Home");
  ASSERT_TRUE(draft.has_value());
  auto lease = base::MakeRefCounted<DaoHomeMutationLease>();
  lease->Invalidate();

  auto published =
      store_->Publish(draft->id, HomeRevisionKind::kInitial, lease);

  ASSERT_FALSE(published.has_value());
  EXPECT_EQ(HomeError::kCancelled, published.error());
  EXPECT_FALSE(store_->GetSnapshot().has_project);
  EXPECT_TRUE(store_->GetDraft(draft->id).has_value());
}

TEST_F(DaoHomeProjectStoreTest,
       PublishesPermissionExpansionAndGrantAtomically) {
  auto draft = store_->ApplyPatch(/*base_revision=*/"", kInitialPatch,
                                  "Create connected Home");
  ASSERT_TRUE(draft.has_value());
  ASSERT_TRUE(draft->permission_expansion);

  auto version = store_->PublishWithGrant(
      draft->id, "fixture-feed", HomeRevisionKind::kSourceConnection, nullptr);
  ASSERT_TRUE(version.has_value());
  EXPECT_TRUE(store_->HasGrant("fixture-feed"));
  EXPECT_EQ(version->id, store_->GetSnapshot().revision);

  auto invalid = store_->ApplyPatch(version->id, kStylePatch,
                                    "Add styles after permission");
  ASSERT_TRUE(invalid.has_value());
  auto rejected = store_->PublishWithGrant(
      invalid->id, "missing", HomeRevisionKind::kSourceConnection, nullptr);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(HomeError::kNotFound, rejected.error());
  EXPECT_EQ(version->id, store_->GetSnapshot().revision);
}

TEST_F(DaoHomeProjectStoreTest,
       TreatsBudgetIncreaseAsExpansionAndPreservesGrantOnDecrease) {
  auto initial_draft = store_->ApplyPatch(
      /*base_revision=*/"", kInitialPatch, "Create connected Home");
  ASSERT_TRUE(initial_draft.has_value());
  auto initial =
      store_->PublishWithGrant(initial_draft->id, "fixture-feed",
                               HomeRevisionKind::kSourceConnection, nullptr);
  ASSERT_TRUE(initial.has_value());
  ASSERT_TRUE(store_->HasGrant("fixture-feed"));

  auto increased = store_->ApplyPatch(initial->id, kIncreaseBudgetPatch,
                                      "Increase collection budget");
  ASSERT_TRUE(increased.has_value());
  EXPECT_TRUE(increased->permission_expansion);
  EXPECT_EQ((std::vector<std::string>{"fixture-feed"}),
            increased->permission_expanded_connector_ids);
  auto increased_version = store_->PublishWithGrants(
      increased->id, increased->permission_expanded_connector_ids,
      HomeRevisionKind::kSourceConnection, nullptr);
  ASSERT_TRUE(increased_version.has_value());
  ASSERT_TRUE(store_->HasGrant("fixture-feed"));

  auto decreased = store_->ApplyPatch(
      increased_version->id, kDecreaseBudgetPatch, "Reduce collection budget");
  ASSERT_TRUE(decreased.has_value());
  EXPECT_FALSE(decreased->permission_expansion);
  EXPECT_TRUE(decreased->permission_expanded_connector_ids.empty());
  auto decreased_version =
      store_->Publish(decreased->id, HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(decreased_version.has_value());
  EXPECT_TRUE(store_->HasGrant("fixture-feed"));
}

TEST_F(DaoHomeProjectStoreTest, TracksEveryExpandedConnectorInDraft) {
  HomeVersion initial = PublishInitialProject();

  auto draft = store_->ApplyPatch(initial.id, kExpandTwoConnectorsPatch,
                                  "Expand two connector scopes");
  ASSERT_TRUE(draft.has_value());
  EXPECT_TRUE(draft->permission_expansion);
  EXPECT_EQ((std::vector<std::string>{"fixture-feed", "fixture-profile"}),
            draft->permission_expanded_connector_ids);

  auto bundle = store_->GetDraftConnectorBundle(draft->id, "fixture-profile");
  ASSERT_TRUE(bundle.has_value());
  EXPECT_EQ("connectors/profile.js", bundle->connector.module);
  EXPECT_EQ(
      "export default {async collect(page) { return "
      "page.query('.profile'); }};\n",
      bundle->module_source);
}

TEST_F(DaoHomeProjectStoreTest,
       ConnectorAuthorizationFingerprintCoversExactExecutableScope) {
  auto authorization_for =
      [&](std::string_view connector_id, std::string_view module_source,
          std::string_view schema_source, std::string_view origins,
          std::string_view paths, std::string_view capabilities,
          int max_result_bytes = 524288, int max_items_per_connector = 50) {
        auto draft = store_->ApplyPatch(
            /*base_revision=*/"",
            ConnectorProjectPatch(connector_id, module_source, schema_source,
                                  origins, paths, capabilities,
                                  max_result_bytes, max_items_per_connector),
            "Create authorization fixture");
        EXPECT_TRUE(draft.has_value());
        if (!draft.has_value()) {
          return HomeConnectorAuthorization();
        }
        auto authorizations = store_->GetDraftConnectorAuthorizations(
            draft->id, {std::string(connector_id)});
        EXPECT_TRUE(authorizations.has_value());
        if (!authorizations.has_value() || authorizations->size() != 1u) {
          return HomeConnectorAuthorization();
        }
        return std::move(authorizations->front());
      };

  HomeConnectorAuthorization baseline = authorization_for(
      "fixture-feed", "export default {collect() { return []; }};",
      R"({"type":"array"})", R"("https://a.test","https://b.test")",
      R"("/feed","/items")", R"("read_dom","scroll")");
  ASSERT_FALSE(baseline.fingerprint.empty());
  EXPECT_EQ("fixture-feed", baseline.connector_id);
  EXPECT_EQ("fixture-feed", baseline.bundle.connector.id);
  EXPECT_EQ(64u, baseline.fingerprint.size());

  HomeConnectorAuthorization renamed = authorization_for(
      "renamed-feed", "export default {collect() { return []; }};",
      R"({"type":"array"})", R"("https://a.test","https://b.test")",
      R"("/feed","/items")", R"("read_dom","scroll")");
  EXPECT_EQ("renamed-feed", renamed.connector_id);
  EXPECT_EQ(baseline.fingerprint, renamed.fingerprint);

  EXPECT_NE(
      baseline.fingerprint,
      authorization_for(
          "fixture-feed", "export default {collect() { return ['changed']; }};",
          R"({"type":"array"})", R"("https://a.test","https://b.test")",
          R"("/feed","/items")", R"("read_dom","scroll")")
          .fingerprint);
  EXPECT_NE(baseline.fingerprint,
            authorization_for(
                "fixture-feed", "export default {collect() { return []; }};",
                R"({"type":"object"})", R"("https://a.test","https://b.test")",
                R"("/feed","/items")", R"("read_dom","scroll")")
                .fingerprint);
  EXPECT_NE(baseline.fingerprint,
            authorization_for(
                "fixture-feed", "export default {collect() { return []; }};",
                R"({"type":"array"})", R"("https://a.test","https://c.test")",
                R"("/feed","/items")", R"("read_dom","scroll")")
                .fingerprint);
  EXPECT_NE(baseline.fingerprint,
            authorization_for(
                "fixture-feed", "export default {collect() { return []; }};",
                R"({"type":"array"})", R"("https://a.test","https://b.test")",
                R"("/feed","/other")", R"("read_dom","scroll")")
                .fingerprint);
  EXPECT_NE(baseline.fingerprint,
            authorization_for(
                "fixture-feed", "export default {collect() { return []; }};",
                R"({"type":"array"})", R"("https://a.test","https://b.test")",
                R"("/feed","/items")", R"("read_dom","read_style")")
                .fingerprint);
  EXPECT_NE(baseline.fingerprint,
            authorization_for(
                "fixture-feed", "export default {collect() { return []; }};",
                R"({"type":"array"})", R"("https://a.test","https://b.test")",
                R"("/feed","/items")", R"("read_dom","scroll")", 1048576, 50)
                .fingerprint);
  EXPECT_NE(baseline.fingerprint,
            authorization_for(
                "fixture-feed", "export default {collect() { return []; }};",
                R"({"type":"array"})", R"("https://a.test","https://b.test")",
                R"("/feed","/items")", R"("read_dom","scroll")", 524288, 75)
                .fingerprint);

  HomeConnectorAuthorization reordered = authorization_for(
      "fixture-feed", "export default {collect() { return []; }};",
      R"({"type":"array"})", R"("https://b.test","https://a.test")",
      R"("/items","/feed")", R"("scroll","read_dom")");
  EXPECT_EQ(baseline.fingerprint, reordered.fingerprint);
}

TEST_F(DaoHomeProjectStoreTest,
       ConnectorAuthorizationFingerprintPreservesInt64BudgetPrecision) {
  HomeConnector connector;
  connector.permissions.origins.push_back(
      url::Origin::Create(GURL("https://fixture.test")));
  connector.permissions.paths.push_back("/feed");
  connector.permissions.capabilities.insert(HomePageCapability::kReadDom);
  HomeLimits lower;
  lower.max_result_bytes = 9007199254740992LL;
  lower.max_items_per_connector = 50;
  HomeLimits upper = lower;
  upper.max_result_bytes = 9007199254740993LL;

  EXPECT_NE(HomeConnectorFingerprint(
                connector, lower, "export default {collect() { return []; }};",
                R"({"type":"array"})"),
            HomeConnectorFingerprint(
                connector, upper, "export default {collect() { return []; }};",
                R"({"type":"array"})"));
}

TEST_F(DaoHomeProjectStoreTest,
       ConnectorAuthorizationFingerprintRejectsInvalidDraftReads) {
  auto draft = store_->ApplyPatch(
      /*base_revision=*/"",
      ConnectorProjectPatch("fixture-feed",
                            "export default {collect() { return []; }};",
                            R"({"type":"array"})", R"("https://fixture.test")",
                            R"("/feed")", R"("read_dom")"),
      "Create authorization fixture");
  ASSERT_TRUE(draft.has_value());

  auto duplicate = store_->GetDraftConnectorAuthorizations(
      draft->id, {"fixture-feed", "fixture-feed"});
  ASSERT_FALSE(duplicate.has_value());
  EXPECT_EQ(HomeError::kInvalidArgument, duplicate.error());

  auto missing =
      store_->GetDraftConnectorAuthorizations(draft->id, {"missing-connector"});
  ASSERT_FALSE(missing.has_value());
  EXPECT_EQ(HomeError::kNotFound, missing.error());

  const base::FilePath module_path = store_->root_for_testing()
                                         .AppendASCII(".tmp")
                                         .AppendASCII(draft->id)
                                         .AppendASCII("connectors/feed.js");
  ASSERT_TRUE(base::DeleteFile(module_path));
  auto missing_module =
      store_->GetDraftConnectorAuthorizations(draft->id, {"fixture-feed"});
  ASSERT_FALSE(missing_module.has_value());
  EXPECT_EQ(HomeError::kNotFound, missing_module.error());

  auto schema_draft = store_->ApplyPatch(
      /*base_revision=*/"",
      ConnectorProjectPatch("fixture-feed",
                            "export default {collect() { return []; }};",
                            R"({"type":"array"})", R"("https://fixture.test")",
                            R"("/feed")", R"("read_dom")"),
      "Create schema read fixture");
  ASSERT_TRUE(schema_draft.has_value());
  const base::FilePath schema_path = store_->root_for_testing()
                                         .AppendASCII(".tmp")
                                         .AppendASCII(schema_draft->id)
                                         .AppendASCII("schemas/feed.json");
  ASSERT_TRUE(base::DeleteFile(schema_path));
  auto missing_schema = store_->GetDraftConnectorAuthorizations(
      schema_draft->id, {"fixture-feed"});
  ASSERT_FALSE(missing_schema.has_value());
  EXPECT_EQ(HomeError::kNotFound, missing_schema.error());
}

TEST_F(DaoHomeProjectStoreTest, HistoryBootstrapRequiresExperience) {
  auto missing = store_->ApplyPatch(/*base_revision=*/"", kInitialPatch,
                                    "Create Home without experience");
  ASSERT_TRUE(missing.has_value());
  auto rejected = store_->Publish(missing->id,
                                  HomeRevisionKind::kHistoryBootstrap, nullptr);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(HomeError::kInvalidManifest, rejected.error());
  EXPECT_TRUE(store_->GetDraft(missing->id).has_value());

  HomeExperience canonical_experience;
  canonical_experience.primary_actions = {"github", "bilibili"};
  ASSERT_TRUE(store_
                  ->NormalizeHistoryBootstrapExperience(missing->id,
                                                        canonical_experience)
                  .has_value());
  auto normalized = store_->GetDraftExperience(missing->id);
  ASSERT_TRUE(normalized.has_value());
  EXPECT_EQ(canonical_experience.primary_actions, normalized->primary_actions);
  EXPECT_TRUE(normalized->source_slots.empty());

  auto invalid = store_->ApplyPatch(
      /*base_revision=*/"",
      ConnectorProjectPatch(
          "fixture-feed", "export default {collect() { return []; }};",
          R"({"type":"array"})", R"("https://fixture.test")", R"("/feed")",
          R"("read_dom")", 524288, 50,
          R"({"kind":"dashboard","primary_actions":[],"source_slots":[]})"),
      "Create Home with invalid experience");
  ASSERT_TRUE(invalid.has_value());
  auto invalid_experience = store_->GetDraftExperience(invalid->id);
  ASSERT_FALSE(invalid_experience.has_value());
  EXPECT_EQ(HomeError::kInvalidManifest, invalid_experience.error());
  auto invalid_publish = store_->Publish(
      invalid->id, HomeRevisionKind::kHistoryBootstrap, nullptr);
  ASSERT_FALSE(invalid_publish.has_value());
  EXPECT_EQ(HomeError::kInvalidManifest, invalid_publish.error());

  auto valid = store_->ApplyPatch(
      /*base_revision=*/"",
      ConnectorProjectPatch("fixture-feed",
                            "export default {collect() { return []; }};",
                            R"({"type":"array"})", R"("https://fixture.test")",
                            R"("/feed")", R"("read_dom")"),
      "Create Home with experience");
  ASSERT_TRUE(valid.has_value());
  auto experience = store_->GetDraftExperience(valid->id);
  ASSERT_TRUE(experience.has_value());
  EXPECT_EQ((std::vector<std::string>{"fixture-feed"}),
            experience->primary_actions);
  EXPECT_EQ((std::vector<std::string>{"fixture-feed"}),
            experience->source_slots);
  auto published =
      store_->Publish(valid->id, HomeRevisionKind::kHistoryBootstrap, nullptr);
  ASSERT_TRUE(published.has_value());
  EXPECT_EQ(HomeRevisionKind::kHistoryBootstrap, published->kind);
}

TEST_F(DaoHomeProjectStoreTest, RejectsStaleBaseWithoutChangingPublishedHead) {
  HomeVersion initial = PublishInitialProject();
  auto draft = store_->ApplyPatch(initial.id, kStylePatch, "Add styles");
  ASSERT_TRUE(draft.has_value());
  auto second =
      store_->Publish(draft->id, HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(second.has_value());

  auto stale = store_->ApplyPatch(initial.id, kStylePatch, "Stale edit");
  ASSERT_FALSE(stale.has_value());
  EXPECT_EQ(HomeError::kStaleRevision, stale.error());
  EXPECT_EQ(second->id, store_->GetSnapshot().revision);
}

TEST_F(DaoHomeProjectStoreTest, AddsDecodedAssetToValidatedDraft) {
  HomeVersion initial = PublishInitialProject();
  const std::string binary("\x89PNG\r\n\x1a\n", 8);

  auto draft = store_->AddAsset(initial.id, "assets/mark.png",
                                base::Base64Encode(binary), "Add mark");
  ASSERT_TRUE(draft.has_value());
  auto published =
      store_->Publish(draft->id, HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(published.has_value());
  auto asset = store_->ReadFile(published->id, "assets/mark.png");
  ASSERT_TRUE(asset.has_value());
  EXPECT_EQ(binary, asset.value());

  auto rejected = store_->AddAsset(published->id, "src/not-an-asset.bin",
                                   base::Base64Encode("data"), "Bad asset");
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(HomeError::kInvalidPath, rejected.error());
}

TEST_F(DaoHomeProjectStoreTest, RollbackCreatesNewHeadWithoutRestoringGrant) {
  HomeVersion initial = PublishInitialProject();
  ASSERT_TRUE(store_->GrantConnector("fixture-feed").has_value());

  auto draft = store_->ApplyPatch(initial.id, kStylePatch, "Add styles");
  ASSERT_TRUE(draft.has_value());
  auto second =
      store_->Publish(draft->id, HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(store_->RevokeConnector("fixture-feed").has_value());

  auto rollback =
      store_->Rollback(second->id, initial.id, "Restore initial", nullptr);
  ASSERT_TRUE(rollback.has_value());
  EXPECT_NE(initial.id, rollback->id);
  EXPECT_NE(second->id, rollback->id);
  EXPECT_EQ(initial.id, rollback->restored_from);
  EXPECT_FALSE(store_->HasGrant("fixture-feed"));
  EXPECT_EQ(rollback->id, store_->GetSnapshot().revision);
}

TEST_F(DaoHomeProjectStoreTest,
       RollbackCancellationDiscardsInternalTemporaryDraft) {
  HomeVersion initial = PublishInitialProject();
  auto styled =
      store_->ApplyPatch(initial.id, kStylePatch, "Create rollback head");
  ASSERT_TRUE(styled.has_value());
  auto current =
      store_->Publish(styled->id, HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(current.has_value());
  auto lease = base::MakeRefCounted<DaoHomeMutationLease>();
  store_->SetBeforeRollbackPublishCallbackForTesting(
      base::BindOnce(&DaoHomeMutationLease::Invalidate, lease));

  const base::FilePath temporary_root =
      profile_dir_.GetPath().AppendASCII("DaoHome").AppendASCII(".tmp");
  auto count_temporary_entries = [&]() {
    base::FileEnumerator enumerator(
        temporary_root, false,
        base::FileEnumerator::FILES | base::FileEnumerator::DIRECTORIES);
    size_t count = 0;
    while (!enumerator.Next().empty()) {
      ++count;
    }
    return count;
  };
  const size_t temporary_entries_before = count_temporary_entries();

  auto rollback =
      store_->Rollback(current->id, initial.id, "Restore initial", lease);

  ASSERT_FALSE(rollback.has_value());
  EXPECT_EQ(HomeError::kCancelled, rollback.error());
  EXPECT_EQ(temporary_entries_before, count_temporary_entries());
  EXPECT_EQ(current->id, store_->GetSnapshot().revision);
}

TEST_F(DaoHomeProjectStoreTest, ExportAndImportNeverTransferGrants) {
  HomeVersion initial = PublishInitialProject();
  ASSERT_TRUE(store_->GrantConnector("fixture-feed").has_value());
  auto styled_draft = store_->ApplyPatch(initial.id, kStylePatch, "Add styles");
  ASSERT_TRUE(styled_draft.has_value());
  auto styled = store_->Publish(styled_draft->id,
                                HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(styled.has_value());
  auto package = store_->ExportProject();
  ASSERT_TRUE(package.has_value());
  EXPECT_EQ(std::string::npos, package->find("grants"));
  EXPECT_NE(std::string::npos, package->find("Dao Home runtime"));

  base::ScopedTempDir other_profile;
  ASSERT_TRUE(other_profile.CreateUniqueTempDir());
  DaoHomeProjectStore imported(other_profile.GetPath());
  ASSERT_TRUE(imported.Initialize().has_value());
  auto draft =
      imported.ImportProject(/*base_revision=*/"", *package, "Import Home");
  ASSERT_TRUE(draft.has_value());
  auto published =
      imported.Publish(draft->id, HomeRevisionKind::kImport, nullptr);
  ASSERT_TRUE(published.has_value());
  EXPECT_FALSE(imported.HasGrant("fixture-feed"));
  EXPECT_EQ("index.html", imported.GetSnapshot().manifest->entry);
  EXPECT_NE(initial.id, published->id);

  const std::vector<HomeVersion> imported_versions = imported.ListVersions();
  ASSERT_EQ(3u, imported_versions.size());
  EXPECT_NE(initial.id, imported_versions[0].id);
  EXPECT_NE(styled->id, imported_versions[1].id);
  EXPECT_EQ(imported_versions[1].id, published->restored_from);
  auto original_styles =
      imported.ReadFile(imported_versions[0].id, "src/styles.css");
  EXPECT_FALSE(original_styles.has_value());
  auto styled_source =
      imported.ReadFile(imported_versions[1].id, "src/styles.css");
  ASSERT_TRUE(styled_source.has_value());
  EXPECT_EQ("main { color: #1f2c38; }\n", styled_source.value());

  auto rollback = imported.Rollback(published->id, imported_versions[0].id,
                                    "Restore imported first version", nullptr);
  ASSERT_TRUE(rollback.has_value());
  EXPECT_EQ(imported_versions[0].id, rollback->restored_from);
}

TEST_F(DaoHomeProjectStoreTest, ImportClearsExistingLocalGrants) {
  HomeVersion initial = PublishInitialProject();
  ASSERT_TRUE(store_->GrantConnector("fixture-feed").has_value());
  auto package = store_->ExportProject();
  ASSERT_TRUE(package.has_value());

  auto draft = store_->ImportProject(initial.id, *package, "Replace Home");
  ASSERT_TRUE(draft.has_value());
  auto published =
      store_->Publish(draft->id, HomeRevisionKind::kImport, nullptr);
  ASSERT_TRUE(published.has_value());
  EXPECT_FALSE(store_->HasGrant("fixture-feed"));
}

TEST_F(DaoHomeProjectStoreTest,
       ImportRejectsCurrentFilesThatDifferFromExportedRevision) {
  PublishInitialProject();
  auto package_json = store_->ExportProject();
  ASSERT_TRUE(package_json.has_value());
  std::optional<base::Value> package =
      base::JSONReader::Read(*package_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(package && package->is_dict());
  base::DictValue* files = package->GetDict().FindDict("files");
  ASSERT_TRUE(files);
  files->Set("index.html",
             base::Base64Encode("<!doctype html><main>Changed Home</main>\n"));
  ASSERT_TRUE(base::JSONWriter::Write(*package, &package_json.value()));

  base::ScopedTempDir other_profile;
  ASSERT_TRUE(other_profile.CreateUniqueTempDir());
  DaoHomeProjectStore imported(other_profile.GetPath());
  ASSERT_TRUE(imported.Initialize().has_value());

  auto draft = imported.ImportProject(/*base_revision=*/"", *package_json,
                                      "Import changed Home");

  ASSERT_FALSE(draft.has_value());
  EXPECT_EQ(HomeError::kUnsupportedFormat, draft.error());
  EXPECT_FALSE(imported.GetSnapshot().has_project);
}

TEST_F(DaoHomeProjectStoreTest, ImportRejectsDisconnectedVersionGraph) {
  HomeVersion initial = PublishInitialProject();
  auto styled_draft = store_->ApplyPatch(initial.id, kStylePatch, "Add styles");
  ASSERT_TRUE(styled_draft.has_value());
  auto styled = store_->Publish(styled_draft->id,
                                HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(styled.has_value());
  auto package_json = store_->ExportProject();
  ASSERT_TRUE(package_json.has_value());
  std::optional<base::Value> package =
      base::JSONReader::Read(*package_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(package && package->is_dict());
  base::ListValue* versions = package->GetDict().FindList("versions");
  ASSERT_TRUE(versions);
  ASSERT_EQ(2u, versions->size());
  versions->back().GetDict().Set("parent", "");
  ASSERT_TRUE(base::JSONWriter::Write(*package, &package_json.value()));

  auto imported =
      store_->ImportProject(styled->id, *package_json, "Import bad graph");

  ASSERT_FALSE(imported.has_value());
  EXPECT_EQ(HomeError::kUnsupportedFormat, imported.error());
}

TEST_F(DaoHomeProjectStoreTest, ImportRejectsNonHeadExportedRevision) {
  HomeVersion initial = PublishInitialProject();
  auto styled_draft = store_->ApplyPatch(initial.id, kStylePatch, "Add styles");
  ASSERT_TRUE(styled_draft.has_value());
  auto styled = store_->Publish(styled_draft->id,
                                HomeRevisionKind::kUserRequest, nullptr);
  ASSERT_TRUE(styled.has_value());
  auto package_json = store_->ExportProject();
  ASSERT_TRUE(package_json.has_value());
  std::optional<base::Value> package =
      base::JSONReader::Read(*package_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(package && package->is_dict());
  base::DictValue* revision_files =
      package->GetDict().FindDict("revision_files");
  ASSERT_TRUE(revision_files);
  const base::DictValue* initial_files = revision_files->FindDict(initial.id);
  ASSERT_TRUE(initial_files);
  base::DictValue initial_files_copy = initial_files->Clone();
  package->GetDict().Set("exported_revision", initial.id);
  package->GetDict().Set("files", std::move(initial_files_copy));
  ASSERT_TRUE(base::JSONWriter::Write(*package, &package_json.value()));

  auto imported = store_->ImportProject(styled->id, *package_json,
                                        "Import non-head revision");

  ASSERT_FALSE(imported.has_value());
  EXPECT_EQ(HomeError::kUnsupportedFormat, imported.error());
}

}  // namespace
}  // namespace dao
