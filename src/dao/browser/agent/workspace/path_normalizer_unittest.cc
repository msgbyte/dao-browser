// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/path_normalizer.h"

#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "build/build_config.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "dao/browser/agent/workspace/workspace_file.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>

#include "base/test/file_path_reparse_point_win.h"
#include "base/win/scoped_handle.h"
#endif

namespace dao {
namespace {

class PathNormalizerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_ = temp_dir_.GetPath();
  }

  base::ScopedTempDir temp_dir_;
  base::FilePath root_;
};

TEST_F(PathNormalizerTest, AcceptsSimpleRelativePath) {
  auto result = ResolveWorkspacePath(root_, "notes.md");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("notes.md")), result.value());
}

TEST_F(PathNormalizerTest, AcceptsUtf8RelativePath) {
  auto result = ResolveWorkspacePath(root_, "caf\xc3\xa9.md");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("caf\u00e9.md")), result.value());
}

TEST_F(PathNormalizerTest, AcceptsNestedRelativePath) {
  auto result = ResolveWorkspacePath(root_, "research/competitors.md");
  ASSERT_TRUE(result.has_value());
}

TEST_F(PathNormalizerTest, RejectsAbsolutePosix) {
  auto result = ResolveWorkspacePath(root_, "/etc/passwd");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsWindowsAbsolute) {
  auto result = ResolveWorkspacePath(root_, "C:\\Windows\\System32\\config");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsDotDotSegment) {
  auto result = ResolveWorkspacePath(root_, "../escape.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsDotDotInMiddle) {
  auto result = ResolveWorkspacePath(root_, "a/../../b.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsEmpty) {
  auto result = ResolveWorkspacePath(root_, "");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsHiddenComponent) {
  auto result = ResolveWorkspacePath(root_, ".git/hooks/pre-commit");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsHiddenSubcomponent) {
  auto result = ResolveWorkspacePath(root_, "good/.hidden/file.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, AllowsAuditLog) {
  auto result = ResolveWorkspacePath(root_, ".audit.log");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL(".audit.log")), result.value());
}

#if BUILDFLAG(IS_POSIX)
TEST_F(PathNormalizerTest, RejectsAliasIntoPrivateDirectory) {
  const auto hidden = root_.AppendASCII(".private");
  ASSERT_TRUE(base::CreateDirectory(hidden));
  ASSERT_TRUE(base::CreateSymbolicLink(hidden, root_.AppendASCII("alias")));
  EXPECT_FALSE(ResolveWorkspacePath(root_, "alias/note.md").has_value());
}

TEST_F(PathNormalizerTest, FileOperationsRejectReplacedParent) {
  const auto folder = root_.AppendASCII("folder");
  const auto archive = root_.AppendASCII("archive");
  ASSERT_TRUE(base::CreateDirectory(folder));
  ASSERT_TRUE(base::WriteFile(folder.AppendASCII("note.md"), "original"));
  auto relative = ResolveWorkspacePath(root_, "folder/note.md");
  ASSERT_TRUE(relative.has_value());

  ASSERT_TRUE(base::Move(folder, archive));
  ASSERT_TRUE(base::CreateSymbolicLink(archive, folder));
  EXPECT_FALSE(OpenWorkspaceFile(root_, *relative).IsValid());
  EXPECT_FALSE(WriteWorkspaceFile(root_, *relative, "updated"));
  EXPECT_FALSE(DeleteWorkspaceFile(root_, *relative));
  std::string body;
  ASSERT_TRUE(base::ReadFileToString(archive.AppendASCII("note.md"), &body));
  EXPECT_EQ("original", body);
}

TEST_F(PathNormalizerTest, RejectsSymlinkEscape) {
  base::ScopedTempDir outside_dir;
  ASSERT_TRUE(outside_dir.CreateUniqueTempDir());
  base::FilePath outside_file =
      outside_dir.GetPath().Append(FILE_PATH_LITERAL("secret.txt"));
  ASSERT_TRUE(base::WriteFile(outside_file, "secret"));

  base::FilePath link_inside = root_.Append(FILE_PATH_LITERAL("link"));
  ASSERT_TRUE(base::CreateSymbolicLink(outside_file, link_inside));

  auto result = ResolveWorkspacePath(root_, "link");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(PathNormalizerTest, RejectsDanglingDirectoryLink) {
  ASSERT_TRUE(base::CreateSymbolicLink(root_.AppendASCII("missing/target"),
                                       root_.AppendASCII("link")));
  auto result = ResolveWorkspacePath(root_, "link/notes.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}
#endif

TEST_F(PathNormalizerTest, RejectsDirectoryLinkOutsideWorkspace) {
  base::ScopedTempDir outside_dir;
  ASSERT_TRUE(outside_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(
      outside_dir.GetPath().Append(FILE_PATH_LITERAL("secret.txt")), "secret"));
  const auto link = root_.Append(FILE_PATH_LITERAL("link"));
#if BUILDFLAG(IS_WIN)
  ASSERT_TRUE(base::CreateDirectory(link));
  auto junction =
      base::test::FilePathReparsePoint::Create(link, outside_dir.GetPath());
  ASSERT_TRUE(junction.has_value());
#else
  ASSERT_TRUE(base::CreateSymbolicLink(outside_dir.GetPath(), link));
#endif

  for (const char* path : {"link/secret.txt", "link/new/file.txt"}) {
    SCOPED_TRACE(path);
    auto result = ResolveWorkspacePath(root_, path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
  }
}

TEST_F(PathNormalizerTest, AcceptsDirectoryLinkWithinWorkspace) {
  const auto target = root_.Append(FILE_PATH_LITERAL("target"));
  const auto link = root_.Append(FILE_PATH_LITERAL("link"));
  ASSERT_TRUE(base::CreateDirectory(target));
#if BUILDFLAG(IS_WIN)
  ASSERT_TRUE(base::CreateDirectory(link));
  auto junction = base::test::FilePathReparsePoint::Create(link, target);
  ASSERT_TRUE(junction.has_value());
#else
  ASSERT_TRUE(base::CreateSymbolicLink(target, link));
#endif

  auto result = ResolveWorkspacePath(root_, "link/new/file.txt");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(base::FilePath(FILE_PATH_LITERAL("target"))
                .Append(FILE_PATH_LITERAL("new"))
                .Append(FILE_PATH_LITERAL("file.txt")),
            result.value());
}

#if BUILDFLAG(IS_WIN)
TEST_F(PathNormalizerTest, RejectsTrailingDotsAndSpacesInComponents) {
  for (const char* path :
       {"Notes./file.md", "Notes /file.md", "notes.md.", "notes.md "}) {
    SCOPED_TRACE(path);
    auto result = ResolveWorkspacePath(root_, path);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
  }
  EXPECT_TRUE(ResolveWorkspacePath(root_, "My Notes.v2/file.md").has_value());
}

TEST_F(PathNormalizerTest, DirectoryPinsDenyDeleteAccessToEveryAncestor) {
  const auto parent = root_.AppendASCII("first");
  const auto child = parent.AppendASCII("second");
  ASSERT_TRUE(base::CreateDirectory(child));
  auto pins =
      OpenWorkspaceDirectory(root_, base::FilePath::FromASCII("first/second"));
  ASSERT_FALSE(pins.empty());
  auto open_for_delete = [](const base::FilePath& path) {
    return base::win::ScopedHandle(::CreateFileW(
        path.value().c_str(), DELETE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  };
  for (const auto& dir : {root_, parent, child}) {
    SCOPED_TRACE(dir.AsUTF8Unsafe());
    auto handle = open_for_delete(dir);
    const DWORD error = ::GetLastError();
    EXPECT_FALSE(handle.is_valid());
    EXPECT_EQ(static_cast<DWORD>(ERROR_SHARING_VIOLATION), error);
  }
  pins.clear();
  EXPECT_TRUE(open_for_delete(parent).is_valid());
}

TEST_F(PathNormalizerTest, RejectsDriveRelativeRoot) {
  auto result = ResolveWorkspacePath(root_, "\\Windows\\file.txt");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}
#endif

TEST_F(PathNormalizerTest, AcceptsTrailingSlashNormalized) {
  auto result = ResolveWorkspacePath(root_, "dir/file.md");
  ASSERT_TRUE(result.has_value());
}

}  // namespace
}  // namespace dao
