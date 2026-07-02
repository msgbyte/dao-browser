// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_store.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/thread_restrictions.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

ActionFeedback MakeActionFeedback(std::string outcome, base::Time timestamp) {
  ActionFeedback feedback;
  feedback.scenario_id = "seed_review_code";
  feedback.action_label = "review_code";
  feedback.domain = "example.com";
  feedback.url = "https://example.com/review";
  feedback.trigger_confidence = 0.9;
  feedback.outcome = std::move(outcome);
  feedback.timestamp = timestamp;
  return feedback;
}

Episode MakeEpisode(std::string title,
                    std::string outcome,
                    std::string action_result) {
  Episode episode;
  episode.domain = "example.com";
  episode.url = "https://example.com/task";
  episode.title = std::move(title);
  episode.intent = "review this page";
  episode.outcome = std::move(outcome);
  episode.action_result = std::move(action_result);
  episode.timestamp = base::Time::Now();
  episode.confidence = 0.95;
  return episode;
}

class DaoAgentMemoryStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    store_ = std::make_unique<DaoAgentMemoryStore>(
        temp_dir_.GetPath().AppendASCII("DaoAgentMemory.db"));
    ASSERT_TRUE(store_->Init());

    Preference pref;
    pref.key = "preferred_editor";
    pref.value = "vim";
    pref.confidence = 0.9;
    pref.evidence_count = 2;
    pref.last_updated = base::Time::Now();
    ASSERT_TRUE(store_->SavePreference(pref));
  }

  MemorySqlQueryResult RunSql(const std::string& sql, int max_rows = 100) {
    return store_->ExecuteReadOnlySqlForDebug(sql, max_rows);
  }

  base::ScopedAllowBlockingForTesting allow_blocking_;
  base::ScopedTempDir temp_dir_;
  std::unique_ptr<DaoAgentMemoryStore> store_;
};

TEST_F(DaoAgentMemoryStoreTest, ReadOnlySqlSelectReturnsColumnsAndRows) {
  MemorySqlQueryResult result =
      RunSql("SELECT key, value, confidence FROM preferences ORDER BY key");

  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(3u, result.columns.size());
  EXPECT_EQ("key", result.columns[0]);
  EXPECT_EQ("value", result.columns[1]);
  EXPECT_EQ("confidence", result.columns[2]);
  ASSERT_EQ(1u, result.rows.size());
  ASSERT_EQ(3u, result.rows[0].size());
  EXPECT_EQ("preferred_editor", result.rows[0][0].value);
  EXPECT_EQ("text", result.rows[0][0].type);
  EXPECT_EQ("vim", result.rows[0][1].value);
  EXPECT_EQ("real", result.rows[0][2].type);
  EXPECT_FALSE(result.truncated);
}

TEST_F(DaoAgentMemoryStoreTest, ReadOnlySqlPragmaTableInfoIsAllowed) {
  MemorySqlQueryResult result = RunSql("PRAGMA table_info(preferences)");

  ASSERT_TRUE(result.ok) << result.error;
  EXPECT_FALSE(result.columns.empty());
  EXPECT_FALSE(result.rows.empty());
}

TEST_F(DaoAgentMemoryStoreTest, ReadOnlySqlRejectsWrites) {
  MemorySqlQueryResult result = RunSql("DELETE FROM preferences");

  EXPECT_FALSE(result.ok);
  EXPECT_NE(std::string::npos, result.error.find("read-only"));
  EXPECT_EQ(1u, RunSql("SELECT key FROM preferences").rows.size());
}

TEST_F(DaoAgentMemoryStoreTest, ReadOnlySqlRejectsMultipleStatements) {
  MemorySqlQueryResult result = RunSql("SELECT 1; SELECT 2");

  EXPECT_FALSE(result.ok);
  EXPECT_NE(std::string::npos, result.error.find("single"));
}

TEST_F(DaoAgentMemoryStoreTest, ReadOnlySqlRejectsAttachDatabase) {
  MemorySqlQueryResult result = RunSql("ATTACH DATABASE '/tmp/x' AS x");

  EXPECT_FALSE(result.ok);
  EXPECT_NE(std::string::npos, result.error.find("read-only"));
}

TEST_F(DaoAgentMemoryStoreTest, ReadOnlySqlAppliesRowLimit) {
  MemorySqlQueryResult result = RunSql(
      "SELECT key FROM preferences UNION ALL SELECT key FROM preferences", 1);

  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(1u, result.rows.size());
  EXPECT_TRUE(result.truncated);
}

TEST_F(DaoAgentMemoryStoreTest, CooldownTreatsRecentNotNowAsTemporaryBlock) {
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(store_->RecordActionFeedback(
      MakeActionFeedback("not_now", now - base::Hours(2))));

  EXPECT_GE(store_->GetCooldownScore("example.com", "seed_review_code"), 3.0);

  ActionFeedback older_feedback =
      MakeActionFeedback("not_now", now - base::Days(2));
  older_feedback.domain = "old.example.com";
  ASSERT_TRUE(store_->RecordActionFeedback(older_feedback));

  EXPECT_DOUBLE_EQ(
      1.0, store_->GetCooldownScore("old.example.com", "seed_review_code"));
}

TEST_F(DaoAgentMemoryStoreTest, CooldownTreatsRecentFailureAsTemporaryBlock) {
  const base::Time now = base::Time::Now();
  ASSERT_TRUE(store_->RecordActionFeedback(
      MakeActionFeedback("failed", now - base::Minutes(30))));

  EXPECT_GE(store_->GetCooldownScore("example.com", "seed_review_code"), 3.0);

  ActionFeedback older_feedback =
      MakeActionFeedback("failed", now - base::Hours(2));
  older_feedback.domain = "old.example.com";
  ASSERT_TRUE(store_->RecordActionFeedback(older_feedback));

  EXPECT_DOUBLE_EQ(
      0.25, store_->GetCooldownScore("old.example.com", "seed_review_code"));
}

TEST_F(DaoAgentMemoryStoreTest, EpisodesByDomainSkipsNegativeActionResults) {
  ASSERT_TRUE(store_->SaveEpisode(
      MakeEpisode("Bad repeat", "completed", "not_helpful")));
  ASSERT_TRUE(store_->SaveEpisode(MakeEpisode("Failed repeat", "failed", "")));
  ASSERT_TRUE(store_->SaveEpisode(
      MakeEpisode("Useful repeat", "completed", "helpful")));

  std::vector<Episode> episodes =
      store_->GetEpisodesByDomain("example.com", /*limit=*/10);

  ASSERT_EQ(1u, episodes.size());
  EXPECT_EQ("Useful repeat", episodes[0].title);
}

TEST_F(DaoAgentMemoryStoreTest, EpisodesByDomainMatchesSavedSubdomains) {
  Episode episode = MakeEpisode("Docs workflow", "completed", "helpful");
  episode.domain = "docs.google.com";
  ASSERT_TRUE(store_->SaveEpisode(episode));

  std::vector<Episode> episodes =
      store_->GetEpisodesByDomain("google.com", /*limit=*/10);

  ASSERT_EQ(1u, episodes.size());
  EXPECT_EQ("Docs workflow", episodes[0].title);
  EXPECT_EQ("docs.google.com", episodes[0].domain);
}

TEST_F(DaoAgentMemoryStoreTest,
       EpisodesByDomainTreatsLikeWildcardsAsLiteralDomainCharacters) {
  Episode matching_episode =
      MakeEpisode("Matching internal workflow", "completed", "helpful");
  matching_episode.domain = "docs.foo_bar.example.com";
  ASSERT_TRUE(store_->SaveEpisode(matching_episode));

  Episode wildcard_episode =
      MakeEpisode("Wildcard internal workflow", "completed", "helpful");
  wildcard_episode.domain = "docs.fooXbar.example.com";
  ASSERT_TRUE(store_->SaveEpisode(wildcard_episode));

  std::vector<Episode> episodes =
      store_->GetEpisodesByDomain("foo_bar.example.com", /*limit=*/10);

  ASSERT_EQ(1u, episodes.size());
  EXPECT_EQ("Matching internal workflow", episodes[0].title);
  EXPECT_EQ("docs.foo_bar.example.com", episodes[0].domain);
}

TEST_F(DaoAgentMemoryStoreTest, SearchEpisodesSkipsNegativeActionResults) {
  ASSERT_TRUE(store_->SaveEpisode(
      MakeEpisode("Bad search", "completed", "not_helpful")));
  ASSERT_TRUE(store_->SaveEpisode(MakeEpisode("Failed search", "failed", "")));
  ASSERT_TRUE(store_->SaveEpisode(
      MakeEpisode("Useful search", "completed", "helpful")));

  std::vector<Episode> episodes =
      store_->SearchEpisodes("review", "example.com", /*limit=*/10);

  ASSERT_EQ(1u, episodes.size());
  EXPECT_EQ("Useful search", episodes[0].title);
}

TEST_F(DaoAgentMemoryStoreTest, SearchEpisodesMatchesSavedSubdomains) {
  Episode episode = MakeEpisode("Docs workflow", "completed", "helpful");
  episode.domain = "docs.google.com";
  ASSERT_TRUE(store_->SaveEpisode(episode));

  std::vector<Episode> episodes =
      store_->SearchEpisodes("review", "google.com", /*limit=*/10);

  ASSERT_EQ(1u, episodes.size());
  EXPECT_EQ("Docs workflow", episodes[0].title);
  EXPECT_EQ("docs.google.com", episodes[0].domain);
}

TEST_F(DaoAgentMemoryStoreTest,
       SearchEpisodesTreatsLikeWildcardsAsLiteralDomainCharacters) {
  Episode matching_episode =
      MakeEpisode("Matching internal search", "completed", "helpful");
  matching_episode.domain = "docs.foo_bar.example.com";
  ASSERT_TRUE(store_->SaveEpisode(matching_episode));

  Episode wildcard_episode =
      MakeEpisode("Wildcard internal search", "completed", "helpful");
  wildcard_episode.domain = "docs.fooXbar.example.com";
  ASSERT_TRUE(store_->SaveEpisode(wildcard_episode));

  std::vector<Episode> episodes =
      store_->SearchEpisodes("review", "foo_bar.example.com", /*limit=*/10);

  ASSERT_EQ(1u, episodes.size());
  EXPECT_EQ("Matching internal search", episodes[0].title);
  EXPECT_EQ("docs.foo_bar.example.com", episodes[0].domain);
}

}  // namespace
}  // namespace dao
