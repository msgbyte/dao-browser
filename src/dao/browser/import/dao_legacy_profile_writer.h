// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_LEGACY_PROFILE_WRITER_H_
#define DAO_BROWSER_IMPORT_DAO_LEGACY_PROFILE_WRITER_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/importer/profile_writer.h"
#include "dao/browser/import/dao_migration_types.h"
#include "dao/browser/import/dao_source_adapter.h"

namespace dao::import {

class DaoChromiumMigrationTarget;
struct WriteResult;

// Adapts Chromium's sandboxed Safari/Firefox importers to the same
// destination merge policy used by the standalone migration service.
class DaoLegacyProfileWriter : public ProfileWriter {
 public:
  DaoLegacyProfileWriter(Profile* profile,
                         DaoChromiumMigrationTarget* target,
                         CategoryResult* result,
                         std::u16string bookmark_root);

  void AddPasswordForm(const password_manager::PasswordForm& form) override;
  void AddHistoryPage(const history::URLRows& page,
                      history::VisitSource visit_source) override;
  void AddBookmarks(
      const std::vector<user_data_importer::ImportedBookmarkEntry>& bookmarks,
      const std::u16string& top_level_folder_name) override;
  void FinishWhenIdle(base::OnceClosure callback);

 protected:
  ~DaoLegacyProfileWriter() override;

 private:
  void OnWriteFinished(WriteResult result);

  raw_ptr<DaoChromiumMigrationTarget> target_;
  raw_ptr<CategoryResult> result_;
  std::u16string bookmark_root_;
  std::vector<HistoryVisit> pending_history_;
  std::vector<PasswordEntry> pending_passwords_;
  size_t pending_writes_ = 0;
  base::OnceClosure finish_callback_;
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_LEGACY_PROFILE_WRITER_H_
