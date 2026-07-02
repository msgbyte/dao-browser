// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_proactive_engine.h"

#include "base/time/time.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace dao {
namespace {

TEST(DaoAgentProactiveEngineTest,
     LegacyEpisodeSuggestionRequiresMatchingPathTemplate) {
  Episode episode;
  episode.path_template = "/projects/dao";

  EXPECT_TRUE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://linear.app/projects/dao?view=list#activity"));
  EXPECT_TRUE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://linear.app/projects/dao/"));

  EXPECT_FALSE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://linear.app/projects/other"));

  episode.path_template.clear();
  EXPECT_FALSE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://linear.app/projects/dao"));
}

TEST(DaoAgentProactiveEngineTest,
     LegacyEpisodeSuggestionRequiresMatchingHostScope) {
  Episode episode;
  episode.domain = "docs.google.com";
  episode.path_template = "/document/d/123";

  EXPECT_TRUE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://docs.google.com/document/d/123"));
  EXPECT_FALSE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://mail.google.com/document/d/123"));

  episode.domain = "google.com";
  EXPECT_TRUE(DoesLegacyEpisodeMatchPageForProactiveSuggestion(
      episode, "https://docs.google.com/document/d/123"));
}

TEST(DaoAgentProactiveEngineTest,
     SuggestionDomainFallsBackToHostForLocalAndIpPages) {
  EXPECT_EQ("github.com", GetDomainForProactiveSuggestion(
                              GURL("https://github.com/dao/dao-browser")));
  EXPECT_EQ("localhost", GetDomainForProactiveSuggestion(
                             GURL("http://localhost:3000/dashboard")));
  EXPECT_EQ("127.0.0.1",
            GetDomainForProactiveSuggestion(GURL("http://127.0.0.1:5173/app")));
  EXPECT_EQ("intranet",
            GetDomainForProactiveSuggestion(GURL("https://intranet/reports")));
}

TEST(DaoAgentProactiveEngineTest,
     SensitiveUrlKeepsDocumentationPagesEligible) {
  EXPECT_FALSE(IsSensitiveUrlForProactiveSuggestion(
      GURL("https://docs.stripe.com/payments/checkout"), "stripe.com"));
  EXPECT_FALSE(IsSensitiveUrlForProactiveSuggestion(
      GURL("https://example.com/docs/security/account-settings"),
      "example.com"));
  EXPECT_FALSE(IsSensitiveUrlForProactiveSuggestion(
      GURL("https://developer.mozilla.org/en-US/docs/Web/API/"
           "Credential_Management_API"),
      "mozilla.org"));
}

TEST(DaoAgentProactiveEngineTest, SensitiveUrlStillBlocksAccountActions) {
  EXPECT_TRUE(IsSensitiveUrlForProactiveSuggestion(
      GURL("https://example.com/checkout"), "example.com"));
  EXPECT_TRUE(IsSensitiveUrlForProactiveSuggestion(
      GURL("https://example.com/account/password"), "example.com"));
}

TEST(DaoAgentProactiveEngineTest,
     RecordsShownScenarioOnlyFromCompleteShownFeedback) {
  DaoAgentProactiveEngine engine(/*memory_service=*/nullptr,
                                /*profile=*/nullptr);
  const base::Time shown_time = base::Time::Now();

  engine.RecordShownScenarioForFeedback(
      /*url=*/"", "github.com", "review_code", "seed_github_pr", shown_time);
  EXPECT_FALSE(engine.HasShownScenarioForTesting(
      "https://github.com/dao/dao-browser/pull/42", "seed_github_pr"));
  EXPECT_TRUE(engine.GetLastDomainActionShownForTesting(
      "github.com", "review_code").is_null());

  engine.RecordShownScenarioForFeedback(
      "https://github.com/dao/dao-browser/pull/42", "github.com",
      "review_code", "seed_github_pr", shown_time);

  EXPECT_TRUE(engine.HasShownScenarioForTesting(
      "https://github.com/dao/dao-browser/pull/42", "seed_github_pr"));
  EXPECT_EQ(shown_time, engine.GetLastDomainActionShownForTesting(
                            "github.com", "review_code"));
}

TEST(DaoAgentProactiveEngineTest, ShownScenarioDedupUsesCanonicalPageUrl) {
  DaoAgentProactiveEngine engine(/*memory_service=*/nullptr,
                                /*profile=*/nullptr);

  engine.RecordShownScenarioForFeedback(
      "https://www.github.com/dao/dao-browser/pull/42/?"
      "utm_source=mail&b=2&a=1#discussion",
      "github.com", "review_code", "seed_github_pr", base::Time::Now());

  EXPECT_TRUE(engine.HasShownScenarioForTesting(
      "https://github.com/dao/dao-browser/pull/42?a=1&b=2#files",
      "seed_github_pr"));
  EXPECT_TRUE(engine.HasShownScenarioForTesting(
      "https://github.com/dao/dao-browser/pull/42?a=1&b=2&gclid=ad",
      "seed_github_pr"));
  EXPECT_FALSE(engine.HasShownScenarioForTesting(
      "https://github.com/dao/dao-browser/pull/43?a=1&b=2",
      "seed_github_pr"));
}

}  // namespace
}  // namespace dao
