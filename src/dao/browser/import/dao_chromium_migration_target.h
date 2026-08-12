// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_CHROMIUM_MIGRATION_TARGET_H_
#define DAO_BROWSER_IMPORT_DAO_CHROMIUM_MIGRATION_TARGET_H_

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/values.h"
#include "components/password_manager/core/browser/password_store/password_store_consumer.h"
#include "dao/browser/import/dao_migration_writer.h"

class Profile;

namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}  // namespace bookmarks

namespace extensions {
class WebstoreInstallWithPrompt;
}  // namespace extensions

namespace history {
struct QueryURLAndVisitsResult;
}  // namespace history

namespace dao::import {

// Writes normalized migration records through Chromium's profile services.
// This class is UI-thread bound and intentionally never touches destination
// profile databases directly.
class DaoChromiumMigrationTarget
    : public MigrationTarget,
      public password_manager::PasswordStoreConsumer {
 public:
  explicit DaoChromiumMigrationTarget(Profile* profile);
  DaoChromiumMigrationTarget(const DaoChromiumMigrationTarget&) = delete;
  DaoChromiumMigrationTarget& operator=(const DaoChromiumMigrationTarget&) =
      delete;
  ~DaoChromiumMigrationTarget() override;

  bool HasBookmark(const BookmarkEntry& entry,
                   const std::u16string& root_name) const override;
  bool AddBookmark(const BookmarkEntry& entry,
                   const std::u16string& root_name) override;
  bool HasHistoryVisit(const HistoryVisit& entry) const override;
  void AddHistoryVisit(const HistoryVisit& entry,
                       ItemWriteCallback callback) override;
  PasswordMatch MatchPassword(const PasswordEntry& entry) const override;
  void AddPassword(const PasswordEntry& entry,
                   ItemWriteCallback callback) override;
  bool IsTabOpen(const GURL& url) const override;
  std::string EnsureImportedTabFolder(
      const std::u16string& folder_name) override;
  bool AddDormantTab(const TabEntry& entry,
                     const std::string& folder_id) override;
  bool FinishImportedTabFolder(const std::string& folder_id) override;
  void AbortImportedTabFolder(const std::string& folder_id) override;
  bool IsExtensionInstalled(const std::string& id) const override;
  bool QueueExtensionInstall(const ExtensionEntry& entry) override;
  void FinishExtensionInstalls(
      base::OnceCallback<void(uint64_t installed, uint64_t failed)> callback);
  void CancelExtensionInstalls();

  // password_manager::PasswordStoreConsumer:
  void OnGetPasswordStoreResultsOrErrorFrom(
      password_manager::PasswordStoreInterface* store,
      password_manager::LoginsResultOrError results_or_error) override;

  // Populates destination credential identities before password writes. The
  // service calls this with forms returned by PasswordStoreInterface.
  void SetExistingPasswords(std::map<std::pair<std::string, std::u16string>,
                                     std::u16string> passwords);

 private:
  std::string BookmarkKey(const BookmarkEntry& entry) const;
  const bookmarks::BookmarkNode* FindOrCreateBookmarkParent(
      const BookmarkEntry& entry,
      const std::u16string& root_name);
  bool LoadFolderData();
  void StartNextExtensionInstall();
  void MaybeFinishExtensionInstalls();
  void OnHistoryWriteVerified(HistoryVisit entry,
                              ItemWriteCallback callback,
                              history::QueryURLAndVisitsResult result);
  void OnPasswordAdded(
      scoped_refptr<password_manager::PasswordStoreInterface> store);
  void OnExtensionInstallFinished(bool enabled,
                                  std::string extension_id,
                                  bool success);

  raw_ptr<Profile> profile_;
  raw_ptr<bookmarks::BookmarkModel> bookmark_model_ = nullptr;
  std::set<std::string> added_bookmarks_;
  std::set<std::pair<std::string, base::Time>> added_history_;
  base::CancelableTaskTracker history_task_tracker_;
  std::map<std::pair<std::string, std::u16string>, std::u16string> passwords_;
  std::optional<PasswordEntry> pending_password_entry_;
  ItemWriteCallback pending_password_callback_;
  std::map<std::string, raw_ptr<const bookmarks::BookmarkNode>>
      bookmark_folders_;
  base::DictValue folder_data_;
  raw_ptr<base::ListValue> folder_items_ = nullptr;
  raw_ptr<base::DictValue> pending_folder_ = nullptr;
  std::vector<std::string> pending_folder_tab_ids_;
  std::deque<ExtensionEntry> extension_queue_;
  scoped_refptr<extensions::WebstoreInstallWithPrompt> extension_installer_;
  base::OnceCallback<void(uint64_t installed, uint64_t failed)>
      extension_completion_callback_;
  uint64_t installed_extension_count_ = 0;
  uint64_t failed_extension_count_ = 0;
  bool cancel_extension_installs_ = false;
  base::WeakPtrFactory<DaoChromiumMigrationTarget> weak_ptr_factory_{this};
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_CHROMIUM_MIGRATION_TARGET_H_
