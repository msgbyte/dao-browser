// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_MIGRATION_WRITER_H_
#define DAO_BROWSER_IMPORT_DAO_MIGRATION_WRITER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "dao/browser/import/dao_source_adapter.h"

namespace dao::import {

struct WriteResult {
  uint64_t imported = 0;
  uint64_t skipped = 0;
  uint64_t conflicted = 0;
  uint64_t failed = 0;
};

enum class PasswordMatch {
  kNone,
  kSame,
  kConflict,
};

using ItemWriteCallback = base::OnceCallback<void(bool)>;
using WriteResultCallback = base::OnceCallback<void(WriteResult)>;

class MigrationTarget {
 public:
  virtual ~MigrationTarget() = default;

  virtual bool HasBookmark(const BookmarkEntry& entry,
                           const std::u16string& root_name) const = 0;
  virtual bool AddBookmark(const BookmarkEntry& entry,
                           const std::u16string& root_name) = 0;
  virtual bool HasHistoryVisit(const HistoryVisit& entry) const = 0;
  virtual void AddHistoryVisit(const HistoryVisit& entry,
                               ItemWriteCallback callback) = 0;
  virtual PasswordMatch MatchPassword(const PasswordEntry& entry) const = 0;
  virtual void AddPassword(const PasswordEntry& entry,
                           ItemWriteCallback callback) = 0;
  virtual bool IsTabOpen(const GURL& url) const = 0;
  virtual std::string EnsureImportedTabFolder(
      const std::u16string& folder_name) = 0;
  virtual bool AddDormantTab(const TabEntry& entry,
                             const std::string& folder_id) = 0;
  virtual bool FinishImportedTabFolder(const std::string& folder_id) = 0;
  virtual void AbortImportedTabFolder(const std::string& folder_id) = 0;
  virtual bool IsExtensionInstalled(const std::string& id) const = 0;
  virtual bool QueueExtensionInstall(const ExtensionEntry& entry) = 0;
};

class DaoMigrationWriter {
 public:
  explicit DaoMigrationWriter(MigrationTarget* target);
  DaoMigrationWriter(const DaoMigrationWriter&) = delete;
  DaoMigrationWriter& operator=(const DaoMigrationWriter&) = delete;
  ~DaoMigrationWriter();

  WriteResult WriteBookmarks(const std::vector<BookmarkEntry>& entries,
                             const std::u16string& root_name);
  void WriteHistory(const std::vector<HistoryVisit>& entries,
                    WriteResultCallback callback);
  void WritePasswords(const std::vector<PasswordEntry>& entries,
                      WriteResultCallback callback);
  WriteResult WriteTabs(const std::vector<TabEntry>& entries,
                        const std::u16string& folder_name);
  WriteResult WriteTabsBatch(const std::vector<TabEntry>& entries,
                             const std::u16string& folder_name,
                             std::string* folder_id);
  bool FinishTabs(const std::string& folder_id);
  WriteResult WriteExtensions(const std::vector<ExtensionEntry>& entries);

 private:
  const raw_ptr<MigrationTarget> target_;
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_MIGRATION_WRITER_H_
