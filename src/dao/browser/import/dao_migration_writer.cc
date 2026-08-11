// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_writer.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"

namespace dao::import {
namespace {

template <typename Entry>
struct AsyncWriteState {
  std::vector<Entry> entries;
  size_t index = 0;
  WriteResult result;
  WriteResultCallback callback;
};

void WriteNextHistory(MigrationTarget* target,
                      std::unique_ptr<AsyncWriteState<HistoryVisit>> state) {
  while (state->index < state->entries.size()) {
    const HistoryVisit& entry = state->entries[state->index++];
    if (target->HasHistoryVisit(entry)) {
      ++state->result.skipped;
      continue;
    }
    target->AddHistoryVisit(
        entry, base::BindOnce(
                   [](MigrationTarget* target,
                      std::unique_ptr<AsyncWriteState<HistoryVisit>> state,
                      bool success) {
                     if (success) {
                       ++state->result.imported;
                     } else {
                       ++state->result.failed;
                     }
                     WriteNextHistory(target, std::move(state));
                   },
                   target, std::move(state)));
    return;
  }
  WriteResult result = state->result;
  WriteResultCallback callback = std::move(state->callback);
  std::move(callback).Run(result);
}

void WriteNextPassword(MigrationTarget* target,
                       std::unique_ptr<AsyncWriteState<PasswordEntry>> state) {
  while (state->index < state->entries.size()) {
    const PasswordEntry& entry = state->entries[state->index++];
    switch (target->MatchPassword(entry)) {
      case PasswordMatch::kSame:
        ++state->result.skipped;
        continue;
      case PasswordMatch::kConflict:
        ++state->result.conflicted;
        continue;
      case PasswordMatch::kNone:
        target->AddPassword(
            entry, base::BindOnce(
                       [](MigrationTarget* target,
                          std::unique_ptr<AsyncWriteState<PasswordEntry>> state,
                          bool success) {
                         if (success) {
                           ++state->result.imported;
                         } else {
                           ++state->result.failed;
                         }
                         WriteNextPassword(target, std::move(state));
                       },
                       target, std::move(state)));
        return;
    }
  }
  WriteResult result = state->result;
  WriteResultCallback callback = std::move(state->callback);
  std::move(callback).Run(result);
}

}  // namespace

DaoMigrationWriter::DaoMigrationWriter(MigrationTarget* target)
    : target_(target) {
  CHECK(target_);
}

DaoMigrationWriter::~DaoMigrationWriter() = default;

WriteResult DaoMigrationWriter::WriteBookmarks(
    const std::vector<BookmarkEntry>& entries,
    const std::u16string& root_name) {
  WriteResult result;
  for (const BookmarkEntry& entry : entries) {
    if (target_->HasBookmark(entry, root_name)) {
      ++result.skipped;
      continue;
    }
    if (target_->AddBookmark(entry, root_name)) {
      ++result.imported;
    } else {
      ++result.failed;
    }
  }
  return result;
}

void DaoMigrationWriter::WriteHistory(const std::vector<HistoryVisit>& entries,
                                      WriteResultCallback callback) {
  auto state = std::make_unique<AsyncWriteState<HistoryVisit>>();
  state->entries = entries;
  state->callback = std::move(callback);
  WriteNextHistory(target_, std::move(state));
}

void DaoMigrationWriter::WritePasswords(
    const std::vector<PasswordEntry>& entries,
    WriteResultCallback callback) {
  auto state = std::make_unique<AsyncWriteState<PasswordEntry>>();
  state->entries = entries;
  state->callback = std::move(callback);
  WriteNextPassword(target_, std::move(state));
}

WriteResult DaoMigrationWriter::WriteTabs(const std::vector<TabEntry>& entries,
                                          const std::u16string& folder_name) {
  std::string folder_id;
  WriteResult result = WriteTabsBatch(entries, folder_name, &folder_id);
  if (!folder_id.empty()) {
    if (result.imported == 0) {
      target_->AbortImportedTabFolder(folder_id);
    } else if (!FinishTabs(folder_id)) {
      result.failed += result.imported;
      result.imported = 0;
    }
  }
  return result;
}

WriteResult DaoMigrationWriter::WriteTabsBatch(
    const std::vector<TabEntry>& entries,
    const std::u16string& folder_name,
    std::string* folder_id) {
  WriteResult result;
  for (const TabEntry& entry : entries) {
    if (target_->IsTabOpen(entry.url)) {
      ++result.skipped;
      continue;
    }
    if (folder_id->empty()) {
      *folder_id = target_->EnsureImportedTabFolder(folder_name);
      if (folder_id->empty()) {
        ++result.failed;
        continue;
      }
    }
    if (target_->AddDormantTab(entry, *folder_id)) {
      ++result.imported;
    } else {
      ++result.failed;
    }
  }
  return result;
}

bool DaoMigrationWriter::FinishTabs(const std::string& folder_id) {
  if (target_->FinishImportedTabFolder(folder_id)) {
    return true;
  }
  target_->AbortImportedTabFolder(folder_id);
  return false;
}

WriteResult DaoMigrationWriter::WriteExtensions(
    const std::vector<ExtensionEntry>& entries) {
  WriteResult result;
  for (const ExtensionEntry& entry : entries) {
    if (target_->IsExtensionInstalled(entry.id)) {
      ++result.skipped;
      continue;
    }
    if (target_->QueueExtensionInstall(entry)) {
      ++result.imported;
    } else {
      ++result.failed;
    }
  }
  return result;
}

}  // namespace dao::import
