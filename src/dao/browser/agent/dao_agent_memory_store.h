// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_STORE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_STORE_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

namespace sql {
class Database;
class MetaTable;
class Statement;
}  // namespace sql

namespace dao {

// Owns the SQLite database for agent memory. All methods must be called on
// the same sequence (enforced by SEQUENCE_CHECKER). The store handles schema
// creation, migrations, and CRUD operations.
class DaoAgentMemoryStore {
 public:
  explicit DaoAgentMemoryStore(const base::FilePath& db_path);
  ~DaoAgentMemoryStore();

  DaoAgentMemoryStore(const DaoAgentMemoryStore&) = delete;
  DaoAgentMemoryStore& operator=(const DaoAgentMemoryStore&) = delete;

  // Initialize database. Returns false on failure.
  bool Init();

  // Conversation messages
  bool SaveConversationMessages(const std::string& session_id,
                                const std::vector<ConversationMessage>& messages);
  std::vector<ConversationMessage> LoadConversationMessages(
      const std::string& session_id,
      int limit);
  std::vector<ConversationMessage> LoadRecentMessages(int limit);

  // Conversation summaries
  bool SaveConversationSummary(const ConversationSummary& summary);
  std::vector<ConversationSummary> LoadConversationSummaries(int limit);
  std::optional<ConversationSummary> FindSummaryByDomain(
      const std::string& domain);

  // Preferences
  bool SavePreference(const Preference& pref);
  bool MergePreference(const std::string& key,
                       const std::string& value,
                       double confidence);
  std::vector<Preference> GetPreferences(int limit, double min_confidence);
  bool DeletePreference(int64_t id);

  // Episodes
  bool SaveEpisode(const Episode& episode);
  std::vector<Episode> GetEpisodesByDomain(const std::string& domain,
                                           int limit);
  std::vector<Episode> SearchEpisodes(const std::string& query,
                                      const std::string& domain,
                                      int limit);
  bool UpdateEpisodeConfidence(int64_t id, double confidence);
  bool DeleteEpisode(int64_t id);

  // Deletion
  bool DeleteConversation(const std::string& session_id);
  bool ClearAll();

  // Stats
  StorageStats GetStorageStats();

  // Row limits
  bool EnforceRowLimits();

 private:
  bool CreateSchema();
  bool MigrateIfNeeded();
  void DatabaseErrorCallback(int error, sql::Statement* stmt);

  static constexpr int kCurrentSchemaVersion = 1;
  static constexpr int kMaxConversationRows = 10000;
  static constexpr int kMaxEpisodes = 500;
  static constexpr int kMaxPreferences = 100;

  base::FilePath db_path_;
  std::unique_ptr<sql::Database> db_;
  std::unique_ptr<sql::MetaTable> meta_table_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_STORE_H_
