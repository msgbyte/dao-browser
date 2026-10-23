// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_folder_storage.h"

#include <utility>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/values.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

constexpr char kFolders[] =
    R"({"version":1,"items":[{"type":"folder","id":"a","name":"A","collapsed":false,"children":[{"type":"tab","tabId":"tab-a","url":"https://example.com","title":"A"}]}]})";
constexpr char kOtherFolders[] =
    R"({"version":1,"items":[{"type":"folder","id":"b","name":"B","collapsed":false,"children":[{"type":"tab","tabId":"tab-b","url":"https://example.com","title":"B"}]}]})";

std::string WindowSnapshots() {
  base::ListValue windows;
  for (const auto* json : {kFolders, kOtherFolders, kFolders}) {
    base::DictValue window;
    window.Set("id", std::to_string(windows.size()));
    window.Set("data", json);
    windows.Append(std::move(window));
  }
  base::DictValue data;
  data.Set("version", 2);
  data.Set("windows", std::move(windows));
  return *base::WriteJson(data);
}

}  // namespace

TEST(DaoFolderStorageTest, AddsTabsToANewTopFolderAndMovesExistingOnes) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const base::FilePath path = temp.GetPath().AppendASCII("folders.json");
  ASSERT_TRUE(base::WriteFile(path, kFolders));
  base::ListValue tabs;
  tabs.Append(base::DictValue()
                  .Set("tabId", "tab-a")
                  .Set("url", "https://example.com")
                  .Set("title", "A"));
  tabs.Append(base::DictValue()
                  .Set("tabId", "tab-new")
                  .Set("url", "https://example.org")
                  .Set("title", "New"));

  ASSERT_TRUE(AddTabsToFolderFile(path, "results", "AI results", tabs));
  // A second call reuses the folder instead of adding another.
  ASSERT_TRUE(AddTabsToFolderFile(path, "results", "AI results",
                                  base::ListValue().Append(
                                      base::DictValue()
                                          .Set("tabId", "tab-new")
                                          .Set("url", "https://example.org")
                                          .Set("title", "New"))));

  auto saved = ReadFolderFile(path);
  ASSERT_TRUE(saved);
  auto data = base::JSONReader::ReadDict(*saved, base::JSON_PARSE_RFC);
  ASSERT_TRUE(data);
  const base::ListValue& items = *data->FindList("items");
  ASSERT_EQ(2u, items.size());
  const base::DictValue& results = items[0].GetDict();
  EXPECT_EQ("results", *results.FindString("id"));
  const base::ListValue& children = *results.FindList("children");
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ("tab-a", *children[0].GetDict().FindString("tabId"));
  EXPECT_EQ("tab-new", *children[1].GetDict().FindString("tabId"));
  EXPECT_TRUE(items[1].GetDict().FindList("children")->empty());
}

TEST(DaoFolderStorageTest, StaleWindowSavePreservesOtherFolders) {
  auto merged = MergeFolderData("", kOtherFolders, kFolders);
  ASSERT_TRUE(merged);
  auto data = base::JSONReader::ReadDict(*merged, base::JSON_PARSE_RFC);
  ASSERT_TRUE(data);
  ASSERT_EQ(2u, data->FindList("items")->size());
  EXPECT_EQ("b", *(*data->FindList("items"))[0].GetDict().FindString("id"));
  EXPECT_EQ("a", *(*data->FindList("items"))[1].GetDict().FindString("id"));
}

TEST(DaoFolderStorageTest, ConcurrentFieldsMergeWithoutResurrectingDeletion) {
  auto renamed = base::JSONReader::ReadDict(kFolders, base::JSON_PARSE_RFC);
  auto collapsed = renamed->Clone();
  (*renamed->FindList("items"))[0].GetDict().Set("name", "Renamed");
  (*collapsed.FindList("items"))[0].GetDict().Set("collapsed", true);
  auto merged = MergeFolderData(kFolders, *base::WriteJson(*renamed),
                                *base::WriteJson(collapsed));
  ASSERT_TRUE(merged);
  auto data = base::JSONReader::ReadDict(*merged, base::JSON_PARSE_RFC);
  const auto& folder = (*data->FindList("items"))[0].GetDict();
  EXPECT_EQ("Renamed", *folder.FindString("name"));
  EXPECT_EQ(true, folder.FindBool("collapsed"));
  auto deleted = MergeFolderData(kFolders, *merged, "");
  ASSERT_TRUE(deleted);
  EXPECT_TRUE(base::JSONReader::ReadDict(*deleted, base::JSON_PARSE_RFC)
                  ->FindList("items")->empty());
}

TEST(DaoFolderStorageTest, MigratesAllWindowSnapshotsWithoutWritingOnRead) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto path = temp.GetPath().AppendASCII("folders.json");
  const std::string snapshots = WindowSnapshots();
  ASSERT_TRUE(base::WriteFile(path, snapshots));
  auto loaded = ReadFolderFile(path);
  ASSERT_TRUE(loaded);
  auto data = base::JSONReader::ReadDict(*loaded, base::JSON_PARSE_RFC);
  ASSERT_TRUE(data);
  EXPECT_EQ(1, data->FindInt("version"));
  const auto* items = data->FindList("items");
  ASSERT_EQ(2u, items->size());
  EXPECT_EQ(1u, (*items)[0].GetDict().FindList("children")->size());
  EXPECT_EQ("tab-a", *(*(*items)[0].GetDict().FindList("children"))[0]
                         .GetDict().FindString("tabId"));
  EXPECT_EQ("tab-b", *(*(*items)[1].GetDict().FindList("children"))[0]
                         .GetDict().FindString("tabId"));
  std::string disk;
  ASSERT_TRUE(base::ReadFileToString(path, &disk));
  EXPECT_EQ(snapshots, disk);
  ASSERT_TRUE(UpdateFolderFile(path, *loaded, *loaded));
  ASSERT_TRUE(base::ReadFileToString(path, &disk));
  EXPECT_EQ(*loaded, disk);
}

TEST(DaoFolderStorageTest, InvalidExistingFilesAreNeverOverwritten) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto path = temp.GetPath().AppendASCII("folders.json");
  for (const std::string invalid : {
           "", "broken", R"({"version":1})",
           R"({"version":2,"windows":[{"id":"a","data":"broken"}]})"}) {
    ASSERT_TRUE(base::WriteFile(path, invalid));
    EXPECT_FALSE(ReadFolderFile(path));
    EXPECT_FALSE(UpdateFolderFile(path, "", kFolders));
    EXPECT_FALSE(UpdateFolderFileIfUnchanged(path, "", kFolders));
    std::string disk;
    ASSERT_TRUE(base::ReadFileToString(path, &disk));
    EXPECT_EQ(invalid, disk);
  }
}

TEST(DaoFolderStorageTest, FailedWritePreservesExistingData) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto path = temp.GetPath().AppendASCII("folders.json");
  ASSERT_TRUE(UpdateFolderFile(path, "", kFolders));
  auto before = ReadFolderFile(path);
  ASSERT_TRUE(before);
  EXPECT_FALSE(UpdateFolderFile(path, *before, "invalid"));
  EXPECT_EQ(before, ReadFolderFile(path));
  // A directory at the file path must fail closed as well.
  const auto directory = temp.GetPath().AppendASCII("directory");
  ASSERT_TRUE(base::CreateDirectory(directory));
  EXPECT_FALSE(UpdateFolderFile(directory, "", kFolders));
  EXPECT_TRUE(base::DirectoryExists(directory));
}

TEST(DaoFolderStorageTest, SettingsEditsNamesAndReleasesAllChildren) {
  auto added = EditFolderData(kFolders, FolderEdit::kAdd, "b", "  Work  ");
  ASSERT_TRUE(added);
  auto renamed = EditFolderData(*added, FolderEdit::kRename, "a", "Research");
  ASSERT_TRUE(renamed);
  auto removed = EditFolderData(*renamed, FolderEdit::kRemove, "a", "");
  ASSERT_TRUE(removed);
  auto data = base::JSONReader::ReadDict(*removed, base::JSON_PARSE_RFC);
  const auto& items = *data->FindList("items");
  ASSERT_EQ(2u, items.size());
  EXPECT_EQ("tab-a", *items[0].GetDict().FindString("tabId"));
  EXPECT_EQ("b", *items[1].GetDict().FindString("id"));
  EXPECT_EQ("Work", *items[1].GetDict().FindString("name"));
  EXPECT_TRUE(items[1].GetDict().FindList("children")->empty());
}

TEST(DaoFolderStorageTest, SettingsRejectInvalidAndDuplicateNames) {
  const std::string invalid_names[] = {
      "", "  ", "\t\n", "\xE3\x80\x80", "a", " A ", std::string(101, 'x')};
  for (const auto& invalid : invalid_names) {
    EXPECT_FALSE(EditFolderData(kFolders, FolderEdit::kAdd, "b", invalid));
  }
  EXPECT_FALSE(EditFolderData(kFolders, FolderEdit::kAdd, "a", "Work"));
  EXPECT_FALSE(EditFolderData(kFolders, FolderEdit::kRename, "missing", "Work"));
  EXPECT_FALSE(EditFolderData(kFolders, FolderEdit::kRemove, "missing", ""));
  EXPECT_TRUE(EditFolderData(kFolders, FolderEdit::kRename, "a", "A"));
  EXPECT_TRUE(
      EditFolderData(kFolders, FolderEdit::kAdd, "b", std::string(100, 'x')));
  EXPECT_FALSE(EditFolderData("broken", FolderEdit::kAdd, "b", "Work"));
}

TEST(DaoFolderStorageTest, SettingsCountsUnicodeCodepoints) {
  std::string name;
  for (int i = 0; i < 100; ++i) {
    name += "\xF0\x9F\x8C\x8D";
  }
  EXPECT_TRUE(EditFolderData(kFolders, FolderEdit::kAdd, "b", name));
  name += "x";
  EXPECT_FALSE(EditFolderData(kFolders, FolderEdit::kAdd, "b", name));
  EXPECT_FALSE(EditFolderData(kFolders, FolderEdit::kAdd, "b", "\xff"));
}

TEST(DaoFolderStorageTest, SettingsEditsLatestFileAndPreservesInvalidData) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto path = temp.GetPath().AppendASCII("folders.json");
  ASSERT_TRUE(UpdateFolderFile(path, "", kFolders));
  ASSERT_TRUE(UpdateFolderFile(path, "", kOtherFolders));
  ASSERT_TRUE(EditFolderFile(path, FolderEdit::kRename, "a", "Research"));
  EXPECT_FALSE(EditFolderFile(path, FolderEdit::kAdd, "c", "research"));
  auto removed = EditFolderFile(path, FolderEdit::kRemove, "a", "");
  ASSERT_TRUE(removed);
  auto data = base::JSONReader::ReadDict(*removed, base::JSON_PARSE_RFC);
  const auto& items = *data->FindList("items");
  ASSERT_EQ(2u, items.size());
  EXPECT_EQ("b", *items[0].GetDict().FindString("id"));
  EXPECT_EQ("tab-b", *(*items[0].GetDict().FindList("children"))[0]
                            .GetDict().FindString("tabId"));
  EXPECT_EQ("tab-a", *items[1].GetDict().FindString("tabId"));
  ASSERT_TRUE(base::WriteFile(path, "broken"));
  EXPECT_FALSE(EditFolderFile(path, FolderEdit::kRemove, "b", ""));
  std::string disk;
  ASSERT_TRUE(base::ReadFileToString(path, &disk));
  EXPECT_EQ("broken", disk);
}

TEST(DaoFolderStorageTest, ClassificationAcceptsUnchangedLegacySnapshots) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto path = temp.GetPath().AppendASCII("folders.json");
  ASSERT_TRUE(base::WriteFile(path, WindowSnapshots()));
  auto loaded = ReadFolderFile(path);
  ASSERT_TRUE(loaded);
  auto classified = base::JSONReader::ReadDict(*loaded, base::JSON_PARSE_RFC);
  ASSERT_TRUE(classified);
  auto tab = base::JSONReader::ReadDict(
      R"({"type":"tab","tabId":"tab-new","url":"https://new.example/","title":"New"})",
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(tab);
  (*classified->FindList("items"))[0].GetDict().FindList("children")->Append(
      std::move(*tab));
  const auto json = base::WriteJson(*classified);
  ASSERT_TRUE(json);

  ASSERT_TRUE(UpdateFolderFileIfUnchanged(path, *loaded, *json));
  const auto saved_json = ReadFolderFile(path);
  ASSERT_TRUE(saved_json);
  EXPECT_EQ(json, saved_json);
  const auto saved = base::JSONReader::ReadDict(
      *saved_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(saved);
  const auto& items = *saved->FindList("items");
  ASSERT_EQ(2u, items.size());
  const auto& children = *items[0].GetDict().FindList("children");
  ASSERT_EQ(2u, children.size());
  EXPECT_EQ("tab-a", *children[0].GetDict().FindString("tabId"));
  EXPECT_EQ("tab-new", *children[1].GetDict().FindString("tabId"));
  ASSERT_EQ(1u, items[1].GetDict().FindList("children")->size());
  EXPECT_EQ("tab-b", *(*items[1].GetDict().FindList("children"))[0]
                         .GetDict().FindString("tabId"));
}

TEST(DaoFolderStorageTest, ClassificationRejectsChangedFileWithoutWriting) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto path = temp.GetPath().AppendASCII("folders.json");
  ASSERT_TRUE(UpdateFolderFile(path, "", kFolders));
  auto base = ReadFolderFile(path);
  ASSERT_TRUE(base);
  auto renamed = EditFolderData(*base, FolderEdit::kRename, "a", "Research");
  ASSERT_TRUE(renamed);
  ASSERT_TRUE(UpdateFolderFileIfUnchanged(path, kFolders, *renamed));
  EXPECT_FALSE(UpdateFolderFileIfUnchanged(path, *base, ""));
  EXPECT_EQ(renamed, ReadFolderFile(path));
  EXPECT_FALSE(UpdateFolderFileIfUnchanged(path, *renamed, "broken"));
  EXPECT_EQ(renamed, ReadFolderFile(path));
}

}  // namespace dao
