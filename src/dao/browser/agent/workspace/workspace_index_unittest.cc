// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_index.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
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

}  // namespace
}  // namespace dao
