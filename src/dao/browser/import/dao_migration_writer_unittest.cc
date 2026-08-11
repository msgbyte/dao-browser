// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_writer.h"

#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "base/test/test_future.h"
#include "base/time/time.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao::import {
namespace {

class FakeMigrationTarget : public MigrationTarget {
 public:
  bool HasBookmark(const BookmarkEntry& entry,
                   const std::u16string& root_name) const override {
    return bookmarks.contains(entry.url.spec());
  }
  bool AddBookmark(const BookmarkEntry& entry,
                   const std::u16string& root_name) override {
    if (!accept_writes) {
      return false;
    }
    bookmarks.insert(entry.url.spec());
    bookmark_roots.push_back(root_name);
    return true;
  }
  bool HasHistoryVisit(const HistoryVisit& entry) const override {
    return history.contains({entry.url.spec(), entry.visit_time});
  }
  void AddHistoryVisit(const HistoryVisit& entry,
                       ItemWriteCallback callback) override {
    if (defer_history_writes) {
      pending_history_entry = entry;
      pending_history_callback = std::move(callback);
      return;
    }
    if (!accept_writes) {
      std::move(callback).Run(false);
      return;
    }
    history.insert({entry.url.spec(), entry.visit_time});
    std::move(callback).Run(true);
  }
  PasswordMatch MatchPassword(const PasswordEntry& entry) const override {
    return password_match;
  }
  void AddPassword(const PasswordEntry& entry,
                   ItemWriteCallback callback) override {
    if (defer_password_writes) {
      pending_password_callback = std::move(callback);
      return;
    }
    if (!accept_writes) {
      std::move(callback).Run(false);
      return;
    }
    ++password_adds;
    std::move(callback).Run(true);
  }
  bool IsTabOpen(const GURL& url) const override {
    return open_tabs.contains(url.spec());
  }
  std::string EnsureImportedTabFolder(
      const std::u16string& folder_name) override {
    imported_folder_name = folder_name;
    return "folder-1";
  }
  bool AddDormantTab(const TabEntry& entry,
                     const std::string& folder_id) override {
    if (!accept_writes) {
      return false;
    }
    dormant_tabs.push_back(entry.url.spec());
    return true;
  }
  bool FinishImportedTabFolder(const std::string& folder_id) override {
    if (!accept_writes || !accept_tab_folder_finish) {
      return false;
    }
    finished_folder_id = folder_id;
    return true;
  }
  void AbortImportedTabFolder(const std::string& folder_id) override {
    aborted_folder_id = folder_id;
    dormant_tabs.clear();
  }
  bool IsExtensionInstalled(const std::string& id) const override {
    return installed_extensions.contains(id);
  }
  bool QueueExtensionInstall(const ExtensionEntry& entry) override {
    installed_extensions.insert(entry.id);
    return true;
  }

  void CompleteHistoryWrite(bool success) {
    ASSERT_FALSE(pending_history_callback.is_null());
    if (success) {
      history.insert(
          {pending_history_entry.url.spec(), pending_history_entry.visit_time});
    }
    std::move(pending_history_callback).Run(success);
  }

  void CompletePasswordWrite(bool success) {
    ASSERT_FALSE(pending_password_callback.is_null());
    if (success) {
      ++password_adds;
    }
    std::move(pending_password_callback).Run(success);
  }

  std::set<std::string> bookmarks;
  std::vector<std::u16string> bookmark_roots;
  std::set<std::pair<std::string, base::Time>> history;
  HistoryVisit pending_history_entry;
  ItemWriteCallback pending_history_callback;
  PasswordMatch password_match = PasswordMatch::kNone;
  int password_adds = 0;
  ItemWriteCallback pending_password_callback;
  std::set<std::string> open_tabs;
  std::u16string imported_folder_name;
  std::vector<std::string> dormant_tabs;
  std::string finished_folder_id;
  std::string aborted_folder_id;
  std::set<std::string> installed_extensions;
  bool accept_writes = true;
  bool accept_tab_folder_finish = true;
  bool defer_history_writes = false;
  bool defer_password_writes = false;
};

TEST(DaoMigrationWriterTest, RepeatedBookmarkAndHistoryWritesAreIdempotent) {
  FakeMigrationTarget target;
  DaoMigrationWriter writer(&target);
  BookmarkEntry bookmark;
  bookmark.url = GURL("https://docs.example/");
  bookmark.title = u"Docs";
  HistoryVisit visit;
  visit.url = GURL("https://docs.example/");
  visit.visit_time = base::Time::FromSecondsSinceUnixEpoch(123);

  EXPECT_EQ(1u, writer.WriteBookmarks({bookmark}, u"Imported").imported);
  EXPECT_EQ(1u, writer.WriteBookmarks({bookmark}, u"Imported").skipped);
  base::test::TestFuture<WriteResult> first_history;
  writer.WriteHistory({visit}, first_history.GetCallback());
  EXPECT_EQ(1u, first_history.Get().imported);
  base::test::TestFuture<WriteResult> second_history;
  writer.WriteHistory({visit}, second_history.GetCallback());
  EXPECT_EQ(1u, second_history.Get().skipped);
}

TEST(DaoMigrationWriterTest, PreservesExistingPasswordOnConflict) {
  FakeMigrationTarget target;
  target.password_match = PasswordMatch::kConflict;
  DaoMigrationWriter writer(&target);
  PasswordEntry password;
  password.origin = GURL("https://example.com/");
  password.username = u"person@example.com";
  password.password = u"new secret";

  base::test::TestFuture<WriteResult> result;
  writer.WritePasswords({password}, result.GetCallback());

  EXPECT_EQ(1u, result.Get().conflicted);
  EXPECT_EQ(0, target.password_adds);
}

TEST(DaoMigrationWriterTest, SkipsOpenTabsAndPreservesImportedOrder) {
  FakeMigrationTarget target;
  target.open_tabs.insert("https://already.example/");
  DaoMigrationWriter writer(&target);
  TabEntry first;
  first.url = GURL("https://already.example/");
  TabEntry second;
  second.url = GURL("https://one.example/");
  TabEntry third;
  third.url = GURL("https://two.example/");

  WriteResult result =
      writer.WriteTabs({first, second, third}, u"Imported from Chrome");

  EXPECT_EQ(2u, result.imported);
  EXPECT_EQ(1u, result.skipped);
  EXPECT_EQ((std::vector<std::string>{"https://one.example/",
                                      "https://two.example/"}),
            target.dormant_tabs);
  EXPECT_EQ(u"Imported from Chrome", target.imported_folder_name);
  EXPECT_EQ("folder-1", target.finished_folder_id);
}

TEST(DaoMigrationWriterTest, WaitsForDestinationBeforeCountingHistoryWrite) {
  FakeMigrationTarget target;
  target.defer_history_writes = true;
  DaoMigrationWriter writer(&target);
  HistoryVisit visit;
  visit.url = GURL("https://history.example/");
  visit.visit_time = base::Time::FromSecondsSinceUnixEpoch(123);
  base::test::TestFuture<WriteResult> result;

  writer.WriteHistory({visit}, result.GetCallback());

  EXPECT_FALSE(result.IsReady());
  target.CompleteHistoryWrite(false);
  EXPECT_EQ(1u, result.Get().failed);
}

TEST(DaoMigrationWriterTest, WaitsForDestinationBeforeCountingPasswordWrite) {
  FakeMigrationTarget target;
  target.defer_password_writes = true;
  DaoMigrationWriter writer(&target);
  PasswordEntry password;
  password.origin = GURL("https://password.example/");
  password.username = u"person@example.com";
  password.password = u"secret";
  base::test::TestFuture<WriteResult> result;

  writer.WritePasswords({password}, result.GetCallback());

  EXPECT_FALSE(result.IsReady());
  target.CompletePasswordWrite(false);
  EXPECT_EQ(1u, result.Get().failed);
}

TEST(DaoMigrationWriterTest, KeepsTabFolderAcrossBatches) {
  FakeMigrationTarget target;
  DaoMigrationWriter writer(&target);
  TabEntry first;
  first.url = GURL("https://one.example/");
  TabEntry second;
  second.url = GURL("https://two.example/");
  std::string folder_id;

  EXPECT_EQ(
      1u,
      writer.WriteTabsBatch({first}, u"Imported tabs", &folder_id).imported);
  EXPECT_TRUE(target.finished_folder_id.empty());
  EXPECT_EQ(
      1u,
      writer.WriteTabsBatch({second}, u"Imported tabs", &folder_id).imported);
  EXPECT_TRUE(writer.FinishTabs(folder_id));

  EXPECT_EQ("folder-1", target.finished_folder_id);
  EXPECT_EQ((std::vector<std::string>{"https://one.example/",
                                      "https://two.example/"}),
            target.dormant_tabs);
}

TEST(DaoMigrationWriterTest, RollsBackTabsWhenFolderPersistenceFails) {
  FakeMigrationTarget target;
  target.accept_tab_folder_finish = false;
  DaoMigrationWriter writer(&target);
  TabEntry tab;
  tab.url = GURL("https://tab.example/");

  WriteResult result = writer.WriteTabs({tab}, u"Imported tabs");

  EXPECT_EQ(0u, result.imported);
  EXPECT_EQ(1u, result.failed);
  EXPECT_EQ("folder-1", target.aborted_folder_id);
  EXPECT_TRUE(target.dormant_tabs.empty());
}

TEST(DaoMigrationWriterTest, SkipsAlreadyInstalledExtensions) {
  FakeMigrationTarget target;
  target.installed_extensions.insert("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  DaoMigrationWriter writer(&target);
  ExtensionEntry installed;
  installed.id = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  ExtensionEntry new_extension;
  new_extension.id = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

  WriteResult result = writer.WriteExtensions({installed, new_extension});

  EXPECT_EQ(1u, result.imported);
  EXPECT_EQ(1u, result.skipped);
}

TEST(DaoMigrationWriterTest, ReportsRejectedDestinationWrites) {
  FakeMigrationTarget target;
  target.accept_writes = false;
  DaoMigrationWriter writer(&target);
  BookmarkEntry bookmark;
  bookmark.url = GURL("https://docs.example/");
  HistoryVisit visit;
  visit.url = GURL("https://history.example/");
  PasswordEntry password;
  password.origin = GURL("https://password.example/");
  TabEntry tab;
  tab.url = GURL("https://tab.example/");

  EXPECT_EQ(1u, writer.WriteBookmarks({bookmark}, u"Imported").failed);
  base::test::TestFuture<WriteResult> history_result;
  writer.WriteHistory({visit}, history_result.GetCallback());
  EXPECT_EQ(1u, history_result.Get().failed);
  base::test::TestFuture<WriteResult> password_result;
  writer.WritePasswords({password}, password_result.GetCallback());
  EXPECT_EQ(1u, password_result.Get().failed);
  EXPECT_EQ(1u, writer.WriteTabs({tab}, u"Imported tabs").failed);
}

}  // namespace
}  // namespace dao::import
