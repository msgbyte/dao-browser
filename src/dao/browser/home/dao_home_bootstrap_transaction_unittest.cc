// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_bootstrap_transaction.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/containers/flat_set.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace dao {
namespace {

class DaoHomeBootstrapTransactionTest : public testing::Test {
 protected:
  DaoHomeBootstrapTransactionTest()
      : owner_factory_(reinterpret_cast<content::WebContents*>(&owner_token_)) {
  }

  void SetUp() override {
    lease_ = base::MakeRefCounted<DaoHomeMutationLease>();
  }

  HomeLaunchTarget LaunchTarget(std::string id, std::string url) {
    HomeLaunchTarget target;
    target.id = std::move(id);
    target.label_hint = target.id + " label";
    target.url = GURL(url);
    target.category_hint = "fixture";
    target.source_eligibility = HomeSourceEligibility::kLaunchAndFeed;
    return target;
  }

  HomeSourceCandidate SourceCandidate(std::string launch_target_id) {
    HomeSourceCandidate candidate;
    candidate.launch_target_id = std::move(launch_target_id);
    candidate.connector_kind_hint = "feed";
    candidate.schema_source = R"({"type":"array"})";
    return candidate;
  }

  HomeBootstrapBrief ThreeSourceBrief() {
    HomeBootstrapBrief brief;
    brief.locale = "en-US";
    brief.launch_targets = {
        LaunchTarget("github", "https://github.com/"),
        LaunchTarget("bilibili", "https://www.bilibili.com/"),
        LaunchTarget("forum", "https://forum.example/"),
    };
    brief.source_candidates = {
        SourceCandidate("github"),
        SourceCandidate("bilibili"),
        SourceCandidate("forum"),
    };
    return brief;
  }

  HomeBootstrapBrief FiveTargetBrief() {
    HomeBootstrapBrief brief = ThreeSourceBrief();
    brief.launch_targets.push_back(
        LaunchTarget("docs", "https://docs.example/"));
    brief.launch_targets.push_back(
        LaunchTarget("search", "https://search.example/"));
    return brief;
  }

  HomeConnector Connector(std::string_view id) {
    HomeConnector connector;
    connector.id = std::string(id);
    connector.module = "connectors/" + std::string(id) + ".js";
    connector.schema = "schemas/" + std::string(id) + ".json";
    connector.permissions.origins.push_back(
        url::Origin::Create(GURL("https://" + std::string(id) + ".example/")));
    connector.permissions.paths.push_back("/feed");
    connector.permissions.capabilities.insert(HomePageCapability::kReadDom);
    connector.permissions.mode = HomeAccessMode::kRead;
    return connector;
  }

  HomeConnectorAuthorization Authorization(std::string_view id) {
    HomeConnectorAuthorization authorization;
    authorization.connector_id = std::string(id);
    authorization.bundle.connector = Connector(id);
    authorization.bundle.limits.max_result_bytes = 4096;
    authorization.bundle.limits.max_items_per_connector = 10;
    authorization.bundle.module_source =
        "export default {async collect(page) { return []; }};";
    authorization.bundle.schema_source = R"({"type":"array"})";
    authorization.bundle.granted = false;
    authorization.fingerprint = std::string(id) + "-fingerprint";
    return authorization;
  }

  HomeDraft Draft(std::string id,
                  std::initializer_list<std::string_view> connector_ids) {
    HomeDraft draft;
    draft.id = std::move(id);
    draft.base_revision = "revision-1";
    draft.summary = "Fixture draft";
    draft.manifest.format_version = 1;
    draft.manifest.entry = "index.html";
    draft.manifest.routes = {"/"};
    draft.manifest.limits.max_result_bytes = 4096;
    draft.manifest.limits.max_items_per_connector = 10;
    for (std::string_view connector_id : connector_ids) {
      draft.manifest.connectors.push_back(Connector(connector_id));
      draft.permission_expanded_connector_ids.push_back(
          std::string(connector_id));
    }
    draft.permission_expansion = !draft.manifest.connectors.empty();
    return draft;
  }

  HomeDraft ProvisionalDraft() {
    return Draft("provisional-draft", {"github", "bilibili", "forum"});
  }

  HomeDraft FinalDraftWith(
      std::initializer_list<std::string_view> connector_ids) {
    return Draft("final-draft", connector_ids);
  }

  std::vector<HomeConnectorAuthorization> ThreeAuthorizations() {
    return {Authorization("github"), Authorization("bilibili"),
            Authorization("forum")};
  }

  std::vector<HomeConnectorAuthorization> FinalAuthorizations(
      std::initializer_list<std::string_view> connector_ids) {
    std::vector<HomeConnectorAuthorization> authorizations;
    for (std::string_view connector_id : connector_ids) {
      authorizations.push_back(Authorization(connector_id));
    }
    return authorizations;
  }

  HomeConnectorTestOutcome Success(std::string connector_id,
                                   std::string fingerprint) {
    HomeConnectorTestOutcome outcome;
    outcome.connector_id = std::move(connector_id);
    outcome.fingerprint = std::move(fingerprint);
    outcome.status = HomeConnectorTestStatus::kSucceeded;
    base::ListValue items;
    items.Append(base::DictValue().Set("title", "Fixture item"));
    outcome.sample = base::Value(std::move(items));
    return outcome;
  }

  HomeConnectorTestOutcome Failure(std::string connector_id,
                                   std::string fingerprint,
                                   std::string error_code) {
    HomeConnectorTestOutcome outcome;
    outcome.connector_id = std::move(connector_id);
    outcome.fingerprint = std::move(fingerprint);
    outcome.status = error_code == "auth_required"
                         ? HomeConnectorTestStatus::kAuthenticationRequired
                         : HomeConnectorTestStatus::kRuntimeFailed;
    outcome.error_code = std::move(error_code);
    return outcome;
  }

  HomeExperience Experience(
      std::initializer_list<std::string_view> primary_actions,
      std::initializer_list<std::string_view> source_slots) {
    HomeExperience experience;
    for (std::string_view action : primary_actions) {
      experience.primary_actions.push_back(std::string(action));
    }
    for (std::string_view slot : source_slots) {
      experience.source_slots.push_back(std::string(slot));
    }
    return experience;
  }

  DaoHomeBootstrapTransaction MakeTransaction(
      HomeBootstrapBrief brief,
      std::string agent_turn_id = "turn-1") {
    return DaoHomeBootstrapTransaction(
        "transaction-1", std::move(agent_turn_id), owner_factory_.GetWeakPtr(),
        "revision-1", std::move(brief), lease_,
        base::BindRepeating(&DaoHomeBootstrapTransactionTest::IsExpectedContext,
                            base::Unretained(this)));
  }

  bool IsExpectedContext(const std::string& expected_agent_turn,
                         const std::string& expected_base_revision) const {
    return live_agent_turn_ == expected_agent_turn &&
           live_base_revision_ == expected_base_revision;
  }

  base::expected<HomePermissionBatchRequest, HomeError> RegisterAndPrepare(
      DaoHomeBootstrapTransaction& transaction,
      HomeDraft draft,
      std::vector<HomeConnectorAuthorization> authorizations) {
    auto registered = transaction.RegisterDraft(draft);
    if (!registered.has_value()) {
      return base::unexpected(registered.error());
    }
    return transaction.PreparePermissionBatch(draft, std::move(authorizations));
  }

  base::expected<HomePreviewRequirements, HomeError> RegisterAndBind(
      DaoHomeBootstrapTransaction& transaction,
      HomeDraft draft,
      std::vector<HomeConnectorAuthorization> authorizations,
      HomeExperience experience) {
    auto registered = transaction.RegisterDraft(draft);
    if (!registered.has_value()) {
      return base::unexpected(registered.error());
    }
    return transaction.BindFinalDraft(draft, std::move(authorizations),
                                      std::move(experience));
  }

  void ResolveAllAndRecord(DaoHomeBootstrapTransaction& transaction) {
    HomePermissionBatchRequest request =
        RegisterAndPrepare(transaction, ProvisionalDraft(),
                           ThreeAuthorizations())
            .value();
    ASSERT_TRUE(
        transaction
            .ResolvePermissionBatch(request.id, {"github", "bilibili", "forum"})
            .has_value());
    ASSERT_TRUE(
        transaction
            .RecordConnectorOutcome(Success("github", "github-fingerprint"))
            .has_value());
    ASSERT_TRUE(
        transaction
            .RecordConnectorOutcome(Success("bilibili", "bilibili-fingerprint"))
            .has_value());
    ASSERT_TRUE(transaction
                    .RecordConnectorOutcome(
                        Failure("forum", "forum-fingerprint", "auth_required"))
                    .has_value());
  }

  scoped_refptr<DaoHomeMutationLease> lease_;
  std::string live_agent_turn_ = "turn-1";
  std::string live_base_revision_ = "revision-1";
  alignas(std::max_align_t) std::byte owner_token_;
  base::WeakPtrFactory<content::WebContents> owner_factory_;
};

TEST_F(DaoHomeBootstrapTransactionTest,
       OwnsOnlyDraftsRegisteredInTheExpectedPhase) {
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    EXPECT_FALSE(
        transaction
            .PreparePermissionBatch(ProvisionalDraft(), ThreeAuthorizations())
            .has_value());
    EXPECT_TRUE(transaction.Cancel().empty());
  }

  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                      ThreeAuthorizations());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        transaction.ResolvePermissionBatch(request->id, {}).has_value());

    EXPECT_FALSE(
        transaction
            .BindFinalDraft(FinalDraftWith({}), {},
                            Experience({"github", "bilibili", "forum"}, {}))
            .has_value());
    EXPECT_EQ((std::vector<std::string>{"provisional-draft"}),
              transaction.Cancel());
  }

  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                      ThreeAuthorizations());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        transaction.ResolvePermissionBatch(request->id, {}).has_value());

    HomeDraft first_rejected = Draft("first-rejected-final", {});
    ASSERT_TRUE(transaction.RegisterDraft(first_rejected).has_value());
    EXPECT_FALSE(
        transaction
            .BindFinalDraft(first_rejected, {}, Experience({"unknown"}, {}))
            .has_value());
    HomeDraft second_rejected = Draft("second-rejected-final", {});
    ASSERT_TRUE(transaction.RegisterDraft(second_rejected).has_value());
    EXPECT_FALSE(
        transaction
            .BindFinalDraft(second_rejected, {}, Experience({"unknown"}, {}))
            .has_value());

    const std::vector<std::string> expected = {
        "provisional-draft", "first-rejected-final", "second-rejected-final"};
    EXPECT_EQ(expected, transaction.Cancel());
    EXPECT_EQ(expected, transaction.Cancel());
  }
}

TEST_F(DaoHomeBootstrapTransactionTest, RejectsLiveTurnAndBaseDrift) {
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    ASSERT_TRUE(transaction.RegisterDraft(ProvisionalDraft()).has_value());
    live_agent_turn_ = "turn-2";
    auto prepared = transaction.PreparePermissionBatch(ProvisionalDraft(),
                                                       ThreeAuthorizations());
    ASSERT_FALSE(prepared.has_value());
    EXPECT_EQ(HomeError::kCancelled, prepared.error());
  }

  live_agent_turn_ = "turn-1";
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    ASSERT_TRUE(transaction.RegisterDraft(ProvisionalDraft()).has_value());
    live_base_revision_ = "revision-2";
    auto prepared = transaction.PreparePermissionBatch(ProvisionalDraft(),
                                                       ThreeAuthorizations());
    ASSERT_FALSE(prepared.has_value());
    EXPECT_EQ(HomeError::kCancelled, prepared.error());
  }
}

TEST_F(DaoHomeBootstrapTransactionTest,
       AcceptsCanonicalPermissionOrderingWithExactFingerprint) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  HomeConnectorAuthorization provisional_authorization =
      Authorization("github");
  provisional_authorization.bundle.connector.permissions.origins.push_back(
      url::Origin::Create(GURL("https://api.github.example/")));
  provisional_authorization.bundle.connector.permissions.paths.push_back(
      "/notifications");
  HomeDraft provisional = Draft("provisional-draft", {"github"});
  provisional.manifest.connectors[0].permissions.origins.push_back(
      url::Origin::Create(GURL("https://api.github.example/")));
  std::ranges::reverse(provisional.manifest.connectors[0].permissions.origins);
  provisional.manifest.connectors[0].permissions.paths.push_back(
      "/notifications");
  std::ranges::reverse(provisional.manifest.connectors[0].permissions.paths);

  auto request = RegisterAndPrepare(transaction, provisional,
                                    {std::move(provisional_authorization)});
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(
      transaction.ResolvePermissionBatch(request->id, {"github"}).has_value());
  ASSERT_TRUE(
      transaction
          .RecordConnectorOutcome(Success("github", "github-fingerprint"))
          .has_value());

  HomeConnectorAuthorization final_authorization = Authorization("github");
  final_authorization.bundle.connector.permissions.origins.push_back(
      url::Origin::Create(GURL("https://api.github.example/")));
  final_authorization.bundle.connector.permissions.paths.push_back(
      "/notifications");
  HomeDraft final_draft = Draft("final-draft", {"github"});
  final_draft.manifest.connectors[0].permissions.origins =
      final_authorization.bundle.connector.permissions.origins;
  final_draft.manifest.connectors[0].permissions.paths =
      final_authorization.bundle.connector.permissions.paths;
  std::ranges::reverse(final_draft.manifest.connectors[0].permissions.origins);
  std::ranges::reverse(final_draft.manifest.connectors[0].permissions.paths);

  EXPECT_TRUE(RegisterAndBind(
                  transaction, final_draft, {std::move(final_authorization)},
                  Experience({"github", "bilibili", "forum"}, {"github"}))
                  .has_value());
}

TEST_F(DaoHomeBootstrapTransactionTest, AllowsSuccessfulSubsetAfterTesting) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  EXPECT_EQ(HomeBootstrapState::kPlanning, transaction.state());

  auto prepared = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                     ThreeAuthorizations());
  ASSERT_TRUE(prepared.has_value());
  HomePermissionBatchRequest request = std::move(prepared.value());
  EXPECT_EQ("transaction-1", request.transaction_id);
  EXPECT_EQ("provisional-draft", request.draft_id);
  EXPECT_EQ("revision-1", request.base_revision);
  ASSERT_EQ(3u, request.items.size());
  EXPECT_EQ("github", request.items[0].connector_id);
  EXPECT_EQ("github label", request.items[0].label);
  EXPECT_EQ("github-fingerprint", request.items[0].fingerprint);
  EXPECT_EQ(HomeBootstrapState::kAwaitingBatchApproval, transaction.state());

  ASSERT_TRUE(
      transaction
          .ResolvePermissionBatch(request.id, {"github", "bilibili", "forum"})
          .has_value());
  EXPECT_EQ(HomeBootstrapState::kTestingSources, transaction.state());
  ASSERT_TRUE(
      transaction
          .RecordConnectorOutcome(Success("github", "github-fingerprint"))
          .has_value());
  ASSERT_TRUE(
      transaction
          .RecordConnectorOutcome(Success("bilibili", "bilibili-fingerprint"))
          .has_value());
  ASSERT_TRUE(transaction
                  .RecordConnectorOutcome(
                      Failure("forum", "forum-fingerprint", "auth_required"))
                  .has_value());
  EXPECT_EQ(HomeBootstrapState::kBuildingFinalHome, transaction.state());

  auto requirements = RegisterAndBind(
      transaction, FinalDraftWith({"github", "bilibili"}),
      FinalAuthorizations({"github", "bilibili"}),
      Experience({"github", "bilibili", "forum"}, {"github", "bilibili"}));
  ASSERT_TRUE(requirements.has_value());
  EXPECT_EQ((base::flat_set<std::string>{"github", "bilibili"}),
            requirements->tested_connector_ids);
  EXPECT_EQ(GURL("https://github.com/"),
            requirements->launch_urls.at("github"));
  EXPECT_EQ(GURL("https://www.bilibili.com/"),
            requirements->launch_urls.at("bilibili"));
  EXPECT_EQ(GURL("https://forum.example/"),
            requirements->launch_urls.at("forum"));
  EXPECT_EQ(HomeBootstrapState::kPreviewing, transaction.state());

  ASSERT_TRUE(transaction.MarkPreviewed("final-draft").has_value());
  ASSERT_TRUE(transaction.BeginPublish("final-draft").has_value());
  EXPECT_EQ(HomeBootstrapState::kPublishing, transaction.state());
  transaction.MarkPublished();
  EXPECT_EQ(HomeBootstrapState::kComplete, transaction.state());
}

TEST_F(DaoHomeBootstrapTransactionTest, RejectAllBuildsFinalWithoutSources) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  HomePermissionBatchRequest request =
      RegisterAndPrepare(transaction, ProvisionalDraft(), ThreeAuthorizations())
          .value();

  ASSERT_TRUE(transaction.ResolvePermissionBatch(request.id, {}).has_value());
  EXPECT_EQ(HomeBootstrapState::kBuildingFinalHome, transaction.state());
  auto requirements =
      RegisterAndBind(transaction, FinalDraftWith({}), {},
                      Experience({"github", "bilibili", "forum"}, {}));
  ASSERT_TRUE(requirements.has_value());
  EXPECT_TRUE(requirements->tested_connector_ids.empty());
  EXPECT_EQ(HomeBootstrapState::kPreviewing, transaction.state());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       FailedPreviewCanBeReplacedByAnotherFinalDraft) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  HomePermissionBatchRequest request =
      RegisterAndPrepare(transaction, ProvisionalDraft(), ThreeAuthorizations())
          .value();
  ASSERT_TRUE(transaction.ResolvePermissionBatch(request.id, {}).has_value());

  HomeDraft failed_draft = FinalDraftWith({});
  failed_draft.id = "failed-final-draft";
  ASSERT_TRUE(RegisterAndBind(
                  transaction, failed_draft, {},
                  Experience({"github", "bilibili", "forum"}, {}))
                  .has_value());
  ASSERT_TRUE(transaction.RejectPreview(failed_draft.id).has_value());
  EXPECT_EQ(HomeBootstrapState::kBuildingFinalHome, transaction.state());

  HomeDraft replacement_draft = FinalDraftWith({});
  replacement_draft.id = "replacement-final-draft";
  ASSERT_TRUE(RegisterAndBind(
                  transaction, replacement_draft, {},
                  Experience({"github", "bilibili", "forum"}, {}))
                  .has_value());
  ASSERT_TRUE(transaction.MarkPreviewed(replacement_draft.id).has_value());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RejectsChangedExecutableScopeOrBudgetFingerprint) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  ResolveAllAndRecord(transaction);

  for (const std::string& changed_fingerprint :
       {"changed-module-fingerprint", "changed-schema-fingerprint",
        "changed-scope-fingerprint", "changed-capability-fingerprint",
        "changed-budget-fingerprint"}) {
    auto authorizations = FinalAuthorizations({"github", "bilibili"});
    authorizations[0].fingerprint = changed_fingerprint;
    EXPECT_FALSE(
        RegisterAndBind(
            transaction, FinalDraftWith({"github", "bilibili"}),
            std::move(authorizations),
            Experience({"github", "bilibili", "forum"}, {"github", "bilibili"}))
            .has_value())
        << changed_fingerprint;
  }
  EXPECT_EQ((std::vector<std::string>{"provisional-draft", "final-draft"}),
            transaction.Cancel());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RejectsSampleBeyondConnectorResultBudget) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                    ThreeAuthorizations());
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(
      transaction.ResolvePermissionBatch(request->id, {"github"}).has_value());

  HomeConnectorTestOutcome oversized = Success("github", "github-fingerprint");
  oversized.sample = base::Value(std::string(5000, 'x'));
  auto recorded = transaction.RecordConnectorOutcome(std::move(oversized));
  ASSERT_FALSE(recorded.has_value());
  EXPECT_EQ(HomeError::kQuotaExceeded, recorded.error());
  EXPECT_EQ(HomeBootstrapState::kTestingSources, transaction.state());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RejectsUntestedConnectorInFinalManifest) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  HomePermissionBatchRequest request =
      RegisterAndPrepare(transaction, ProvisionalDraft(), ThreeAuthorizations())
          .value();
  ASSERT_TRUE(
      transaction.ResolvePermissionBatch(request.id, {"github"}).has_value());
  ASSERT_TRUE(
      transaction
          .RecordConnectorOutcome(Success("github", "github-fingerprint"))
          .has_value());

  EXPECT_FALSE(
      RegisterAndBind(transaction, FinalDraftWith({"github", "bilibili"}),
                      FinalAuthorizations({"github", "bilibili"}),
                      Experience({"github", "bilibili", "forum"}, {"github"}))
          .has_value());
}

TEST_F(DaoHomeBootstrapTransactionTest, RejectsSourceSlotForFailedConnector) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  ResolveAllAndRecord(transaction);

  EXPECT_FALSE(
      RegisterAndBind(
          transaction, FinalDraftWith({"github", "bilibili"}),
          FinalAuthorizations({"github", "bilibili"}),
          Experience({"github", "bilibili", "forum"}, {"github", "forum"}))
          .has_value());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RejectsDifferentBaseRevisionAndAgentTurn) {
  DaoHomeBootstrapTransaction wrong_base = MakeTransaction(ThreeSourceBrief());
  HomeDraft stale = ProvisionalDraft();
  stale.base_revision = "revision-2";
  EXPECT_FALSE(
      RegisterAndPrepare(wrong_base, stale, ThreeAuthorizations()).has_value());

  DaoHomeBootstrapTransaction wrong_turn = MakeTransaction(ThreeSourceBrief());
  auto request =
      RegisterAndPrepare(wrong_turn, ProvisionalDraft(), ThreeAuthorizations());
  ASSERT_TRUE(request.has_value());
  live_agent_turn_ = "turn-2";
  auto resolved = wrong_turn.ResolvePermissionBatch(
      request->id, {"github", "bilibili", "forum"});
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(HomeError::kCancelled, resolved.error());
  EXPECT_EQ(HomeBootstrapState::kAwaitingBatchApproval, wrong_turn.state());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RequiresKnownActionsAndFirstFourRankedTargets) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(FiveTargetBrief());
  HomePermissionBatchRequest request =
      RegisterAndPrepare(transaction, ProvisionalDraft(), ThreeAuthorizations())
          .value();
  ASSERT_TRUE(transaction.ResolvePermissionBatch(request.id, {}).has_value());

  EXPECT_FALSE(
      RegisterAndBind(
          transaction, FinalDraftWith({}), {},
          Experience({"github", "bilibili", "forum", "docs", "unknown"}, {}))
          .has_value());
  EXPECT_FALSE(RegisterAndBind(transaction, FinalDraftWith({}), {},
                               Experience({"github", "bilibili", "forum"}, {}))
                   .has_value());
  EXPECT_TRUE(
      RegisterAndBind(transaction, FinalDraftWith({}), {},
                      Experience({"github", "bilibili", "forum", "docs"}, {}))
          .has_value());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RejectsWrongRequestAndOutOfOrderTransitions) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  EXPECT_FALSE(
      transaction
          .RecordConnectorOutcome(Success("github", "github-fingerprint"))
          .has_value());
  auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                    ThreeAuthorizations());
  ASSERT_TRUE(request.has_value());
  EXPECT_FALSE(
      RegisterAndPrepare(transaction, ProvisionalDraft(), ThreeAuthorizations())
          .has_value());
  EXPECT_FALSE(transaction.ResolvePermissionBatch("other-request", {"github"})
                   .has_value());
  EXPECT_FALSE(
      transaction.ResolvePermissionBatch(request->id, {"unknown"}).has_value());
  ASSERT_TRUE(
      transaction
          .ResolvePermissionBatch(request->id, {"github", "bilibili", "forum"})
          .has_value());
  EXPECT_FALSE(
      RegisterAndBind(
          transaction, FinalDraftWith({"github", "bilibili"}),
          FinalAuthorizations({"github", "bilibili"}),
          Experience({"github", "bilibili", "forum"}, {"github", "bilibili"}))
          .has_value());
  ASSERT_TRUE(
      transaction
          .RecordConnectorOutcome(Success("github", "github-fingerprint"))
          .has_value());
  EXPECT_FALSE(
      transaction
          .RecordConnectorOutcome(Success("github", "github-fingerprint"))
          .has_value());
  ASSERT_TRUE(
      transaction
          .RecordConnectorOutcome(Success("bilibili", "bilibili-fingerprint"))
          .has_value());
  ASSERT_TRUE(transaction
                  .RecordConnectorOutcome(
                      Failure("forum", "forum-fingerprint", "auth_required"))
                  .has_value());
  ASSERT_TRUE(
      RegisterAndBind(
          transaction, FinalDraftWith({"github", "bilibili"}),
          FinalAuthorizations({"github", "bilibili"}),
          Experience({"github", "bilibili", "forum"}, {"github", "bilibili"}))
          .has_value());
  EXPECT_FALSE(transaction.BeginPublish("final-draft").has_value());
  EXPECT_FALSE(transaction.MarkPreviewed("other-draft").has_value());
  ASSERT_TRUE(transaction.MarkPreviewed("final-draft").has_value());
  EXPECT_FALSE(transaction.MarkPreviewed("final-draft").has_value());
  EXPECT_FALSE(transaction.BeginPublish("other-draft").has_value());
  ASSERT_TRUE(transaction.BeginPublish("final-draft").has_value());
  EXPECT_FALSE(transaction.BeginPublish("final-draft").has_value());
  transaction.MarkPublished();
  EXPECT_EQ(HomeBootstrapState::kComplete, transaction.state());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       RejectsNonCandidateOversizedAndMismatchedProvisionalBatches) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  HomeDraft mismatched = ProvisionalDraft();
  mismatched.manifest.connectors[0].id = "not-github";
  EXPECT_FALSE(
      RegisterAndPrepare(transaction, mismatched, ThreeAuthorizations())
          .has_value());

  DaoHomeBootstrapTransaction oversized = MakeTransaction(ThreeSourceBrief());
  HomeDraft four =
      Draft("four-source-draft", {"github", "bilibili", "forum", "extra"});
  auto four_authorizations = ThreeAuthorizations();
  four_authorizations.push_back(Authorization("extra"));
  EXPECT_FALSE(
      RegisterAndPrepare(oversized, four, std::move(four_authorizations))
          .has_value());
}

TEST_F(DaoHomeBootstrapTransactionTest, RejectsOwnerLossAndInvalidLease) {
  DaoHomeBootstrapTransaction owner_lost = MakeTransaction(ThreeSourceBrief());
  auto request =
      RegisterAndPrepare(owner_lost, ProvisionalDraft(), ThreeAuthorizations());
  ASSERT_TRUE(request.has_value());
  owner_factory_.InvalidateWeakPtrs();
  auto resolved = owner_lost.ResolvePermissionBatch(request->id, {});
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(HomeError::kCancelled, resolved.error());

  lease_ = base::MakeRefCounted<DaoHomeMutationLease>();
  DaoHomeBootstrapTransaction invalid_lease =
      MakeTransaction(ThreeSourceBrief());
  lease_->Invalidate();
  auto prepared = RegisterAndPrepare(invalid_lease, ProvisionalDraft(),
                                     ThreeAuthorizations());
  ASSERT_FALSE(prepared.has_value());
  EXPECT_EQ(HomeError::kCancelled, prepared.error());
}

TEST_F(DaoHomeBootstrapTransactionTest, CancelsEveryNonTerminalState) {
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    EXPECT_TRUE(transaction.Cancel().empty());
    EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  }
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    ASSERT_TRUE(RegisterAndPrepare(transaction, ProvisionalDraft(),
                                   ThreeAuthorizations())
                    .has_value());
    EXPECT_EQ((std::vector<std::string>{"provisional-draft"}),
              transaction.Cancel());
    EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  }
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                      ThreeAuthorizations());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(transaction.ResolvePermissionBatch(request->id, {"github"})
                    .has_value());
    EXPECT_EQ((std::vector<std::string>{"provisional-draft"}),
              transaction.Cancel());
    EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  }
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                      ThreeAuthorizations());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        transaction.ResolvePermissionBatch(request->id, {}).has_value());
    EXPECT_EQ((std::vector<std::string>{"provisional-draft"}),
              transaction.Cancel());
    EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  }
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                      ThreeAuthorizations());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        transaction.ResolvePermissionBatch(request->id, {}).has_value());
    ASSERT_TRUE(RegisterAndBind(transaction, FinalDraftWith({}), {},
                                Experience({"github", "bilibili", "forum"}, {}))
                    .has_value());
    EXPECT_EQ((std::vector<std::string>{"provisional-draft", "final-draft"}),
              transaction.Cancel());
    EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  }
  {
    DaoHomeBootstrapTransaction transaction =
        MakeTransaction(ThreeSourceBrief());
    auto request = RegisterAndPrepare(transaction, ProvisionalDraft(),
                                      ThreeAuthorizations());
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(
        transaction.ResolvePermissionBatch(request->id, {}).has_value());
    ASSERT_TRUE(RegisterAndBind(transaction, FinalDraftWith({}), {},
                                Experience({"github", "bilibili", "forum"}, {}))
                    .has_value());
    ASSERT_TRUE(transaction.MarkPreviewed("final-draft").has_value());
    ASSERT_TRUE(transaction.BeginPublish("final-draft").has_value());
    EXPECT_EQ((std::vector<std::string>{"provisional-draft", "final-draft"}),
              transaction.Cancel());
    EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  }
}

TEST_F(DaoHomeBootstrapTransactionTest,
       CancellationIsIdempotentAndExcludesPublishedFinalDraft) {
  DaoHomeBootstrapTransaction cancelled = MakeTransaction(ThreeSourceBrief());
  ResolveAllAndRecord(cancelled);
  ASSERT_TRUE(RegisterAndBind(cancelled, FinalDraftWith({"github", "bilibili"}),
                              FinalAuthorizations({"github", "bilibili"}),
                              Experience({"github", "bilibili", "forum"},
                                         {"github", "bilibili"}))
                  .has_value());
  const std::vector<std::string> expected = {"provisional-draft",
                                             "final-draft"};
  EXPECT_EQ(expected, cancelled.Cancel());
  EXPECT_EQ(expected, cancelled.Cancel());

  DaoHomeBootstrapTransaction published = MakeTransaction(ThreeSourceBrief());
  auto request =
      RegisterAndPrepare(published, ProvisionalDraft(), ThreeAuthorizations());
  ASSERT_TRUE(request.has_value());
  ASSERT_TRUE(published.ResolvePermissionBatch(request->id, {}).has_value());
  ASSERT_TRUE(RegisterAndBind(published, FinalDraftWith({}), {},
                              Experience({"github", "bilibili", "forum"}, {}))
                  .has_value());
  ASSERT_TRUE(published.MarkPreviewed("final-draft").has_value());
  ASSERT_TRUE(published.BeginPublish("final-draft").has_value());
  published.MarkPublished();
  EXPECT_EQ((std::vector<std::string>{"provisional-draft"}),
            published.Cancel());
  EXPECT_EQ(HomeBootstrapState::kComplete, published.state());
}

TEST_F(DaoHomeBootstrapTransactionTest,
       CleanupAfterSuccessfulSamplesIsTerminalAndIdempotent) {
  DaoHomeBootstrapTransaction transaction = MakeTransaction(ThreeSourceBrief());
  ResolveAllAndRecord(transaction);

  const std::vector<std::string> cleanup = {"provisional-draft"};
  EXPECT_EQ(cleanup, transaction.Cancel());
  EXPECT_EQ(HomeBootstrapState::kCancelled, transaction.state());
  auto final = RegisterAndBind(
      transaction, FinalDraftWith({"github", "bilibili"}),
      FinalAuthorizations({"github", "bilibili"}),
      Experience({"github", "bilibili", "forum"}, {"github", "bilibili"}));
  ASSERT_FALSE(final.has_value());
  EXPECT_EQ(HomeError::kInvalidArgument, final.error());
  EXPECT_EQ(cleanup, transaction.Cancel());
}

}  // namespace
}  // namespace dao
