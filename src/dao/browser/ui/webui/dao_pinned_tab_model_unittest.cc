// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_pinned_tab_model.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "dao/browser/ui/webui/dao_pinned_tab_storage.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {

TEST(DaoPinnedTabModelTest, LoadsV1AsUnboundReconciling) {
  DaoPinnedTabModel model;
  ASSERT_TRUE(model.LoadFromJson(
      R"({"version":1,"items":[{"id":"pin","title":"Docs","url":"https://docs.example"}]})"));
  ASSERT_EQ(1u, model.items().size());
  EXPECT_TRUE(model.items()[0].backing_tab_id.empty());
  EXPECT_EQ(DaoPinnedTabState::kReconciling, model.items()[0].state);
}

TEST(DaoPinnedTabModelTest, V2RoundTripReconcilesPersistedDormantState) {
  DaoPinnedTabModel model;
  DaoPinnedTabItem& item = model.AddOrUpdate(
      "Docs", "https://docs.example", std::string(), "tab-uuid");
  ASSERT_TRUE(model.SetState(item.id, DaoPinnedTabState::kDormant));

  DaoPinnedTabModel restored;
  ASSERT_TRUE(restored.LoadFromJson(model.ToJson()));
  ASSERT_EQ(1u, restored.items().size());
  EXPECT_EQ("tab-uuid", restored.items()[0].backing_tab_id);
  EXPECT_EQ(DaoPinnedTabState::kReconciling, restored.items()[0].state);
}

TEST(DaoPinnedTabModelTest, SameUrlTabsCreateDistinctPinnedItems) {
  DaoPinnedTabModel model;
  model.AddOrUpdate("A", "https://same.example", std::string(), "tab-a");
  model.AddOrUpdate("B", "https://same.example", std::string(), "tab-b");
  EXPECT_EQ(2u, model.items().size());
}

TEST(DaoPinnedTabModelTest, InvalidJsonPreservesCurrentItems) {
  DaoPinnedTabModel model;
  model.AddOrUpdate("Docs", "https://docs.example", std::string(),
                    "tab-uuid");
  EXPECT_FALSE(model.LoadFromJson("not-json"));
  ASSERT_EQ(1u, model.items().size());
  EXPECT_EQ("tab-uuid", model.items()[0].backing_tab_id);
}

TEST(DaoPinnedTabModelTest, PartiallyInvalidJsonPreservesCurrentItems) {
  DaoPinnedTabModel model;
  model.AddOrUpdate("Existing", "https://existing.example", std::string(),
                    "existing-tab");

  EXPECT_FALSE(model.LoadFromJson(
      R"({"version":1,"items":[{"id":"valid","title":"Valid","url":"https://valid.example"},{"id":"invalid","title":"Missing URL"}]})"));
  ASSERT_EQ(1u, model.items().size());
  EXPECT_EQ("existing-tab", model.items()[0].backing_tab_id);

  EXPECT_FALSE(model.LoadFromJson(R"({"version":1})"));
  ASSERT_EQ(1u, model.items().size());
  EXPECT_EQ("existing-tab", model.items()[0].backing_tab_id);
}

TEST(DaoPinnedTabStorageTest, AtomicWriteReplacesExistingFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("pins.json");
  ASSERT_TRUE(base::WriteFile(path, "old"));

  ASSERT_TRUE(WritePinnedTabsFileAtomically(path, "new"));

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ("new", contents);
}

TEST(DaoPinnedTabStorageTest, FailedWritePreservesExistingFile) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath path = temp_dir.GetPath().AppendASCII("pins.json");
  ASSERT_TRUE(base::WriteFile(path, "old"));
  ASSERT_TRUE(base::SetPosixFilePermissions(
      temp_dir.GetPath(), base::FILE_PERMISSION_READ_BY_USER |
                              base::FILE_PERMISSION_EXECUTE_BY_USER));

  EXPECT_FALSE(WritePinnedTabsFileAtomically(path, "new"));

  ASSERT_TRUE(base::SetPosixFilePermissions(temp_dir.GetPath(),
                                            base::FILE_PERMISSION_USER_MASK));
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(path, &contents));
  EXPECT_EQ("old", contents);
}

}  // namespace dao
