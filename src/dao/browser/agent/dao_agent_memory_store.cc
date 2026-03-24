// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_store.h"

#include <algorithm>
#include <tuple>

#include "base/logging.h"
#include "sql/database.h"
#include "sql/meta_table.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace dao {

namespace {

base::Time TimeFromInt(int64_t t) {
  return base::Time::FromDeltaSinceWindowsEpoch(base::Microseconds(t));
}

int64_t TimeToInt(base::Time t) {
  return t.ToDeltaSinceWindowsEpoch().InMicroseconds();
}

}  // namespace

DaoAgentMemoryStore::DaoAgentMemoryStore(const base::FilePath& db_path)
    : db_path_(db_path) {
  // The store is created on the UI thread but used on a background sequence.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

DaoAgentMemoryStore::~DaoAgentMemoryStore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

bool DaoAgentMemoryStore::Init() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  db_ = std::make_unique<sql::Database>(sql::DatabaseOptions(),
                                        sql::Database::Tag("DaoAgentMemory"));

  db_->set_error_callback(
      base::BindRepeating(&DaoAgentMemoryStore::DatabaseErrorCallback,
                          base::Unretained(this)));

  if (!db_->Open(db_path_)) {
    LOG(ERROR) << "Failed to open DaoAgentMemory database";
    return false;
  }

  // Enable WAL mode for better concurrent read/write performance.
  std::ignore = db_->Execute("PRAGMA journal_mode=WAL");

  meta_table_ = std::make_unique<sql::MetaTable>();
  if (!meta_table_->Init(db_.get(), kCurrentSchemaVersion,
                         kCurrentSchemaVersion)) {
    LOG(ERROR) << "Failed to init meta table";
    return false;
  }

  if (!MigrateIfNeeded()) {
    return false;
  }

  if (!CreateSchema()) {
    return false;
  }

  return true;
}

bool DaoAgentMemoryStore::CreateSchema() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return false;
  }

  // Conversations table
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS conversations ("
          "  id INTEGER PRIMARY KEY,"
          "  session_id TEXT NOT NULL,"
          "  role TEXT NOT NULL,"
          "  content TEXT NOT NULL,"
          "  timestamp INTEGER NOT NULL,"
          "  page_url TEXT,"
          "  page_title TEXT"
          ")")) {
    return false;
  }

  // Conversation summaries table
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS conversation_summaries ("
          "  id INTEGER PRIMARY KEY,"
          "  session_id TEXT NOT NULL,"
          "  summary TEXT NOT NULL,"
          "  message_count INTEGER,"
          "  first_timestamp INTEGER,"
          "  last_timestamp INTEGER,"
          "  primary_domain TEXT"
          ")")) {
    return false;
  }

  // Preferences table
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS preferences ("
          "  id INTEGER PRIMARY KEY,"
          "  key TEXT UNIQUE NOT NULL,"
          "  value TEXT NOT NULL,"
          "  confidence REAL DEFAULT 0.5,"
          "  evidence_count INTEGER DEFAULT 1,"
          "  last_updated INTEGER NOT NULL"
          ")")) {
    return false;
  }

  // Episodes table
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS episodes ("
          "  id INTEGER PRIMARY KEY,"
          "  domain TEXT NOT NULL,"
          "  path_template TEXT,"
          "  url TEXT NOT NULL,"
          "  title TEXT,"
          "  intent TEXT,"
          "  entities TEXT,"
          "  tools_used TEXT,"
          "  outcome TEXT,"
          "  timestamp INTEGER NOT NULL,"
          "  confidence REAL DEFAULT 0.7"
          ")")) {
    return false;
  }

  // FTS5 virtual tables (non-critical; may fail on older SQLite builds).
  std::ignore = db_->Execute(
      "CREATE VIRTUAL TABLE IF NOT EXISTS episodes_fts USING fts5("
      "  intent, entities, outcome, content=episodes, content_rowid=id"
      ")");
  std::ignore = db_->Execute(
      "CREATE VIRTUAL TABLE IF NOT EXISTS preferences_fts USING fts5("
      "  key, value, content=preferences, content_rowid=id"
      ")");

  // FTS sync triggers for episodes
  std::ignore = db_->Execute(
      "CREATE TRIGGER IF NOT EXISTS episodes_ai AFTER INSERT ON episodes BEGIN"
      "  INSERT INTO episodes_fts(rowid, intent, entities, outcome)"
      "  VALUES (new.id, new.intent, new.entities, new.outcome);"
      "END");
  std::ignore = db_->Execute(
      "CREATE TRIGGER IF NOT EXISTS episodes_ad AFTER DELETE ON episodes BEGIN"
      "  INSERT INTO episodes_fts(episodes_fts, rowid, intent, entities, "
      "outcome)"
      "  VALUES('delete', old.id, old.intent, old.entities, old.outcome);"
      "END");
  std::ignore = db_->Execute(
      "CREATE TRIGGER IF NOT EXISTS episodes_au AFTER UPDATE ON episodes BEGIN"
      "  INSERT INTO episodes_fts(episodes_fts, rowid, intent, entities, "
      "outcome)"
      "  VALUES('delete', old.id, old.intent, old.entities, old.outcome);"
      "  INSERT INTO episodes_fts(rowid, intent, entities, outcome)"
      "  VALUES (new.id, new.intent, new.entities, new.outcome);"
      "END");

  // FTS sync triggers for preferences
  std::ignore = db_->Execute(
      "CREATE TRIGGER IF NOT EXISTS preferences_ai AFTER INSERT ON preferences "
      "BEGIN"
      "  INSERT INTO preferences_fts(rowid, key, value)"
      "  VALUES (new.id, new.key, new.value);"
      "END");
  std::ignore = db_->Execute(
      "CREATE TRIGGER IF NOT EXISTS preferences_ad AFTER DELETE ON preferences "
      "BEGIN"
      "  INSERT INTO preferences_fts(preferences_fts, rowid, key, value)"
      "  VALUES('delete', old.id, old.key, old.value);"
      "END");
  std::ignore = db_->Execute(
      "CREATE TRIGGER IF NOT EXISTS preferences_au AFTER UPDATE ON preferences "
      "BEGIN"
      "  INSERT INTO preferences_fts(preferences_fts, rowid, key, value)"
      "  VALUES('delete', old.id, old.key, old.value);"
      "  INSERT INTO preferences_fts(rowid, key, value)"
      "  VALUES (new.id, new.key, new.value);"
      "END");

  // Indexes
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_conversations_session "
      "ON conversations(session_id)");
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_conversations_page_url "
      "ON conversations(page_url)");
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_conversations_timestamp "
      "ON conversations(timestamp DESC)");
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_episodes_domain "
      "ON episodes(domain)");
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_episodes_timestamp "
      "ON episodes(timestamp DESC)");
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_summaries_domain "
      "ON conversation_summaries(primary_domain)");

  return transaction.Commit();
}

bool DaoAgentMemoryStore::MigrateIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Version 1 is the initial schema. Future migrations go here.
  int version = meta_table_->GetVersionNumber();
  if (version == kCurrentSchemaVersion) {
    return true;
  }
  // If version is newer than expected, reset.
  if (version > kCurrentSchemaVersion) {
    LOG(WARNING) << "DaoAgentMemory DB version " << version
                 << " is newer than expected " << kCurrentSchemaVersion;
    return false;
  }
  // Future migration logic goes here.
  return true;
}

void DaoAgentMemoryStore::DatabaseErrorCallback(int error,
                                                 sql::Statement* stmt) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  LOG(ERROR) << "DaoAgentMemory database error: " << error;

  if (sql::Database::IsExpectedSqliteError(error)) {
    return;
  }

  // Raze and recreate on corruption.
  db_->RazeAndPoison();
}

// --- Conversation Messages ---

bool DaoAgentMemoryStore::SaveConversationMessages(
    const std::string& session_id,
    const std::vector<ConversationMessage>& messages) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return false;
  }

  // Delete existing messages for this session (idempotent overwrite).
  sql::Statement delete_stmt(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "DELETE FROM conversations WHERE session_id=?"));
  delete_stmt.BindString(0, session_id);
  if (!delete_stmt.Run()) {
    return false;
  }

  // Insert all messages.
  sql::Statement insert_stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO conversations "
      "(session_id, role, content, timestamp, page_url, page_title) "
      "VALUES (?, ?, ?, ?, ?, ?)"));

  for (const auto& msg : messages) {
    insert_stmt.Reset(true);
    insert_stmt.BindString(0, session_id);
    insert_stmt.BindString(1, msg.role);
    insert_stmt.BindString(2, msg.content);
    insert_stmt.BindInt64(3, TimeToInt(msg.timestamp));
    insert_stmt.BindString(4, msg.page_url);
    insert_stmt.BindString(5, msg.page_title);
    if (!insert_stmt.Run()) {
      return false;
    }
  }

  return transaction.Commit();
}

std::vector<ConversationMessage> DaoAgentMemoryStore::LoadConversationMessages(
    const std::string& session_id,
    int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<ConversationMessage> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, session_id, role, content, timestamp, page_url, page_title "
      "FROM conversations WHERE session_id=? "
      "ORDER BY timestamp ASC LIMIT ?"));
  stmt.BindString(0, session_id);
  stmt.BindInt(1, limit);

  while (stmt.Step()) {
    ConversationMessage msg;
    msg.id = stmt.ColumnInt64(0);
    msg.session_id = stmt.ColumnString(1);
    msg.role = stmt.ColumnString(2);
    msg.content = stmt.ColumnString(3);
    msg.timestamp = TimeFromInt(stmt.ColumnInt64(4));
    msg.page_url = stmt.ColumnString(5);
    msg.page_title = stmt.ColumnString(6);
    result.push_back(std::move(msg));
  }
  return result;
}

std::vector<ConversationMessage> DaoAgentMemoryStore::LoadRecentMessages(
    int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<ConversationMessage> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, session_id, role, content, timestamp, page_url, page_title "
      "FROM conversations ORDER BY timestamp DESC LIMIT ?"));
  stmt.BindInt(0, limit);

  while (stmt.Step()) {
    ConversationMessage msg;
    msg.id = stmt.ColumnInt64(0);
    msg.session_id = stmt.ColumnString(1);
    msg.role = stmt.ColumnString(2);
    msg.content = stmt.ColumnString(3);
    msg.timestamp = TimeFromInt(stmt.ColumnInt64(4));
    msg.page_url = stmt.ColumnString(5);
    msg.page_title = stmt.ColumnString(6);
    result.push_back(std::move(msg));
  }
  // Reverse to get chronological order.
  std::reverse(result.begin(), result.end());
  return result;
}

// --- Conversation Summaries ---

bool DaoAgentMemoryStore::SaveConversationSummary(
    const ConversationSummary& summary) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO conversation_summaries "
      "(session_id, summary, message_count, first_timestamp, last_timestamp, "
      "primary_domain) VALUES (?, ?, ?, ?, ?, ?)"));
  stmt.BindString(0, summary.session_id);
  stmt.BindString(1, summary.summary);
  stmt.BindInt(2, summary.message_count);
  stmt.BindInt64(3, TimeToInt(summary.first_timestamp));
  stmt.BindInt64(4, TimeToInt(summary.last_timestamp));
  stmt.BindString(5, summary.primary_domain);
  return stmt.Run();
}

std::vector<ConversationSummary>
DaoAgentMemoryStore::LoadConversationSummaries(int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<ConversationSummary> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, session_id, summary, message_count, first_timestamp, "
      "last_timestamp, primary_domain "
      "FROM conversation_summaries ORDER BY last_timestamp DESC LIMIT ?"));
  stmt.BindInt(0, limit);

  while (stmt.Step()) {
    ConversationSummary s;
    s.id = stmt.ColumnInt64(0);
    s.session_id = stmt.ColumnString(1);
    s.summary = stmt.ColumnString(2);
    s.message_count = stmt.ColumnInt(3);
    s.first_timestamp = TimeFromInt(stmt.ColumnInt64(4));
    s.last_timestamp = TimeFromInt(stmt.ColumnInt64(5));
    s.primary_domain = stmt.ColumnString(6);
    result.push_back(std::move(s));
  }
  return result;
}

std::optional<ConversationSummary>
DaoAgentMemoryStore::FindSummaryByDomain(const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, session_id, summary, message_count, first_timestamp, "
      "last_timestamp, primary_domain "
      "FROM conversation_summaries WHERE primary_domain=? "
      "ORDER BY last_timestamp DESC LIMIT 1"));
  stmt.BindString(0, domain);

  if (stmt.Step()) {
    ConversationSummary s;
    s.id = stmt.ColumnInt64(0);
    s.session_id = stmt.ColumnString(1);
    s.summary = stmt.ColumnString(2);
    s.message_count = stmt.ColumnInt(3);
    s.first_timestamp = TimeFromInt(stmt.ColumnInt64(4));
    s.last_timestamp = TimeFromInt(stmt.ColumnInt64(5));
    s.primary_domain = stmt.ColumnString(6);
    return s;
  }
  return std::nullopt;
}

// --- Preferences ---

bool DaoAgentMemoryStore::SavePreference(const Preference& pref) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO preferences "
      "(key, value, confidence, evidence_count, last_updated) "
      "VALUES (?, ?, ?, ?, ?)"));
  stmt.BindString(0, pref.key);
  stmt.BindString(1, pref.value);
  stmt.BindDouble(2, pref.confidence);
  stmt.BindInt(3, pref.evidence_count);
  stmt.BindInt64(4, TimeToInt(pref.last_updated));
  return stmt.Run();
}

bool DaoAgentMemoryStore::MergePreference(const std::string& key,
                                           const std::string& value,
                                           double confidence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Try to find existing preference.
  sql::Statement select_stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, confidence, evidence_count FROM preferences WHERE key=?"));
  select_stmt.BindString(0, key);

  if (select_stmt.Step()) {
    // Existing preference — merge confidence.
    double old_confidence = select_stmt.ColumnDouble(1);
    int old_count = select_stmt.ColumnInt(2);
    double merged_confidence =
        (old_confidence * old_count + confidence) / (old_count + 1);

    sql::Statement update_stmt(db_->GetCachedStatement(
        SQL_FROM_HERE,
        "UPDATE preferences SET value=?, confidence=?, "
        "evidence_count=evidence_count+1, last_updated=? WHERE key=?"));
    update_stmt.BindString(0, value);
    update_stmt.BindDouble(1, merged_confidence);
    update_stmt.BindInt64(2, TimeToInt(base::Time::Now()));
    update_stmt.BindString(3, key);
    return update_stmt.Run();
  }

  // New preference.
  Preference pref;
  pref.key = key;
  pref.value = value;
  pref.confidence = confidence;
  pref.evidence_count = 1;
  pref.last_updated = base::Time::Now();
  return SavePreference(pref);
}

std::vector<Preference> DaoAgentMemoryStore::GetPreferences(
    int limit,
    double min_confidence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<Preference> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, key, value, confidence, evidence_count, last_updated "
      "FROM preferences WHERE confidence>=? "
      "ORDER BY confidence DESC LIMIT ?"));
  stmt.BindDouble(0, min_confidence);
  stmt.BindInt(1, limit);

  while (stmt.Step()) {
    Preference p;
    p.id = stmt.ColumnInt64(0);
    p.key = stmt.ColumnString(1);
    p.value = stmt.ColumnString(2);
    p.confidence = stmt.ColumnDouble(3);
    p.evidence_count = stmt.ColumnInt(4);
    p.last_updated = TimeFromInt(stmt.ColumnInt64(5));
    result.push_back(std::move(p));
  }
  return result;
}

bool DaoAgentMemoryStore::DeletePreference(int64_t id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "DELETE FROM preferences WHERE id=?"));
  stmt.BindInt64(0, id);
  return stmt.Run();
}

// --- Episodes ---

bool DaoAgentMemoryStore::SaveEpisode(const Episode& episode) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO episodes "
      "(domain, path_template, url, title, intent, entities, tools_used, "
      "outcome, timestamp, confidence) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
  stmt.BindString(0, episode.domain);
  stmt.BindString(1, episode.path_template);
  stmt.BindString(2, episode.url);
  stmt.BindString(3, episode.title);
  stmt.BindString(4, episode.intent);
  stmt.BindString(5, episode.entities);
  stmt.BindString(6, episode.tools_used);
  stmt.BindString(7, episode.outcome);
  stmt.BindInt64(8, TimeToInt(episode.timestamp));
  stmt.BindDouble(9, episode.confidence);
  return stmt.Run();
}

std::vector<Episode> DaoAgentMemoryStore::GetEpisodesByDomain(
    const std::string& domain,
    int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<Episode> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, domain, path_template, url, title, intent, entities, "
      "tools_used, outcome, timestamp, confidence "
      "FROM episodes WHERE domain=? "
      "ORDER BY timestamp DESC LIMIT ?"));
  stmt.BindString(0, domain);
  stmt.BindInt(1, limit);

  while (stmt.Step()) {
    Episode e;
    e.id = stmt.ColumnInt64(0);
    e.domain = stmt.ColumnString(1);
    e.path_template = stmt.ColumnString(2);
    e.url = stmt.ColumnString(3);
    e.title = stmt.ColumnString(4);
    e.intent = stmt.ColumnString(5);
    e.entities = stmt.ColumnString(6);
    e.tools_used = stmt.ColumnString(7);
    e.outcome = stmt.ColumnString(8);
    e.timestamp = TimeFromInt(stmt.ColumnInt64(9));
    e.confidence = stmt.ColumnDouble(10);
    result.push_back(std::move(e));
  }
  return result;
}

std::vector<Episode> DaoAgentMemoryStore::SearchEpisodes(
    const std::string& query,
    const std::string& domain,
    int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<Episode> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT e.id, e.domain, e.path_template, e.url, e.title, "
      "e.intent, e.entities, e.tools_used, e.outcome, e.timestamp, "
      "e.confidence "
      "FROM episodes e "
      "JOIN episodes_fts f ON e.id = f.rowid "
      "WHERE episodes_fts MATCH ? AND e.domain=? "
      "ORDER BY rank LIMIT ?"));
  stmt.BindString(0, query);
  stmt.BindString(1, domain);
  stmt.BindInt(2, limit);

  while (stmt.Step()) {
    Episode e;
    e.id = stmt.ColumnInt64(0);
    e.domain = stmt.ColumnString(1);
    e.path_template = stmt.ColumnString(2);
    e.url = stmt.ColumnString(3);
    e.title = stmt.ColumnString(4);
    e.intent = stmt.ColumnString(5);
    e.entities = stmt.ColumnString(6);
    e.tools_used = stmt.ColumnString(7);
    e.outcome = stmt.ColumnString(8);
    e.timestamp = TimeFromInt(stmt.ColumnInt64(9));
    e.confidence = stmt.ColumnDouble(10);
    result.push_back(std::move(e));
  }
  return result;
}

bool DaoAgentMemoryStore::UpdateEpisodeConfidence(int64_t id,
                                                   double confidence) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "UPDATE episodes SET confidence=? WHERE id=?"));
  stmt.BindDouble(0, confidence);
  stmt.BindInt64(1, id);
  return stmt.Run();
}

bool DaoAgentMemoryStore::DeleteEpisode(int64_t id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "DELETE FROM episodes WHERE id=?"));
  stmt.BindInt64(0, id);
  return stmt.Run();
}

// --- Deletion ---

bool DaoAgentMemoryStore::DeleteConversation(const std::string& session_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return false;
  }

  sql::Statement del_msgs(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "DELETE FROM conversations WHERE session_id=?"));
  del_msgs.BindString(0, session_id);
  if (!del_msgs.Run()) {
    return false;
  }

  sql::Statement del_summary(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM conversation_summaries WHERE session_id=?"));
  del_summary.BindString(0, session_id);
  if (!del_summary.Run()) {
    return false;
  }

  return transaction.Commit();
}

bool DaoAgentMemoryStore::ClearAll() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return false;
  }

  std::ignore = db_->Execute("DELETE FROM conversations");
  std::ignore = db_->Execute("DELETE FROM conversation_summaries");
  std::ignore = db_->Execute("DELETE FROM preferences");
  std::ignore = db_->Execute("DELETE FROM episodes");

  return transaction.Commit();
}

// --- Stats ---

StorageStats DaoAgentMemoryStore::GetStorageStats() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  StorageStats stats;

  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM conversations"));
    if (stmt.Step()) {
      stats.conversation_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM conversation_summaries"));
    if (stmt.Step()) {
      stats.summary_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM episodes"));
    if (stmt.Step()) {
      stats.episode_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM preferences"));
    if (stmt.Step()) {
      stats.preference_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT page_count * page_size FROM pragma_page_count, "
                       "pragma_page_size"));
    if (stmt.Step()) {
      stats.total_size_bytes = stmt.ColumnInt64(0);
    }
  }
  return stats;
}

bool DaoAgentMemoryStore::EnforceRowLimits() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return false;
  }

  // Trim conversations (keep newest).
  std::ignore = db_->Execute(
      "DELETE FROM conversations WHERE id NOT IN ("
      "  SELECT id FROM conversations ORDER BY timestamp DESC LIMIT 10000"
      ")");

  // Trim episodes (keep highest confidence).
  std::ignore = db_->Execute(
      "DELETE FROM episodes WHERE id NOT IN ("
      "  SELECT id FROM episodes ORDER BY confidence DESC, timestamp DESC "
      "LIMIT 500"
      ")");

  // Trim preferences (keep highest confidence).
  std::ignore = db_->Execute(
      "DELETE FROM preferences WHERE id NOT IN ("
      "  SELECT id FROM preferences ORDER BY confidence DESC LIMIT 100"
      ")");

  return transaction.Commit();
}

}  // namespace dao
