// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_store.h"

#include <algorithm>
#include <tuple>

#include "base/logging.h"
#include "base/strings/strcat.h"
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

constexpr char kDreamReportColumns[] =
    "id, dream_date, report_markdown, habit_candidates, material_stats, "
    "status, attempt_count, trigger_kind, debug_material_json, viewed_at, "
    "created_at";

DreamReport DreamReportFromStatement(sql::Statement& stmt) {
  DreamReport r;
  r.id = stmt.ColumnInt64(0);
  r.dream_date = stmt.ColumnString(1);
  r.report_markdown = stmt.ColumnString(2);
  r.habit_candidates = stmt.ColumnString(3);
  r.material_stats = stmt.ColumnString(4);
  r.status = stmt.ColumnString(5);
  r.attempt_count = stmt.ColumnInt(6);
  r.trigger_kind = stmt.ColumnString(7);
  r.debug_material_json = stmt.ColumnString(8);
  if (stmt.GetColumnType(9) != sql::ColumnType::kNull) {
    r.viewed_at = TimeFromInt(stmt.ColumnInt64(9));
  }
  r.created_at = TimeFromInt(stmt.ColumnInt64(10));
  return r;
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

  InstallErrorCallback();

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

void DaoAgentMemoryStore::InstallErrorCallback() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  db_->set_error_callback(
      base::BindRepeating(&DaoAgentMemoryStore::DatabaseErrorCallback,
                          base::Unretained(this)));
}

bool DaoAgentMemoryStore::ExecuteOptionalSql(base::cstring_view sql) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  db_->reset_error_callback();
  const bool ok = db_->Execute(sql);
  if (!ok) {
    LOG(WARNING) << "Optional DaoAgentMemory SQL unavailable: "
                 << db_->GetErrorMessage();
  }
  InstallErrorCallback();
  return ok;
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
  std::ignore = ExecuteOptionalSql(
      "CREATE VIRTUAL TABLE IF NOT EXISTS episodes_fts USING fts5("
      "  intent, entities, outcome, content=episodes, content_rowid=id"
      ")");
  std::ignore = ExecuteOptionalSql(
      "CREATE VIRTUAL TABLE IF NOT EXISTS preferences_fts USING fts5("
      "  key, value, content=preferences, content_rowid=id"
      ")");

  // FTS sync triggers for episodes
  std::ignore = ExecuteOptionalSql(
      "CREATE TRIGGER IF NOT EXISTS episodes_ai AFTER INSERT ON episodes BEGIN"
      "  INSERT INTO episodes_fts(rowid, intent, entities, outcome)"
      "  VALUES (new.id, new.intent, new.entities, new.outcome);"
      "END");
  std::ignore = ExecuteOptionalSql(
      "CREATE TRIGGER IF NOT EXISTS episodes_ad AFTER DELETE ON episodes BEGIN"
      "  INSERT INTO episodes_fts(episodes_fts, rowid, intent, entities, "
      "outcome)"
      "  VALUES('delete', old.id, old.intent, old.entities, old.outcome);"
      "END");
  std::ignore = ExecuteOptionalSql(
      "CREATE TRIGGER IF NOT EXISTS episodes_au AFTER UPDATE ON episodes BEGIN"
      "  INSERT INTO episodes_fts(episodes_fts, rowid, intent, entities, "
      "outcome)"
      "  VALUES('delete', old.id, old.intent, old.entities, old.outcome);"
      "  INSERT INTO episodes_fts(rowid, intent, entities, outcome)"
      "  VALUES (new.id, new.intent, new.entities, new.outcome);"
      "END");

  // FTS sync triggers for preferences
  std::ignore = ExecuteOptionalSql(
      "CREATE TRIGGER IF NOT EXISTS preferences_ai AFTER INSERT ON preferences "
      "BEGIN"
      "  INSERT INTO preferences_fts(rowid, key, value)"
      "  VALUES (new.id, new.key, new.value);"
      "END");
  std::ignore = ExecuteOptionalSql(
      "CREATE TRIGGER IF NOT EXISTS preferences_ad AFTER DELETE ON preferences "
      "BEGIN"
      "  INSERT INTO preferences_fts(preferences_fts, rowid, key, value)"
      "  VALUES('delete', old.id, old.key, old.value);"
      "END");
  std::ignore = ExecuteOptionalSql(
      "CREATE TRIGGER IF NOT EXISTS preferences_au AFTER UPDATE ON preferences "
      "BEGIN"
      "  INSERT INTO preferences_fts(preferences_fts, rowid, key, value)"
      "  VALUES('delete', old.id, old.key, old.value);"
      "  INSERT INTO preferences_fts(rowid, key, value)"
      "  VALUES (new.id, new.key, new.value);"
      "END");

  // Scenarios table (personal scenarios only — seeds are hard-coded)
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS scenarios ("
          "  id TEXT PRIMARY KEY,"
          "  type TEXT NOT NULL,"
          "  name TEXT NOT NULL,"
          "  description TEXT,"
          "  url_pattern TEXT NOT NULL,"
          "  page_hints TEXT,"
          "  action_prompt TEXT NOT NULL,"
          "  action_label TEXT NOT NULL,"
          "  requires_page_content INTEGER DEFAULT 1,"
          "  times_triggered INTEGER DEFAULT 0,"
          "  times_accepted INTEGER DEFAULT 0,"
          "  times_dismissed INTEGER DEFAULT 0,"
          "  created_at INTEGER NOT NULL,"
          "  last_triggered_at INTEGER DEFAULT 0"
          ")")) {
    return false;
  }

  // Action feedback table
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS action_feedback ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  scenario_id TEXT,"
          "  action_label TEXT NOT NULL,"
          "  domain TEXT NOT NULL,"
          "  url TEXT NOT NULL,"
          "  trigger_confidence REAL,"
          "  outcome TEXT NOT NULL,"
          "  timestamp INTEGER NOT NULL,"
          "  session_id TEXT"
          ")")) {
    return false;
  }

  // Dream reports table (Dream Analysis archive; no FTS)
  if (!db_->Execute(
          "CREATE TABLE IF NOT EXISTS dream_reports ("
          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "  dream_date TEXT NOT NULL UNIQUE,"
          "  report_markdown TEXT NOT NULL DEFAULT '',"
          "  habit_candidates TEXT NOT NULL DEFAULT '[]',"
          "  material_stats TEXT NOT NULL DEFAULT '{}',"
          "  status TEXT NOT NULL,"
          "  attempt_count INTEGER NOT NULL DEFAULT 0,"
          "  trigger_kind TEXT NOT NULL,"
          "  debug_material_json TEXT NOT NULL DEFAULT '',"
          "  viewed_at INTEGER,"
          "  created_at INTEGER NOT NULL"
          ")")) {
    return false;
  }

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
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_feedback_lookup "
      "ON action_feedback(scenario_id, domain, timestamp)");
  std::ignore = db_->Execute(
      "CREATE INDEX IF NOT EXISTS idx_dream_reports_date "
      "ON dream_reports(dream_date)");

  return transaction.Commit();
}

bool DaoAgentMemoryStore::MigrateIfNeeded() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
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

  // v1 → v2: Add scenarios table, action_feedback table, and episode columns.
  if (version == 1) {
    // New columns on episodes (nullable, so existing rows are unaffected).
    std::ignore =
        db_->Execute("ALTER TABLE episodes ADD COLUMN user_action TEXT");
    std::ignore =
        db_->Execute("ALTER TABLE episodes ADD COLUMN action_result TEXT");
    std::ignore = meta_table_->SetVersionNumber(2);
    std::ignore = meta_table_->SetCompatibleVersionNumber(2);
    version = 2;
  }

  // v2 → v3: dream_reports table (created in CreateSchema via IF NOT
  // EXISTS; only the version number needs to move).
  if (version == 2) {
    std::ignore = meta_table_->SetVersionNumber(3);
    std::ignore = meta_table_->SetCompatibleVersionNumber(3);
    version = 3;
  }

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
      "outcome, timestamp, confidence, user_action, action_result) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
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
  stmt.BindString(10, episode.user_action);
  stmt.BindString(11, episode.action_result);
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
      "tools_used, outcome, timestamp, confidence, user_action, action_result "
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
    e.user_action = stmt.ColumnString(11);
    e.action_result = stmt.ColumnString(12);
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
      "e.confidence, e.user_action, e.action_result "
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
    e.user_action = stmt.ColumnString(11);
    e.action_result = stmt.ColumnString(12);
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
  std::ignore = db_->Execute("DELETE FROM scenarios");
  std::ignore = db_->Execute("DELETE FROM action_feedback");

  return transaction.Commit();
}

// --- Stats ---

StorageStats DaoAgentMemoryStore::GetStorageStats() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  StorageStats stats;
  if (!db_ || !db_->is_open()) {
    return stats;
  }

  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM conversations"));
    if (stmt.is_valid() && stmt.Step()) {
      stats.conversation_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM conversation_summaries"));
    if (stmt.is_valid() && stmt.Step()) {
      stats.summary_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM episodes"));
    if (stmt.is_valid() && stmt.Step()) {
      stats.episode_count = stmt.ColumnInt(0);
    }
  }
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE, "SELECT COUNT(*) FROM preferences"));
    if (stmt.is_valid() && stmt.Step()) {
      stats.preference_count = stmt.ColumnInt(0);
    }
  }
  {
    int64_t page_count = 0;
    int64_t page_size = 0;
    {
      sql::Statement stmt(
          db_->GetCachedStatement(SQL_FROM_HERE, "PRAGMA page_count"));
      if (stmt.is_valid() && stmt.Step()) {
        page_count = stmt.ColumnInt64(0);
      }
    }
    {
      sql::Statement stmt(
          db_->GetCachedStatement(SQL_FROM_HERE, "PRAGMA page_size"));
      if (stmt.is_valid() && stmt.Step()) {
        page_size = stmt.ColumnInt64(0);
      }
    }
    if (page_count > 0 && page_size > 0) {
      stats.total_size_bytes = page_count * page_size;
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

// --- Episodes (extended) ---

int DaoAgentMemoryStore::GetDomainEpisodeCountWithAction(
    const std::string& domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT COUNT(*) FROM episodes "
      "WHERE domain=? AND user_action IS NOT NULL AND user_action != ''"));
  stmt.BindString(0, domain);
  if (stmt.Step()) {
    return stmt.ColumnInt(0);
  }
  return 0;
}

// --- Scenarios ---

bool DaoAgentMemoryStore::SavePersonalScenario(
    const ScenarioDefinition& scenario) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO scenarios "
      "(id, type, name, description, url_pattern, page_hints, action_prompt, "
      "action_label, requires_page_content, times_triggered, times_accepted, "
      "times_dismissed, created_at, last_triggered_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
  stmt.BindString(0, scenario.id);
  stmt.BindString(1, scenario.type);
  stmt.BindString(2, scenario.name);
  stmt.BindString(3, scenario.description);
  stmt.BindString(4, scenario.url_pattern);
  stmt.BindString(5, scenario.page_hints);
  stmt.BindString(6, scenario.action_prompt);
  stmt.BindString(7, scenario.action_label);
  stmt.BindInt(8, scenario.requires_page_content ? 1 : 0);
  stmt.BindInt(9, scenario.times_triggered);
  stmt.BindInt(10, scenario.times_accepted);
  stmt.BindInt(11, scenario.times_dismissed);
  stmt.BindInt64(12, TimeToInt(scenario.created_at));
  stmt.BindInt64(13, TimeToInt(scenario.last_triggered_at));
  return stmt.Run();
}

std::vector<ScenarioDefinition> DaoAgentMemoryStore::GetPersonalScenarios() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  std::vector<ScenarioDefinition> result;
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT id, type, name, description, url_pattern, page_hints, "
      "action_prompt, action_label, requires_page_content, times_triggered, "
      "times_accepted, times_dismissed, created_at, last_triggered_at "
      "FROM scenarios ORDER BY created_at DESC"));

  while (stmt.Step()) {
    ScenarioDefinition s;
    s.id = stmt.ColumnString(0);
    s.type = stmt.ColumnString(1);
    s.name = stmt.ColumnString(2);
    s.description = stmt.ColumnString(3);
    s.url_pattern = stmt.ColumnString(4);
    s.page_hints = stmt.ColumnString(5);
    s.action_prompt = stmt.ColumnString(6);
    s.action_label = stmt.ColumnString(7);
    s.requires_page_content = stmt.ColumnInt(8) != 0;
    s.times_triggered = stmt.ColumnInt(9);
    s.times_accepted = stmt.ColumnInt(10);
    s.times_dismissed = stmt.ColumnInt(11);
    s.created_at = TimeFromInt(stmt.ColumnInt64(12));
    s.last_triggered_at = TimeFromInt(stmt.ColumnInt64(13));
    result.push_back(std::move(s));
  }
  return result;
}

bool DaoAgentMemoryStore::DeleteScenario(const std::string& id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(
      db_->GetCachedStatement(SQL_FROM_HERE,
                              "DELETE FROM scenarios WHERE id=?"));
  stmt.BindString(0, id);
  return stmt.Run();
}

bool DaoAgentMemoryStore::UpdateScenarioStats(const std::string& id,
                                               const std::string& stat_column) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // stat_column is one of: "times_triggered", "times_accepted",
  // "times_dismissed". We build the SQL dynamically but validate the column.
  if (stat_column != "times_triggered" && stat_column != "times_accepted" &&
      stat_column != "times_dismissed") {
    LOG(ERROR) << "Invalid stat column: " << stat_column;
    return false;
  }
  std::string sql = "UPDATE scenarios SET " + stat_column + "=" + stat_column +
                    "+1, last_triggered_at=? WHERE id=?";
  sql::Statement stmt(db_->GetUniqueStatement(sql));
  stmt.BindInt64(0, TimeToInt(base::Time::Now()));
  stmt.BindString(1, id);
  return stmt.Run();
}

// --- Action Feedback ---

bool DaoAgentMemoryStore::RecordActionFeedback(const ActionFeedback& feedback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT INTO action_feedback "
      "(scenario_id, action_label, domain, url, trigger_confidence, "
      "outcome, timestamp, session_id) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
  stmt.BindString(0, feedback.scenario_id);
  stmt.BindString(1, feedback.action_label);
  stmt.BindString(2, feedback.domain);
  stmt.BindString(3, feedback.url);
  stmt.BindDouble(4, feedback.trigger_confidence);
  stmt.BindString(5, feedback.outcome);
  stmt.BindInt64(6, TimeToInt(feedback.timestamp));
  stmt.BindString(7, feedback.session_id);
  return stmt.Run();
}

double DaoAgentMemoryStore::GetCooldownScore(const std::string& domain,
                                              const std::string& scenario_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Find the last "clicked" timestamp for this domain+scenario.
  int64_t last_click_ts = 0;
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT MAX(timestamp) FROM action_feedback "
        "WHERE scenario_id=? AND domain=? AND outcome='clicked'"));
    stmt.BindString(0, scenario_id);
    stmt.BindString(1, domain);
    if (stmt.Step() && stmt.GetColumnType(0) != sql::ColumnType::kNull) {
      last_click_ts = stmt.ColumnInt64(0);
    }
  }

  // Sum cooldown contributions since the last click.
  double score = 0.0;
  {
    sql::Statement stmt(db_->GetCachedStatement(
        SQL_FROM_HERE,
        "SELECT outcome, timestamp FROM action_feedback "
        "WHERE scenario_id=? AND domain=? AND timestamp>? "
        "ORDER BY timestamp ASC"));
    stmt.BindString(0, scenario_id);
    stmt.BindString(1, domain);
    stmt.BindInt64(2, last_click_ts);

    base::Time now = base::Time::Now();
    while (stmt.Step()) {
      std::string outcome = stmt.ColumnString(0);
      base::Time ts = TimeFromInt(stmt.ColumnInt64(1));
      // Apply 7-day decay: subtract weeks elapsed.
      int weeks_elapsed =
          (now - ts).InDays() / 7;
      double decay = static_cast<double>(weeks_elapsed);

      double contribution = 0.0;
      if (outcome == "dismissed") {
        contribution = 1.0;
      } else if (outcome == "ignored") {
        contribution = 0.5;
      }
      score += std::max(0.0, contribution - decay);
    }
  }
  return score;
}

bool DaoAgentMemoryStore::ClearDismissedFeedback() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "DELETE FROM action_feedback WHERE outcome='dismissed'"));
  return stmt.Run();
}

// --- Dream Reports ---

bool DaoAgentMemoryStore::SaveDreamReport(const DreamReport& report) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // Chromium's SQLite is built with SQLITE_OMIT_UPSERT, so
  // "ON CONFLICT ... DO UPDATE" is unavailable. INSERT OR REPLACE keeps
  // the upsert-by-dream_date semantics (the UNIQUE constraint replaces
  // the old row; a re-run intentionally resets viewed_at to unread).
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO dream_reports (dream_date, report_markdown, "
      "habit_candidates, material_stats, status, attempt_count, "
      "trigger_kind, debug_material_json, viewed_at, created_at) "
      "VALUES (?,?,?,?,?,?,?,?,NULL,?)"));
  stmt.BindString(0, report.dream_date);
  stmt.BindString(1, report.report_markdown);
  stmt.BindString(2, report.habit_candidates);
  stmt.BindString(3, report.material_stats);
  stmt.BindString(4, report.status);
  stmt.BindInt(5, report.attempt_count);
  stmt.BindString(6, report.trigger_kind);
  stmt.BindString(7, report.debug_material_json);
  stmt.BindInt64(8, TimeToInt(base::Time::Now()));
  return stmt.Run();
}

std::optional<DreamReport> DaoAgentMemoryStore::GetDreamReportByDate(
    const std::string& date) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      base::StrCat({"SELECT ", kDreamReportColumns,
                    " FROM dream_reports WHERE dream_date = ?"})));
  stmt.BindString(0, date);
  if (!stmt.Step()) {
    return std::nullopt;
  }
  return DreamReportFromStatement(stmt);
}

std::vector<DreamReport> DaoAgentMemoryStore::GetDreamReports(int limit) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  limit = std::clamp(limit, 1, 100);
  sql::Statement stmt(db_->GetUniqueStatement(
      base::StrCat({"SELECT ", kDreamReportColumns,
                    " FROM dream_reports WHERE status = 'completed' "
                    "ORDER BY dream_date DESC LIMIT ?"})));
  stmt.BindInt(0, limit);
  std::vector<DreamReport> reports;
  while (stmt.Step()) {
    reports.push_back(DreamReportFromStatement(stmt));
  }
  return reports;
}

std::optional<DreamReport> DaoAgentMemoryStore::GetLatestDreamReport() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      base::StrCat({"SELECT ", kDreamReportColumns,
                    " FROM dream_reports WHERE status = 'completed' "
                    "ORDER BY dream_date DESC LIMIT 1"})));
  if (!stmt.is_valid() || !stmt.Step()) {
    return std::nullopt;
  }
  return DreamReportFromStatement(stmt);
}

std::optional<DreamReport>
DaoAgentMemoryStore::GetLatestUnviewedDreamReport() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetUniqueStatement(
      base::StrCat({"SELECT ", kDreamReportColumns,
                    " FROM dream_reports WHERE viewed_at IS NULL "
                    "AND status = 'completed' "
                    "ORDER BY dream_date DESC LIMIT 1"})));
  if (!stmt.Step()) {
    return std::nullopt;
  }
  return DreamReportFromStatement(stmt);
}

bool DaoAgentMemoryStore::MarkDreamReportViewed(int64_t id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  sql::Statement stmt(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "UPDATE dream_reports SET viewed_at = ? WHERE id = ?"));
  stmt.BindInt64(0, TimeToInt(base::Time::Now()));
  stmt.BindInt64(1, id);
  return stmt.Run();
}

}  // namespace dao
