// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_index.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

class WorkspaceIndexTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }
  base::ScopedTempDir dir_;
};

TEST_F(WorkspaceIndexTest, RewritesEmptyWorkspace) {
  WorkspaceIndex idx(dir_.GetPath());
  idx.Rewrite();
  std::string content;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII("WORKSPACE.md"), &content));
  EXPECT_NE(std::string::npos, content.find("# Workspace Index"));
  EXPECT_NE(std::string::npos, content.find("0 files"));
}

TEST_F(WorkspaceIndexTest, ListsFilesWithSizes) {
  ASSERT_TRUE(base::WriteFile(dir_.GetPath().AppendASCII("notes.md"),
                              "hello world"));
  ASSERT_TRUE(base::WriteFile(dir_.GetPath().AppendASCII("data.csv"),
                              "a,b,c\n1,2,3\n"));
  WorkspaceIndex(dir_.GetPath()).Rewrite();

  std::string content;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII("WORKSPACE.md"), &content));
  EXPECT_NE(std::string::npos, content.find("`notes.md`"));
  EXPECT_NE(std::string::npos, content.find("`data.csv`"));
  EXPECT_NE(std::string::npos, content.find("2 files"));
}

TEST_F(WorkspaceIndexTest, SkipsAuditAndStagingAndIndexItself) {
  ASSERT_TRUE(base::WriteFile(
      dir_.GetPath().AppendASCII(".audit.log"), "x"));
  ASSERT_TRUE(base::CreateDirectory(
      dir_.GetPath().AppendASCII(".workspace_tmp")));
  ASSERT_TRUE(base::WriteFile(
      dir_.GetPath().AppendASCII(".workspace_tmp/leftover"), "x"));
  ASSERT_TRUE(base::WriteFile(dir_.GetPath().AppendASCII("real.md"), "x"));
  WorkspaceIndex(dir_.GetPath()).Rewrite();
  std::string content;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII("WORKSPACE.md"), &content));
  EXPECT_EQ(std::string::npos, content.find(".audit.log"));
  EXPECT_EQ(std::string::npos, content.find("leftover"));
  EXPECT_NE(std::string::npos, content.find("`real.md`"));
}

TEST_F(WorkspaceIndexTest, HeaderWarnsNotToEdit) {
  WorkspaceIndex(dir_.GetPath()).Rewrite();
  std::string content;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII("WORKSPACE.md"), &content));
  EXPECT_NE(std::string::npos, content.find("Auto-generated"));
  EXPECT_NE(std::string::npos, content.find("DO NOT EDIT"));
}

TEST_F(WorkspaceIndexTest, IndexesOnlyVisibleRegularFiles) {
  const auto& root = dir_.GetPath();
  const auto visible = root.AppendASCII("files");
  const auto hidden = visible.AppendASCII(".private");
  ASSERT_TRUE(base::CreateDirectory(hidden));
  ASSERT_TRUE(base::WriteFile(hidden.AppendASCII("hidden.md"), "hidden\n"));
  const auto notes = visible.AppendASCII("notes.md");
  ASSERT_TRUE(base::WriteFile(notes, std::string(20000, 'x') + "\nend\n"));
  ASSERT_TRUE(base::WriteFile(visible.AppendASCII("WORKSPACE.md"), "nested"));
#if BUILDFLAG(IS_POSIX)
  ASSERT_TRUE(base::CreateSymbolicLink(notes, root.AppendASCII("alias.md")));
  ASSERT_TRUE(base::CreateSymbolicLink(visible, root.AppendASCII("alias")));
#endif
  WorkspaceIndex(root).Rewrite();
  std::string index;
  ASSERT_TRUE(base::ReadFileToString(root.AppendASCII("WORKSPACE.md"), &index));
  EXPECT_NE(std::string::npos, index.find("notes.md` (19.5 KB, 2 lines)"));
  EXPECT_NE(std::string::npos, index.find("WORKSPACE.md` (6 B, 0 lines)"));
  EXPECT_EQ(std::string::npos, index.find("alias"));
  EXPECT_EQ(std::string::npos, index.find("hidden.md"));
}

#if BUILDFLAG(IS_POSIX)
TEST_F(WorkspaceIndexTest, DoesNotOverwriteTargetOfIndexLink) {
  const auto& root = dir_.GetPath();
  const auto notes = root.AppendASCII("notes.md");
  ASSERT_TRUE(base::WriteFile(notes, "notes"));
  ASSERT_TRUE(base::CreateSymbolicLink(notes, root.AppendASCII("WORKSPACE.md")));
  WorkspaceIndex(root).Rewrite();
  std::string content;
  ASSERT_TRUE(base::ReadFileToString(notes, &content));
  EXPECT_EQ("notes", content);
}
#endif

}  // namespace
}  // namespace dao
