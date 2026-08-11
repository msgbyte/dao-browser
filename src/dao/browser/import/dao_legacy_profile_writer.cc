// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_legacy_profile_writer.h"

#include <iterator>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted.h"
#include "components/history/core/browser/history_types.h"
#include "components/password_manager/core/browser/password_form.h"
#include "components/user_data_importer/common/imported_bookmark_entry.h"
#include "dao/browser/import/dao_chromium_migration_target.h"
#include "dao/browser/import/dao_migration_writer.h"

namespace dao::import {
namespace {

void MergeResult(CategoryResult* total, const WriteResult& result) {
  total->imported += result.imported;
  total->skipped += result.skipped;
  total->conflicted += result.conflicted;
  total->failed += result.failed;
}

}  // namespace

DaoLegacyProfileWriter::DaoLegacyProfileWriter(
    Profile* profile,
    DaoChromiumMigrationTarget* target,
    CategoryResult* result,
    std::u16string bookmark_root)
    : ProfileWriter(profile),
      target_(target),
      result_(result),
      bookmark_root_(std::move(bookmark_root)) {}

DaoLegacyProfileWriter::~DaoLegacyProfileWriter() = default;

void DaoLegacyProfileWriter::AddPasswordForm(
    const password_manager::PasswordForm& form) {
  PasswordEntry entry;
  entry.origin = form.url;
  entry.signon_realm = form.signon_realm;
  entry.username = form.username_value;
  entry.password = form.password_value;
  entry.date_created = form.date_created;
  pending_passwords_.push_back(std::move(entry));
}

void DaoLegacyProfileWriter::AddHistoryPage(const history::URLRows& page,
                                            history::VisitSource) {
  std::vector<HistoryVisit> visits;
  visits.reserve(page.size());
  for (const history::URLRow& row : page) {
    HistoryVisit visit;
    visit.url = row.url();
    visit.title = row.title();
    visit.visit_time = row.last_visit();
    visits.push_back(std::move(visit));
  }
  pending_history_.insert(pending_history_.end(),
                          std::make_move_iterator(visits.begin()),
                          std::make_move_iterator(visits.end()));
}

void DaoLegacyProfileWriter::AddBookmarks(
    const std::vector<user_data_importer::ImportedBookmarkEntry>& bookmarks,
    const std::u16string&) {
  std::vector<BookmarkEntry> entries;
  entries.reserve(bookmarks.size());
  for (const user_data_importer::ImportedBookmarkEntry& imported : bookmarks) {
    BookmarkEntry entry;
    entry.in_toolbar = imported.in_toolbar;
    entry.is_folder = imported.is_folder;
    entry.url = imported.url;
    entry.path = imported.path;
    entry.title = imported.title;
    entry.creation_time = imported.creation_time;
    entries.push_back(std::move(entry));
  }
  DaoMigrationWriter writer(target_);
  MergeResult(result_, writer.WriteBookmarks(entries, bookmark_root_));
}

void DaoLegacyProfileWriter::FinishWhenIdle(base::OnceClosure callback) {
  scoped_refptr<DaoLegacyProfileWriter> keep_alive(this);
  CHECK(finish_callback_.is_null());
  finish_callback_ = std::move(callback);
  const bool has_history = !pending_history_.empty();
  const bool has_passwords = !pending_passwords_.empty();
  pending_writes_ =
      static_cast<size_t>(has_history) + static_cast<size_t>(has_passwords);
  if (pending_writes_ == 0) {
    std::move(finish_callback_).Run();
    return;
  }
  DaoMigrationWriter writer(target_);
  if (has_history) {
    writer.WriteHistory(std::move(pending_history_),
                        base::BindOnce(&DaoLegacyProfileWriter::OnWriteFinished,
                                       base::RetainedRef(this)));
  }
  if (has_passwords) {
    writer.WritePasswords(
        std::move(pending_passwords_),
        base::BindOnce(&DaoLegacyProfileWriter::OnWriteFinished,
                       base::RetainedRef(this)));
  }
}

void DaoLegacyProfileWriter::OnWriteFinished(WriteResult result) {
  CHECK_GT(pending_writes_, 0u);
  MergeResult(result_, result);
  --pending_writes_;
  if (pending_writes_ == 0 && !finish_callback_.is_null()) {
    std::move(finish_callback_).Run();
  }
}

}  // namespace dao::import
