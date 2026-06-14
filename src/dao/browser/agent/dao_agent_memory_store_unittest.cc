// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_store.h"

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/threading/scoped_blocking_call.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

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
  MemorySqlQueryResult result =
      RunSql("SELECT key FROM preferences UNION ALL SELECT key FROM preferences",
             1);

  ASSERT_TRUE(result.ok) << result.error;
  ASSERT_EQ(1u, result.rows.size());
  EXPECT_TRUE(result.truncated);
}

}  // namespace
}  // namespace dao
