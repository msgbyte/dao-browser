// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_proactive_ranker.h"

#include <optional>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/time/time.h"
#include "dao/browser/agent/dao_agent_scenario_registry.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

ScenarioDefinition MakeScenario(std::string id,
                                std::string type,
                                std::string url_pattern,
                                std::string action_label,
                                bool requires_page_content) {
  ScenarioDefinition scenario;
  scenario.id = std::move(id);
  scenario.type = std::move(type);
  scenario.name = "Scenario";
  scenario.url_pattern = std::move(url_pattern);
  scenario.action_label = std::move(action_label);
  scenario.requires_page_content = requires_page_content;
  return scenario;
}

ProactiveCandidate MakeCandidate(ScenarioDefinition scenario) {
  ProactiveCandidate candidate;
  candidate.source = scenario.type == "personal"
                         ? ProactiveCandidateSource::kPersonalScenario
                         : ProactiveCandidateSource::kSeedScenario;
  candidate.scenario = std::move(scenario);
  return candidate;
}

ProactivePageSignals MakeSignals(const std::string& url) {
  ProactivePageSignals signals;
  signals.url = url;
  signals.domain = "github.com";
  signals.title = "Pull request";
  signals.word_count = 2200;
  signals.char_count = 12000;
  signals.code_block_count = 8;
  signals.heading_count = 10;
  signals.has_code = true;
  signals.has_significant_content = true;
  return signals;
}

ProactiveCandidate MakeSeedCandidateForUrl(const std::string& url) {
  DaoAgentScenarioRegistry registry;
  std::optional<ScenarioDefinition> scenario = registry.Match(url);
  if (!scenario.has_value()) {
    ADD_FAILURE() << "Expected a seed scenario for " << url;
    return ProactiveCandidate();
  }
  return MakeCandidate(std::move(*scenario));
}

TEST(DaoAgentProactiveRankerTest, BalancedShowsLargeGithubPr) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_pr", "seed", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "review_code", true));
  ProactivePageSignals signals =
      MakeSignals("https://github.com/dao/dao-browser/pull/42");
  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_GE(decision.score.final_score, 0.75);
  EXPECT_EQ("captures_after_run", decision.context_disclosure);
  EXPECT_TRUE(decision.suppression_reason.empty());

  auto parsed =
      base::JSONReader::Read(decision.score_debug_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  const base::DictValue* dict = parsed->GetIfDict();
  ASSERT_TRUE(dict);
  const std::string* suppression_reason =
      dict->FindString("suppression_reason");
  ASSERT_TRUE(suppression_reason);
  EXPECT_TRUE(suppression_reason->empty());
}

TEST(DaoAgentProactiveRankerTest,
     BalancedShowsLargeGitlabMergeRequest) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_pr", "seed",
      R"((^https://github\.com/[^/]+/[^/]+/pull/\d+|/-/merge_requests/\d+))",
      "review_code", true));
  ProactivePageSignals signals =
      MakeSignals("https://gitlab.com/acme/web/-/merge_requests/123");
  signals.domain = "gitlab.com";
  signals.title = "Merge request";

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_GE(decision.score.final_score, 0.75);
  EXPECT_EQ("captures_after_run", decision.context_disclosure);
  EXPECT_TRUE(decision.suppression_reason.empty());
}

TEST(DaoAgentProactiveRankerTest,
     ReviewCodeShowsGitlabMergeRequestWithReviewPageIdentity) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  const std::string url = "https://gitlab.com/acme/web/-/merge_requests/123";
  ProactiveCandidate candidate = MakeSeedCandidateForUrl(url);
  ProactivePageSignals signals = MakeSignals(url);
  signals.domain = "gitlab.com";
  signals.title = "Improve navigation (!123) · Merge requests · acme / web";
  signals.meta_description = "Review the merge request diff";

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_TRUE(decision.suppression_reason.empty());
  EXPECT_GE(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest,
     ReviewCodeSuppressesGitlabPathWithoutReviewPageIdentity) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  const std::string url =
      "https://example.com/docs/gitlab/-/merge_requests/123";
  ProactiveCandidate candidate = MakeSeedCandidateForUrl(url);
  ProactivePageSignals signals = MakeSignals(url);
  signals.domain = "example.com";
  signals.title = "GitLab webhook examples";
  signals.meta_description = "API fixture paths and routing examples";

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_page_hint", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, SeedHighValueActionGetsUsefulnessBoost) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_review", "seed", R"(^https://github\.com/[^/]+/[^/]+/issues/\d+)",
      "analyze_issue", true));
  ProactivePageSignals signals;
  signals.url = "https://github.com/dao/dao-browser/issues/42";
  signals.domain = "github.com";
  signals.title = "Issue";
  signals.has_significant_content = false;
  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_DOUBLE_EQ(0.20, decision.score.usefulness);
}

TEST(DaoAgentProactiveRankerTest, BalancedShowsLargeGitlabIssue) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_issue", "seed",
      R"((^https://github\.com/[^/]+/[^/]+/issues/\d+|/-/issues/\d+))",
      "analyze_issue", true));
  ProactivePageSignals signals;
  signals.url = "https://gitlab.com/acme/web/-/issues/456";
  signals.domain = "gitlab.com";
  signals.title = "Issue";
  signals.word_count = 1800;
  signals.char_count = 9500;
  signals.heading_count = 6;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_GE(decision.score.final_score, 0.75);
  EXPECT_EQ("captures_after_run", decision.context_disclosure);
  EXPECT_TRUE(decision.suppression_reason.empty());
}

TEST(DaoAgentProactiveRankerTest,
     AnalyzeIssueShowsGitlabIssueWithIssuePageIdentity) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  const std::string url = "https://gitlab.com/acme/web/-/issues/456";
  ProactiveCandidate candidate = MakeSeedCandidateForUrl(url);
  ProactivePageSignals signals;
  signals.url = url;
  signals.domain = "gitlab.com";
  signals.title = "Navigation stalls · Issue #456 · acme / web";
  signals.meta_description = "Issue discussion and status";
  signals.word_count = 1800;
  signals.char_count = 9500;
  signals.heading_count = 6;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_TRUE(decision.suppression_reason.empty());
  EXPECT_GE(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest,
     AnalyzeIssueSuppressesGitlabPathWithoutIssuePageIdentity) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  const std::string url = "https://example.com/docs/gitlab/-/issues/456";
  ProactiveCandidate candidate = MakeSeedCandidateForUrl(url);
  ProactivePageSignals signals;
  signals.url = url;
  signals.domain = "example.com";
  signals.title = "GitLab route examples";
  signals.meta_description = "API fixture paths and routing examples";
  signals.word_count = 1800;
  signals.char_count = 9500;
  signals.heading_count = 6;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_page_hint", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, QuietSuppressesGenericShortDoc) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_docs_summary", "seed", R"((docs\.|documentation|readme|wiki|\.md))",
      "summarize_doc", true));
  ProactivePageSignals signals;
  signals.url = "https://example.com/readme";
  signals.domain = "example.com";
  signals.title = "Readme";
  signals.word_count = 120;
  signals.char_count = 500;
  signals.has_significant_content = false;
  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_LT(decision.score.final_score, 0.75);
  EXPECT_TRUE(decision.suppression_reason.empty());
}

TEST(DaoAgentProactiveRankerTest,
     WideDocsUrlWithoutPageHintEvidenceSuppresses) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  ScenarioDefinition scenario = MakeScenario(
      "seed_docs_summary", "seed", R"((docs\.|documentation|readme|wiki|\.md))",
      "summarize_doc", true);
  scenario.page_hints = R"(["documentation","API","guide","reference"])";
  auto candidate = MakeCandidate(std::move(scenario));
  ProactivePageSignals signals;
  signals.url = "https://example.com/wiki/team-offsite";
  signals.domain = "example.com";
  signals.title = "Team offsite planning";
  signals.meta_description = "Travel, dinner, and hotel logistics";
  signals.word_count = 2600;
  signals.char_count = 15000;
  signals.heading_count = 12;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_page_hint", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, DocsPathHintQualifiesDocumentationPage) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  ScenarioDefinition scenario = MakeScenario(
      "seed_docs_summary", "seed",
      R"((docs\.|documentation|readme|wiki|\.md|/(docs|api|reference|guide)(/|$|[?#])))",
      "summarize_doc", true);
  scenario.page_hints = R"(["docs","documentation","API","guide","reference"])";
  auto candidate = MakeCandidate(std::move(scenario));
  ProactivePageSignals signals;
  signals.url = "https://nextjs.org/docs/app/getting-started/installation";
  signals.domain = "nextjs.org";
  signals.title = "Installation";
  signals.meta_description = "Add Next.js to a new application";
  signals.word_count = 2200;
  signals.char_count = 12000;
  signals.heading_count = 9;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_TRUE(decision.suppression_reason.empty());
  EXPECT_GE(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest,
     AnalyzeProgressShowsWithProjectBoardStructure) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_linear_project", "seed", R"(^https://linear\.app/[^/]+/project/)",
      "analyze_progress", true));
  ProactivePageSignals signals;
  signals.url = "https://linear.app/dao/project/proactive-suggestions";
  signals.domain = "linear.app";
  signals.title = "Proactive suggestions · Project";
  signals.word_count = 1800;
  signals.char_count = 9500;
  signals.table_count = 1;
  signals.heading_count = 6;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_TRUE(decision.suppression_reason.empty());
  EXPECT_GE(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest,
     AnalyzeProgressSuppressesPlainProjectDescription) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_linear_project", "seed", R"(^https://linear\.app/[^/]+/project/)",
      "analyze_progress", true));
  ProactivePageSignals signals;
  signals.url = "https://linear.app/dao/project/proactive-suggestions";
  signals.domain = "linear.app";
  signals.title = "Proactive suggestions · Project";
  signals.word_count = 1800;
  signals.char_count = 9500;
  signals.heading_count = 6;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_action_evidence", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, ReviewCodeRequiresCodeEvidence) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_pr", "seed", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "review_code", true));
  ProactivePageSignals signals;
  signals.url = "https://github.com/dao/dao-browser/pull/42";
  signals.domain = "github.com";
  signals.title = "Pull request";
  signals.word_count = 2800;
  signals.char_count = 14000;
  signals.heading_count = 8;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_action_evidence", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest,
     ReviewCodeDoesNotTreatTableOnlyPageAsDiffEvidence) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_pr", "seed", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "review_code", true));
  ProactivePageSignals signals;
  signals.url = "https://github.com/dao/dao-browser/pull/42";
  signals.domain = "github.com";
  signals.title = "Pull request";
  signals.meta_description = "Conversation, checks, and status";
  signals.word_count = 2800;
  signals.char_count = 14000;
  signals.table_count = 3;
  signals.heading_count = 8;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_action_evidence", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, ExtractAnswerRequiresAnswerEvidence) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario("seed_stackoverflow", "seed",
                                              R"(stackoverflow\.com/questions)",
                                              "extract_answer", true));
  ProactivePageSignals signals;
  signals.url = "https://stackoverflow.com/questions/42/how-to-test";
  signals.domain = "stackoverflow.com";
  signals.title = "How to test";
  signals.word_count = 2400;
  signals.char_count = 14000;
  signals.heading_count = 8;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_action_evidence", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, ExtractAnswerShowsWithAnswerEvidence) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario("seed_stackoverflow", "seed",
                                              R"(stackoverflow\.com/questions)",
                                              "extract_answer", true));
  ProactivePageSignals signals;
  signals.url = "https://stackoverflow.com/questions/42/how-to-test";
  signals.domain = "stackoverflow.com";
  signals.title = "How to test";
  signals.word_count = 2400;
  signals.char_count = 14000;
  signals.heading_count = 8;
  signals.answer_count = 2;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_TRUE(decision.suppression_reason.empty());
  EXPECT_EQ("structured_content", decision.reason);
}

TEST(DaoAgentProactiveRankerTest,
     ExtractAnswerShowsForForumThreadWithReplies) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_stackoverflow", "seed", R"(^https://discuss\.[^/]+/)",
      "extract_answer", true));
  ProactivePageSignals signals;
  signals.url = "https://discuss.python.org/t/how-to-test/42";
  signals.domain = "python.org";
  signals.title = "How to test";
  signals.word_count = 2400;
  signals.char_count = 14000;
  signals.heading_count = 8;
  signals.answer_count = 2;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kAgentPanelCard, decision.tier);
  EXPECT_TRUE(decision.suppression_reason.empty());
  EXPECT_EQ("structured_content", decision.reason);
}

TEST(DaoAgentProactiveRankerTest,
     ExtractAnswerSuppressesForumThreadWithoutReplies) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_stackoverflow", "seed", R"(^https://discuss\.[^/]+/)",
      "extract_answer", true));
  ProactivePageSignals signals;
  signals.url = "https://discuss.python.org/t/how-to-test/42";
  signals.domain = "python.org";
  signals.title = "How to test";
  signals.word_count = 2400;
  signals.char_count = 14000;
  signals.heading_count = 8;
  signals.answer_count = 1;
  signals.has_significant_content = true;

  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("missing_action_evidence", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest, SensitivePageSuppresses) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_issue", "seed", R"(^https://github\.com/[^/]+/[^/]+/issues/\d+)",
      "analyze_issue", true));
  ProactivePageSignals signals =
      MakeSignals("https://github.com/dao/dao-browser/issues/9");
  signals.is_sensitive = true;
  ProactiveDecision decision = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("sensitive_page", decision.suppression_reason);
  EXPECT_DOUBLE_EQ(1.0, decision.score.privacy_sensitivity);
}

TEST(DaoAgentProactiveRankerTest, FatigueSuppressesAfterRepeatedDismissals) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_pr", "seed", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "review_code", true));
  ProactivePageSignals signals =
      MakeSignals("https://github.com/dao/dao-browser/pull/42");
  ProactiveFeedbackSignals feedback;
  feedback.cooldown_score = 3.0;
  ProactiveDecision decision =
      ranker.Rank(candidate, signals, feedback, base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("fatigue", decision.suppression_reason);
  EXPECT_NEAR(0.45, decision.score.fatigue_penalty, 1e-9);
}

TEST(DaoAgentProactiveRankerTest, PersonalScenarioScoresAboveSameSeedScenario) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  ScenarioDefinition seed = MakeScenario(
      "seed_custom", "seed", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "custom_review", true);
  ScenarioDefinition personal = seed;
  personal.id = "personal_custom";
  personal.type = "personal";
  personal.times_triggered = 10;
  personal.times_accepted = 7;
  personal.times_dismissed = 1;

  ProactivePageSignals signals =
      MakeSignals("https://github.com/dao/dao-browser/pull/42");
  ProactiveDecision seed_decision =
      ranker.Rank(MakeCandidate(std::move(seed)), signals,
                  ProactiveFeedbackSignals(), base::Time::Now());
  ProactiveDecision personal_decision =
      ranker.Rank(MakeCandidate(std::move(personal)), signals,
                  ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_GT(personal_decision.score.final_score,
            seed_decision.score.final_score);
  EXPECT_DOUBLE_EQ(0.20, personal_decision.score.user_fit);
}

TEST(DaoAgentProactiveRankerTest, PersonalScenarioSuppressesAfterPoorFit) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  ScenarioDefinition scenario = MakeScenario(
      "personal_pr", "personal", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "custom_review", true);
  scenario.times_triggered = 5;
  scenario.times_accepted = 0;
  scenario.times_dismissed = 4;
  ProactivePageSignals signals =
      MakeSignals("https://github.com/dao/dao-browser/pull/42");

  ProactiveDecision decision =
      ranker.Rank(MakeCandidate(std::move(scenario)), signals,
                  ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, decision.tier);
  EXPECT_EQ("poor_personal_fit", decision.suppression_reason);
  EXPECT_LT(decision.score.final_score, 0.75);
}

TEST(DaoAgentProactiveRankerTest,
     PersonalHighValueActionDoesNotGetSeedUsefulnessBoost) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  ScenarioDefinition scenario = MakeScenario(
      "personal_pr", "personal", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "review_code", true);
  ProactivePageSignals signals;
  signals.url = "https://github.com/dao/dao-browser/pull/42";
  signals.domain = "github.com";
  signals.title = "Pull request";
  signals.has_significant_content = false;
  ProactiveDecision decision =
      ranker.Rank(MakeCandidate(std::move(scenario)), signals,
                  ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_DOUBLE_EQ(0.0, decision.score.usefulness);
}

TEST(DaoAgentProactiveRankerTest, TypingSuppressesProactiveCard) {
  DaoAgentProactiveRanker ranker{DaoAgentProactiveRanker::Options()};
  auto candidate = MakeCandidate(MakeScenario(
      "seed_github_pr", "seed", R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)",
      "review_code", true));
  ProactivePageSignals signals =
      MakeSignals("https://github.com/dao/dao-browser/pull/42");
  signals.is_typing = true;

  ProactiveDecision typing = ranker.Rank(
      candidate, signals, ProactiveFeedbackSignals(), base::Time::Now());

  EXPECT_EQ(ProactivePresentationTier::kSilent, typing.tier);
  EXPECT_EQ("typing", typing.suppression_reason);
  EXPECT_GE(typing.score.interruption_cost, 1.0);
}

TEST(DaoAgentProactiveRankerTest, PageContextSuppressesSensitiveFallback) {
  ProactivePageSignals signals =
      MakeSignals("https://example.com/account/settings");
  signals.is_sensitive = true;

  EXPECT_EQ("sensitive_page",
            DaoAgentProactiveRanker::PageContextSuppressionReason(signals));
}

TEST(DaoAgentProactiveRankerTest, PageContextSuppressesTypingFallback) {
  ProactivePageSignals signals = MakeSignals("https://example.com/editor");
  signals.is_typing = true;

  EXPECT_EQ("typing",
            DaoAgentProactiveRanker::PageContextSuppressionReason(signals));
}

}  // namespace
}  // namespace dao
