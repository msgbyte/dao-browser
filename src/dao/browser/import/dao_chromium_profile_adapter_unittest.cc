// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_chromium_profile_adapter.h"

#include <string>
#include <string_view>
#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_view_util.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao::import {
namespace {

class FakePasswordDecryptor : public PasswordDecryptor {
public:
  std::optional<std::u16string>
  Decrypt(base::span<const uint8_t> encrypted_value) override {
    if (std::string(base::as_string_view(encrypted_value)) != "cipher") {
      return std::nullopt;
    }
    return u"secret";
  }
};

TEST(DaoChromiumProfileAdapterTest, ReadsBookmarkHierarchyAndToolbarOrigin) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      profile_dir.GetPath().AppendASCII("Bookmarks"),
      R"({"roots":{"bookmark_bar":{"type":"folder","name":"Bookmarks bar","children":[{"type":"folder","name":"Work","children":[{"type":"url","name":"Docs","url":"https://docs.example/","date_added":"13300000000000000"}]}]},"other":{"type":"folder","name":"Other bookmarks","children":[{"type":"url","name":"News","url":"https://news.example/"}]}}})"));

  DaoChromiumProfileAdapter adapter(profile_dir.GetPath());
  ReadBatch<BookmarkEntry> batch = adapter.ReadBookmarks();

  ASSERT_TRUE(batch.success);
  ASSERT_EQ(3u, batch.records.size());
  EXPECT_TRUE(batch.records[0].is_folder);
  EXPECT_EQ(u"Work", batch.records[0].title);
  EXPECT_TRUE(batch.records[1].in_toolbar);
  EXPECT_EQ(std::vector<std::u16string>({u"Work"}), batch.records[1].path);
  EXPECT_EQ("https://docs.example/", batch.records[1].url.spec());
  EXPECT_FALSE(batch.records[2].in_toolbar);
  EXPECT_EQ(3u, adapter.CountCandidates(DataCategory::kBookmarks));
}

TEST(DaoChromiumProfileAdapterTest, ReadsHistoryVisitsInStableOrder) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  sql::Database database;
  ASSERT_TRUE(database.Open(profile_dir.GetPath().AppendASCII("History")));
  ASSERT_TRUE(database.Execute(
      "CREATE TABLE urls (id INTEGER PRIMARY KEY, url LONGVARCHAR, title "
      "LONGVARCHAR)"));
  ASSERT_TRUE(database.Execute(
      "CREATE TABLE visits (id INTEGER PRIMARY KEY, url INTEGER, visit_time "
      "INTEGER)"));
  ASSERT_TRUE(database.Execute(
      "INSERT INTO urls VALUES (1, 'https://example.com/', 'Example')"));
  ASSERT_TRUE(
      database.Execute("INSERT INTO visits VALUES (2, 1, 13300000000000002), "
                       "(1, 1, 13300000000000001)"));
  database.Close();

  DaoChromiumProfileAdapter adapter(profile_dir.GetPath());
  ReadBatch<HistoryVisit> batch = adapter.ReadHistory();

  ASSERT_TRUE(batch.success);
  ASSERT_EQ(2u, batch.records.size());
  EXPECT_LT(batch.records[0].visit_time, batch.records[1].visit_time);
  EXPECT_EQ(u"Example", batch.records[0].title);
  EXPECT_EQ(2u, adapter.CountCandidates(DataCategory::kHistory));
}

TEST(DaoChromiumProfileAdapterTest, ReadsExtensionEnabledState) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      profile_dir.GetPath().AppendASCII("Preferences"),
      R"({"extensions":{"settings":{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa":{"state":1,"from_webstore":true,"manifest":{"name":"Enabled"}},"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb":{"state":0,"from_webstore":true,"manifest":{"name":"Disabled"}},"cccccccccccccccccccccccccccccccc":{"state":1,"manifest":{"name":"Component"}}}}})"));

  DaoChromiumProfileAdapter adapter(profile_dir.GetPath());
  ReadBatch<ExtensionEntry> batch = adapter.ReadExtensions();

  ASSERT_TRUE(batch.success);
  ASSERT_EQ(2u, batch.records.size());
  EXPECT_EQ("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", batch.records[0].id);
  EXPECT_TRUE(batch.records[0].enabled);
  EXPECT_FALSE(batch.records[1].enabled);
  EXPECT_EQ(2u, adapter.CountCandidates(DataCategory::kExtensions));
}

TEST(DaoChromiumProfileAdapterTest, ReadsPasswordMetadataWithDecryptor) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  sql::Database database;
  ASSERT_TRUE(database.Open(profile_dir.GetPath().AppendASCII("Login Data")));
  ASSERT_TRUE(database.Execute(
      "CREATE TABLE logins (origin_url VARCHAR, username_value VARCHAR, "
      "password_value BLOB, signon_realm VARCHAR, date_created INTEGER, "
      "blacklisted_by_user INTEGER)"));
  sql::Statement insert(database.GetUniqueStatement(
      "INSERT INTO logins VALUES (?, ?, ?, ?, ?, 0)"));
  insert.BindString(0, "https://example.com/login");
  insert.BindString16(1, u"person@example.com");
  insert.BindBlob(2, base::as_byte_span(std::string_view("cipher")));
  insert.BindString(3, "https://example.com/");
  insert.BindInt64(4, 13300000000000000);
  ASSERT_TRUE(insert.Run());
  database.Close();

  DaoChromiumProfileAdapter adapter(profile_dir.GetPath(),
                                    std::make_unique<FakePasswordDecryptor>());
  ReadBatch<PasswordEntry> batch = adapter.ReadPasswords();

  ASSERT_TRUE(batch.success);
  ASSERT_EQ(1u, batch.records.size());
  EXPECT_EQ(u"person@example.com", batch.records[0].username);
  EXPECT_EQ(u"secret", batch.records[0].password);
  EXPECT_EQ("https://example.com/", batch.records[0].signon_realm);
}

TEST(DaoChromiumProfileAdapterTest,
     CountsPasswordCandidatesWithoutDecryptingThem) {
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());
  sql::Database database;
  ASSERT_TRUE(database.Open(profile_dir.GetPath().AppendASCII("Login Data")));
  ASSERT_TRUE(
      database.Execute("CREATE TABLE logins (blacklisted_by_user INTEGER)"));
  ASSERT_TRUE(database.Execute("INSERT INTO logins VALUES (0), (0), (1)"));
  database.Close();

  DaoChromiumProfileAdapter adapter(profile_dir.GetPath());

  EXPECT_EQ(2u, adapter.CountCandidates(DataCategory::kPasswords));
}

} // namespace
} // namespace dao::import
