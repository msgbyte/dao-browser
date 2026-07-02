// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_scenario_registry.h"

#include <algorithm>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

std::vector<std::string> MatchingScenarioIds(DaoAgentScenarioRegistry& registry,
                                             const std::string& url) {
  std::vector<std::string> ids;
  for (const ScenarioDefinition& scenario :
       registry.GetMatchingScenarios(url)) {
    ids.push_back(scenario.id);
  }
  return ids;
}

bool HasScenarioId(const std::vector<std::string>& ids,
                   const std::string& id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

TEST(DaoAgentScenarioRegistryTest, CodeReviewScenarioMatchesGitlabMergeRequest) {
  DaoAgentScenarioRegistry registry;

  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(
          registry,
          "https://gitlab.com/acme/web/-/merge_requests/123"),
      "seed_github_pr"));
}

TEST(DaoAgentScenarioRegistryTest, IssueScenarioMatchesGitlabIssue) {
  DaoAgentScenarioRegistry registry;

  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://gitlab.com/acme/web/-/issues/456"),
      "seed_github_issue"));
}

TEST(DaoAgentScenarioRegistryTest,
     DocsScenarioMatchesCommonDocumentationPaths) {
  DaoAgentScenarioRegistry registry;

  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(
          registry,
          "https://nextjs.org/docs/app/getting-started/installation"),
      "seed_docs_summary"));
  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://react.dev/reference/react/useEffect"),
      "seed_docs_summary"));
  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://developer.mozilla.org/en-US/docs/Web/API/fetch"),
      "seed_docs_summary"));
  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry, "https://example.com/guide/install"),
      "seed_docs_summary"));
}

TEST(DaoAgentScenarioRegistryTest,
     DocsScenarioKeepsBroadWikiUrlsForRankerFiltering) {
  DaoAgentScenarioRegistry registry;

  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry, "https://example.com/wiki/team-offsite"),
      "seed_docs_summary"));
}

TEST(DaoAgentScenarioRegistryTest, DocsScenarioDoesNotMatchApiSubstrings) {
  DaoAgentScenarioRegistry registry;

  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(registry, "https://example.com/apiary/catalog"),
      "seed_docs_summary"));
}

TEST(DaoAgentScenarioRegistryTest,
     StackOverflowScenarioMatchesQuestionAndForumHosts) {
  DaoAgentScenarioRegistry registry;

  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://stackoverflow.com/questions/42/how-to-test"),
      "seed_stackoverflow"));
  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://math.stackexchange.com/questions/42"),
      "seed_stackoverflow"));
  EXPECT_TRUE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://discuss.python.org/t/how-to-test/42"),
      "seed_stackoverflow"));
}

TEST(DaoAgentScenarioRegistryTest,
     StackOverflowScenarioIgnoresUrlExamplesInOtherHosts) {
  DaoAgentScenarioRegistry registry;

  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(
          registry,
          "https://example.com/docs/stackoverflow.com/questions/42"),
      "seed_stackoverflow"));
  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://example.com/docs/discuss.python.org/t/42"),
      "seed_stackoverflow"));
  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(registry,
                          "https://notstackoverflow.com/questions/42"),
      "seed_stackoverflow"));
}

TEST(DaoAgentScenarioRegistryTest,
     StackOverflowScenarioIgnoresForumIndexPages) {
  DaoAgentScenarioRegistry registry;

  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(registry, "https://discuss.python.org/"),
      "seed_stackoverflow"));
  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(registry, "https://discuss.python.org/latest"),
      "seed_stackoverflow"));
  EXPECT_FALSE(HasScenarioId(
      MatchingScenarioIds(registry, "https://discuss.python.org/c/core-dev/23"),
      "seed_stackoverflow"));
}

}  // namespace
}  // namespace dao
