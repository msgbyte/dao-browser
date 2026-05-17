// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/path_normalizer.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

class PathNormalizerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_ = temp_dir_.GetPath();
    // NormalizePath canonicalizes symlinks (e.g. macOS /var -> /private/var),
    // so equality checks against root_ must use the canonical form.
    resolved_root_ = base::MakeAbsoluteFilePath(root_);
    if (resolved_root_.empty()) {
      resolved_root_ = root_;
    }
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath root_;
  base::FilePath resolved_root_;
};

TEST_F(PathNormalizerTest, AcceptsSimpleRelativePath) {
  auto result = NormalizePath(root_, "notes.md");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(resolved_root_.Append(FILE_PATH_LITERAL("notes.md")),
            result.value());
}

TEST_F(PathNormalizerTest, AcceptsNestedRelativePath) {
  auto result = NormalizePath(root_, "research/competitors.md");
  ASSERT_TRUE(result.has_value());
}

TEST_F(PathNormalizerTest, RejectsAbsolutePosix) {
  auto result = NormalizePath(root_, "/etc/passwd");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsWindowsAbsolute) {
  auto result = NormalizePath(root_, "C:\\Windows\\System32\\config");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsDotDotSegment) {
  auto result = NormalizePath(root_, "../escape.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsDotDotInMiddle) {
  auto result = NormalizePath(root_, "a/../../b.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsEmpty) {
  auto result = NormalizePath(root_, "");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsHiddenComponent) {
  auto result = NormalizePath(root_, ".git/hooks/pre-commit");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsHiddenSubcomponent) {
  auto result = NormalizePath(root_, "good/.hidden/file.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, AllowsAuditLog) {
  auto result = NormalizePath(root_, ".audit.log");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(resolved_root_.Append(FILE_PATH_LITERAL(".audit.log")),
            result.value());
}

TEST_F(PathNormalizerTest, RejectsSymlinkEscape) {
  base::ScopedTempDir outside_dir;
  ASSERT_TRUE(outside_dir.CreateUniqueTempDir());
  base::FilePath outside_file = outside_dir.GetPath().Append(
      FILE_PATH_LITERAL("secret.txt"));
  ASSERT_TRUE(base::WriteFile(outside_file, "secret"));

  base::FilePath link_inside = root_.Append(FILE_PATH_LITERAL("link"));
  ASSERT_TRUE(base::CreateSymbolicLink(outside_file, link_inside));

  auto result = NormalizePath(root_, "link");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, AcceptsTrailingSlashNormalized) {
  auto result = NormalizePath(root_, "dir/file.md");
  ASSERT_TRUE(result.has_value());
}

}  // namespace
}  // namespace dao
