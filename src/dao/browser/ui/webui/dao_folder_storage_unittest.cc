// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_folder_storage.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {

TEST(DaoFolderStorageTest, LoadsLegacyFolderFileOnce) {
  constexpr char kLegacyJson[] =
      R"({"version":1,"items":[{"type":"tab","tabId":"tab-a","url":"https://a.example","title":"A"}]})";
  DaoFolderStorage storage;
  ASSERT_TRUE(storage.LoadFromJson(kLegacyJson));

  const DaoFolderWindowSnapshot* snapshot =
      storage.FindClaimableSnapshot({}, {}, {});
  ASSERT_TRUE(snapshot);
  EXPECT_TRUE(snapshot->legacy);
  EXPECT_EQ(kLegacyJson, snapshot->json);
  EXPECT_EQ(std::set<std::string>({"tab-a"}), snapshot->tab_ids);

  EXPECT_FALSE(storage.FindClaimableSnapshot({}, {}, {snapshot->id}));

  DaoFolderStorage restored;
  ASSERT_TRUE(restored.LoadFromJson(storage.ToJson()));
  ASSERT_TRUE(restored.FindClaimableSnapshot({}, {}, {}));
  EXPECT_TRUE(restored.FindClaimableSnapshot({}, {}, {})->legacy);
}

TEST(DaoFolderStorageTest, PreservesOtherWindowWhenUpdatingOneSnapshot) {
  DaoFolderStorage storage;
  storage.UpsertSnapshot("window-a", {"tab-a"}, R"({"items":["a"]})");
  storage.UpsertSnapshot("window-b", {"tab-b"}, R"({"items":["b"]})");
  storage.UpsertSnapshot("window-b", {"tab-b2"}, R"({"items":["updated"]})");

  DaoFolderStorage restored;
  ASSERT_TRUE(restored.LoadFromJson(storage.ToJson()));
  ASSERT_TRUE(restored.FindById("window-a"));
  EXPECT_EQ(R"({"items":["a"]})", restored.FindById("window-a")->json);
  ASSERT_TRUE(restored.FindById("window-b"));
  EXPECT_EQ(std::set<std::string>({"tab-b2"}),
            restored.FindById("window-b")->tab_ids);
}

TEST(DaoFolderStorageTest, DoesNotGiveNewWindowAnUnclaimedV2Snapshot) {
  DaoFolderStorage storage;
  storage.UpsertSnapshot("window-a", {"tab-a"}, R"({"items":[]})");

  EXPECT_FALSE(storage.FindClaimableSnapshot({}, {}, {}));
  EXPECT_EQ("window-a",
            storage.FindClaimableSnapshot({"window-a"}, {}, {})->id);
  EXPECT_FALSE(storage.FindClaimableSnapshot({"window-a"}, {}, {"window-a"}));
}

TEST(DaoFolderStorageTest, FallsBackToStableTabIdentityOverlap) {
  DaoFolderStorage storage;
  storage.UpsertSnapshot("window-a", {"tab-a", "tab-shared"},
                         R"({"items":[]})");
  storage.UpsertSnapshot("window-b", {"tab-b"}, R"({"items":[]})");

  EXPECT_FALSE(
      storage.FindClaimableSnapshot({"provisional-window"}, {"tab-b"}, {}));

  const DaoFolderWindowSnapshot* snapshot =
      storage.FindClaimableSnapshot({}, {"tab-b"}, {});
  ASSERT_TRUE(snapshot);
  EXPECT_EQ("window-b", snapshot->id);
}

TEST(DaoFolderStorageTest, AtomicWriteReplacesExistingFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("folders.json");
  ASSERT_TRUE(base::WriteFile(path, "old"));

  ASSERT_TRUE(WriteFolderFileAtomically(path, "new"));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ("new", contents);
}

TEST(DaoFolderStorageTest, SupersededWriteDoesNotReplaceNewerData) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("folders.json");
  const uint64_t old_generation = ReserveFolderFileWrite(path, "old");
  const uint64_t new_generation = ReserveFolderFileWrite(path, "new");

  ASSERT_TRUE(WriteReservedFolderFileAtomically(path, "old", old_generation));
  EXPECT_FALSE(base::PathExists(path));
  std::string pending_contents;
  ASSERT_TRUE(ReadFolderFileWithPendingWrite(path, &pending_contents));
  EXPECT_EQ("new", pending_contents);
  ASSERT_TRUE(WriteReservedFolderFileAtomically(path, "new", new_generation));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ("new", contents);
}

TEST(DaoFolderStorageTest, PendingWriteIsReadableBeforeDiskWrite) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("folders.json");

  ReserveFolderFileWrite(path, "pending");

  std::string contents;
  ASSERT_TRUE(ReadFolderFileWithPendingWrite(path, &contents));
  EXPECT_EQ("pending", contents);
  EXPECT_FALSE(base::PathExists(path));
}

TEST(DaoFolderStorageTest, SuccessfulWriteClearsPendingData) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("folders.json");
  const uint64_t generation = ReserveFolderFileWrite(path, "reserved");
  ASSERT_TRUE(WriteReservedFolderFileAtomically(path, "reserved", generation));
  ASSERT_TRUE(base::WriteFile(path, "on-disk"));

  std::string contents;
  ASSERT_TRUE(ReadFolderFileWithPendingWrite(path, &contents));
  EXPECT_EQ("on-disk", contents);
}

TEST(DaoFolderStorageTest, FailedSynchronousWritePreservesPendingData) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath not_a_directory =
      temp_dir.GetPath().AppendASCII("not-a-directory");
  ASSERT_TRUE(base::WriteFile(not_a_directory, "file"));
  const base::FilePath path = not_a_directory.AppendASCII("folders.json");
  ReserveFolderFileWrite(path, "before");

  EXPECT_FALSE(WriteFolderFileAtomically(path, "imported"));

  std::string contents;
  ASSERT_TRUE(ReadFolderFileWithPendingWrite(path, &contents));
  EXPECT_EQ("before", contents);
}

}  // namespace dao
