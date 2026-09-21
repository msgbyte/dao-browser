// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_folder_storage.h"

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

}  // namespace dao
