// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/path_normalizer.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "build/build_config.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_WIN)
#include "base/test/file_path_reparse_point_win.h"
#endif

namespace dao {
namespace {

class PathNormalizerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_ = temp_dir_.GetPath();
    // NormalizePath canonicalizes symlinks (e.g. macOS /var -> /private/var),
    // so equality checks against root_ must use the canonical form.
#if BUILDFLAG(IS_WIN)
    ASSERT_TRUE(base::NormalizeFilePath(root_, &resolved_root_));
#else
    resolved_root_ = base::MakeAbsoluteFilePath(root_);
    if (resolved_root_.empty()) {
      resolved_root_ = root_;
    }
#endif
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

TEST_F(PathNormalizerTest, AcceptsUtf8RelativePath) {
  auto result = NormalizePath(root_, "caf\xc3\xa9.md");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(resolved_root_.Append(FILE_PATH_LITERAL("caf\u00e9.md")),
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

#if BUILDFLAG(IS_POSIX)
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
#endif

#if BUILDFLAG(IS_WIN)
TEST_F(PathNormalizerTest, RejectsJunctionEscape) {
  base::ScopedTempDir outside_dir;
  ASSERT_TRUE(outside_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      outside_dir.GetPath().Append(FILE_PATH_LITERAL("secret.txt")), "secret"));
  const auto link = root_.Append(FILE_PATH_LITERAL("link"));
  ASSERT_TRUE(base::CreateDirectory(link));
  auto junction =
      base::test::FilePathReparsePoint::Create(link, outside_dir.GetPath());
  ASSERT_TRUE(junction.has_value());

  for (const char* path : {"link/secret.txt", "link/new/file.txt"}) {
    SCOPED_TRACE(path);
    auto result = NormalizePath(root_, path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
  }
}

TEST_F(PathNormalizerTest, AcceptsJunctionWithinWorkspace) {
  const auto target = root_.Append(FILE_PATH_LITERAL("target"));
  const auto link = root_.Append(FILE_PATH_LITERAL("link"));
  ASSERT_TRUE(base::CreateDirectory(target));
  ASSERT_TRUE(base::CreateDirectory(link));
  auto junction = base::test::FilePathReparsePoint::Create(link, target);
  ASSERT_TRUE(junction.has_value());

  auto result = NormalizePath(root_, "link/new/file.txt");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(resolved_root_.Append(FILE_PATH_LITERAL("target"))
                .Append(FILE_PATH_LITERAL("new"))
                .Append(FILE_PATH_LITERAL("file.txt")),
            result.value());
}

TEST_F(PathNormalizerTest, RejectsDriveRelativeRoot) {
  auto result = NormalizePath(root_, "\\Windows\\file.txt");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}
#endif

TEST_F(PathNormalizerTest, AcceptsTrailingSlashNormalized) {
  auto result = NormalizePath(root_, "dir/file.md");
  ASSERT_TRUE(result.has_value());
}

}  // namespace
}  // namespace dao
