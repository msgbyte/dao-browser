// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_chromium_profile_adapter.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted.h"
#include "base/strings/cstring_view.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/sessions/core/command_storage_backend.h"
#include "components/sessions/core/command_storage_manager.h"
#include "components/sessions/core/serialized_navigation_entry.h"
#include "components/sessions/core/session_service_commands.h"
#include "components/sessions/core/session_types.h"
#include "sql/database.h"
#include "sql/statement.h"

namespace dao::import {
namespace {

inline constexpr sql::Database::Tag kDatabaseTag{"DaoMigration"};

base::Time ChromiumTimeFromString(const std::string *value) {
  int64_t microseconds = 0;
  if (!value || !base::StringToInt64(*value, &microseconds) ||
      microseconds <= 0) {
    return base::Time();
  }
  return base::Time::FromDeltaSinceWindowsEpoch(
      base::Microseconds(microseconds));
}

void ReadBookmarkChildren(const base::ListValue *children, bool in_toolbar,
                          std::vector<std::u16string> path,
                          std::vector<BookmarkEntry> *entries) {
  if (!children) {
    return;
  }
  for (const base::Value &child : *children) {
    if (!child.is_dict()) {
      continue;
    }
    const base::DictValue &node = child.GetDict();
    const std::string *type = node.FindString("type");
    const std::string *name = node.FindString("name");
    if (!type || !name) {
      continue;
    }

    BookmarkEntry entry;
    entry.in_toolbar = in_toolbar;
    entry.is_folder = *type == "folder";
    entry.path = path;
    entry.title = base::UTF8ToUTF16(*name);
    entry.creation_time = ChromiumTimeFromString(node.FindString("date_added"));
    if (entry.is_folder) {
      entries->push_back(entry);
      path.push_back(entry.title);
      ReadBookmarkChildren(node.FindList("children"), in_toolbar,
                           std::move(path), entries);
      continue;
    }

    const std::string *url = node.FindString("url");
    if (!url) {
      continue;
    }
    entry.url = GURL(*url);
    if (entry.url.is_valid()) {
      entries->push_back(std::move(entry));
    }
  }
}

template <typename T> ReadBatch<T> ReadFailure(std::string error_code) {
  ReadBatch<T> batch;
  batch.error_code = std::move(error_code);
  return batch;
}

std::optional<uint64_t> ReadCount(sql::Database *database,
                                  base::cstring_view query) {
  sql::Statement statement(database->GetUniqueStatement(query));
  if (!statement.is_valid() || !statement.Step()) {
    return std::nullopt;
  }
  const int64_t count = statement.ColumnInt64(0);
  if (count < 0 || statement.Step() || !statement.Succeeded()) {
    return std::nullopt;
  }
  return static_cast<uint64_t>(count);
}

} // namespace

DaoChromiumProfileAdapter::DaoChromiumProfileAdapter(
    base::FilePath profile_path,
    std::unique_ptr<PasswordDecryptor> password_decryptor)
    : profile_path_(std::move(profile_path)),
      password_decryptor_(std::move(password_decryptor)) {}

DaoChromiumProfileAdapter::~DaoChromiumProfileAdapter() = default;

ReadBatch<BookmarkEntry> DaoChromiumProfileAdapter::ReadBookmarks() {
  std::string contents;
  if (!base::ReadFileToString(profile_path_.AppendASCII("Bookmarks"),
                              &contents)) {
    return ReadFailure<BookmarkEntry>("source_missing");
  }
  std::optional<base::DictValue> value =
      base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!value) {
    return ReadFailure<BookmarkEntry>("invalid_bookmarks");
  }
  const base::DictValue *roots = value->FindDict("roots");
  if (!roots) {
    return ReadFailure<BookmarkEntry>("invalid_bookmarks");
  }

  ReadBatch<BookmarkEntry> batch;
  batch.success = true;
  for (const auto [root_name, root_value] : *roots) {
    if (!root_value.is_dict()) {
      continue;
    }
    ReadBookmarkChildren(root_value.GetDict().FindList("children"),
                         root_name == "bookmark_bar", {}, &batch.records);
  }
  return batch;
}

ReadBatch<HistoryVisit> DaoChromiumProfileAdapter::ReadHistory() {
  sql::Database database(kDatabaseTag);
  if (!database.Open(profile_path_.AppendASCII("History"))) {
    return ReadFailure<HistoryVisit>("sqlite_open_failed");
  }
  sql::Statement statement(database.GetUniqueStatement(
      "SELECT urls.url, urls.title, visits.visit_time "
      "FROM visits JOIN urls ON urls.id = visits.url "
      "ORDER BY visits.visit_time, visits.id"));
  if (!statement.is_valid()) {
    return ReadFailure<HistoryVisit>("sqlite_query_failed");
  }

  ReadBatch<HistoryVisit> batch;
  batch.success = true;
  while (statement.Step()) {
    HistoryVisit visit;
    visit.url = GURL(statement.ColumnString(0));
    if (!visit.url.is_valid()) {
      continue;
    }
    visit.title = statement.ColumnString16(1);
    visit.visit_time = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(statement.ColumnInt64(2)));
    batch.records.push_back(std::move(visit));
  }
  if (!statement.Succeeded()) {
    return ReadFailure<HistoryVisit>("sqlite_query_failed");
  }
  return batch;
}

ReadBatch<PasswordEntry> DaoChromiumProfileAdapter::ReadPasswords() {
  if (!password_decryptor_) {
    return ReadFailure<PasswordEntry>("password_decryptor_unavailable");
  }
  sql::Database database(kDatabaseTag);
  if (!database.Open(profile_path_.AppendASCII("Login Data"))) {
    return ReadFailure<PasswordEntry>("sqlite_open_failed");
  }
  sql::Statement statement(database.GetUniqueStatement(
      "SELECT origin_url, username_value, password_value, signon_realm, "
      "date_created FROM logins WHERE blacklisted_by_user = 0 "
      "ORDER BY date_created, origin_url, username_value"));
  if (!statement.is_valid()) {
    return ReadFailure<PasswordEntry>("sqlite_query_failed");
  }

  ReadBatch<PasswordEntry> batch;
  batch.success = true;
  while (statement.Step()) {
    std::optional<std::u16string> password =
        password_decryptor_->Decrypt(statement.ColumnBlob(2));
    if (!password) {
      return ReadFailure<PasswordEntry>("password_decryption_denied");
    }
    PasswordEntry entry;
    entry.origin = GURL(statement.ColumnString(0));
    if (!entry.origin.is_valid()) {
      continue;
    }
    entry.username = statement.ColumnString16(1);
    entry.password = std::move(*password);
    entry.signon_realm = statement.ColumnString(3);
    entry.date_created = base::Time::FromDeltaSinceWindowsEpoch(
        base::Microseconds(statement.ColumnInt64(4)));
    batch.records.push_back(std::move(entry));
  }
  if (!statement.Succeeded()) {
    return ReadFailure<PasswordEntry>("sqlite_query_failed");
  }
  return batch;
}

ReadBatch<TabEntry> DaoChromiumProfileAdapter::ReadTabs() {
  if (!base::SequencedTaskRunner::HasCurrentDefault()) {
    return ReadFailure<TabEntry>("session_reader_unavailable");
  }
  const base::FilePath sessions_path = profile_path_.AppendASCII("Sessions");
  if (!base::DirectoryExists(sessions_path)) {
    ReadBatch<TabEntry> empty;
    empty.success = true;
    return empty;
  }

  auto backend = base::MakeRefCounted<sessions::CommandStorageBackend>(
      base::SequencedTaskRunner::GetCurrentDefault(), sessions_path,
      sessions::CommandStorageManager::SessionType::kSessionRestore,
      /*encryptor=*/nullptr);
  backend->MoveCurrentSessionToLastSession();
  sessions::CommandStorageBackend::ReadCommandsResult commands =
      backend->ReadLastSessionCommands();
  if (commands.error_reading) {
    return ReadFailure<TabEntry>("session_read_failed");
  }

  std::vector<std::unique_ptr<sessions::SessionWindow>> windows;
  SessionID active_window = SessionID::InvalidValue();
  std::string platform_session_id;
  std::set<SessionID> discarded_windows;
  sessions::RestoreSessionFromCommands(commands.commands, &windows,
                                       &active_window, &platform_session_id,
                                       &discarded_windows);

  ReadBatch<TabEntry> batch;
  batch.success = true;
  for (size_t window_index = 0; window_index < windows.size(); ++window_index) {
    const sessions::SessionWindow &window = *windows[window_index];
    for (size_t tab_index = 0; tab_index < window.tabs.size(); ++tab_index) {
      const sessions::SessionTab &tab = *window.tabs[tab_index];
      if (tab.navigations.empty()) {
        continue;
      }
      const sessions::SerializedNavigationEntry &navigation =
          tab.navigations[tab.normalized_navigation_index()];
      if (!navigation.virtual_url().is_valid() ||
          !navigation.virtual_url().SchemeIsHTTPOrHTTPS()) {
        continue;
      }
      TabEntry entry;
      entry.url = navigation.virtual_url();
      entry.title = navigation.title();
      entry.window_index = static_cast<int>(window_index);
      entry.tab_index = static_cast<int>(tab_index);
      entry.pinned = tab.pinned;
      batch.records.push_back(std::move(entry));
    }
  }
  return batch;
}

ReadBatch<ExtensionEntry> DaoChromiumProfileAdapter::ReadExtensions() {
  std::string contents;
  if (!base::ReadFileToString(profile_path_.AppendASCII("Preferences"),
                              &contents)) {
    return ReadFailure<ExtensionEntry>("source_missing");
  }
  std::optional<base::DictValue> value =
      base::JSONReader::ReadDict(contents, base::JSON_PARSE_RFC);
  if (!value) {
    return ReadFailure<ExtensionEntry>("invalid_preferences");
  }
  const base::DictValue *settings =
      value->FindDictByDottedPath("extensions.settings");
  if (!settings) {
    ReadBatch<ExtensionEntry> empty;
    empty.success = true;
    return empty;
  }

  ReadBatch<ExtensionEntry> batch;
  batch.success = true;
  for (const auto [extension_id, extension_value] : *settings) {
    if (!extension_value.is_dict()) {
      continue;
    }
    const base::DictValue &extension = extension_value.GetDict();
    if (!extension.FindBool("from_webstore").value_or(false)) {
      continue;
    }
    const base::DictValue *manifest = extension.FindDict("manifest");
    ExtensionEntry entry;
    entry.id = extension_id;
    if (manifest) {
      const std::string *name = manifest->FindString("name");
      if (name) {
        entry.name = *name;
      }
    }
    entry.enabled = extension.FindInt("state").value_or(0) == 1;
    batch.records.push_back(std::move(entry));
  }
  return batch;
}

std::optional<uint64_t>
DaoChromiumProfileAdapter::CountCandidates(DataCategory category) {
  auto count_records = [](const auto &batch) -> std::optional<uint64_t> {
    if (!batch.success) {
      return std::nullopt;
    }
    return static_cast<uint64_t>(batch.records.size());
  };
  switch (category) {
  case DataCategory::kBookmarks:
    return count_records(ReadBookmarks());
  case DataCategory::kHistory: {
    sql::Database database(sql::DatabaseOptions().set_read_only(true),
                           kDatabaseTag);
    if (!database.Open(profile_path_.AppendASCII("History"))) {
      return std::nullopt;
    }
    return ReadCount(
        &database,
        "SELECT COUNT(*) FROM visits JOIN urls ON urls.id = visits.url");
  }
  case DataCategory::kPasswords: {
    sql::Database database(sql::DatabaseOptions().set_read_only(true),
                           kDatabaseTag);
    if (!database.Open(profile_path_.AppendASCII("Login Data"))) {
      return std::nullopt;
    }
    return ReadCount(
        &database, "SELECT COUNT(*) FROM logins WHERE blacklisted_by_user = 0");
  }
  case DataCategory::kTabs:
    return count_records(ReadTabs());
  case DataCategory::kExtensions:
    return count_records(ReadExtensions());
  }
  return std::nullopt;
}

} // namespace dao::import
