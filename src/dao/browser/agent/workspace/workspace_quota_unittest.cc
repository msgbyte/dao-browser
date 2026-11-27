// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_quota.h"

#include <string>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_WIN)
#include "base/test/file_path_reparse_point_win.h"
#endif

namespace dao {
namespace {

class WorkspaceQuotaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(temp_dir_.CreateUniqueTempDir());
    root_ = temp_dir_.GetPath();
  }
  base::ScopedTempDir temp_dir_;
  base::FilePath root_;
};

TEST_F(WorkspaceQuotaTest, EmptyRootHasZeroUsage) {
  WorkspaceQuota q(root_);
  auto usage = q.GetUsage();
  EXPECT_EQ(0u, usage.total_bytes);
  EXPECT_EQ(0u, usage.entry_count);
}

TEST_F(WorkspaceQuotaTest, CountsFiles) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("a.md"), "hello"));
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("b.md"), "worldworld"));
  WorkspaceQuota q(root_);
  auto usage = q.GetUsage();
  EXPECT_EQ(15u, usage.total_bytes);
  EXPECT_EQ(2u, usage.entry_count);
}

TEST_F(WorkspaceQuotaTest, SkipsHiddenAuditAndIndex) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII(".audit.log"), "xx"));
#if BUILDFLAG(IS_WIN)
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("workspace.MD"), "yy"));
#else
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("WORKSPACE.md"), "yy"));
#endif
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("real.md"), "z"));
  WorkspaceQuota q(root_);
  auto usage = q.GetUsage();
  EXPECT_EQ(1u, usage.total_bytes);
  EXPECT_EQ(1u, usage.entry_count);
}

TEST_F(WorkspaceQuotaTest, CacheInvalidatedOnNotify) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("a.md"), "hello"));
  WorkspaceQuota q(root_);
  EXPECT_EQ(5u, q.GetUsage().total_bytes);

  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("a.md"), "helloextra"));
  // Without invalidation the cached value is stale.
  EXPECT_EQ(5u, q.GetUsage().total_bytes);
  q.InvalidateCache();
  EXPECT_EQ(10u, q.GetUsage().total_bytes);
}

TEST_F(WorkspaceQuotaTest, CountsHiddenAndNestedFilesOnceAcrossDirectoryLinks) {
  const auto visible = root_.AppendASCII("files");
  const auto hidden = visible.AppendASCII(".private");
  ASSERT_TRUE(base::CreateDirectory(hidden));
  ASSERT_TRUE(base::WriteFile(hidden.AppendASCII("hidden.md"), "hidden"));
  ASSERT_TRUE(base::WriteFile(visible.AppendASCII("WORKSPACE.md"), "nested"));
  const auto staging = root_.AppendASCII(".workspace_tmp");
  ASSERT_TRUE(base::CreateDirectory(staging));
  ASSERT_TRUE(base::WriteFile(staging.AppendASCII("partial.md"), "partial"));
  const auto notes = visible.AppendASCII("notes.md");
  ASSERT_TRUE(base::WriteFile(notes, "notes"));
  const auto alias = root_.AppendASCII("alias");
#if BUILDFLAG(IS_WIN)
  ASSERT_TRUE(base::CreateDirectory(alias));
  auto junction = base::test::FilePathReparsePoint::Create(alias, visible);
  ASSERT_TRUE(junction.has_value());
#else
  ASSERT_TRUE(base::CreateSymbolicLink(visible, alias));
  ASSERT_TRUE(base::CreateSymbolicLink(notes, root_.AppendASCII("alias.md")));
#endif
  const auto usage = WorkspaceQuota(root_).GetUsage();
  EXPECT_EQ(17u, usage.total_bytes);
  EXPECT_EQ(3u, usage.entry_count);
}

TEST_F(WorkspaceQuotaTest, AcceptsWriteWithinQuota) {
  WorkspaceQuota q(root_);
  EXPECT_TRUE(q.CanAcceptWrite(base::FilePath::FromASCII("new.md"),
                               /*new_bytes=*/1024, std::nullopt));
}

TEST_F(WorkspaceQuotaTest, RetriesUsageWhenRootBecomesAvailable) {
  const auto root = root_.AppendASCII("workspace");
  WorkspaceQuota quota(root);
  EXPECT_FALSE(quota.CanAcceptWrite(base::FilePath::FromASCII("new.md"), 1,
                                    std::nullopt));
  ASSERT_TRUE(base::CreateDirectory(root));
  EXPECT_TRUE(quota.CanAcceptWrite(base::FilePath::FromASCII("new.md"), 1,
                                   std::nullopt));
}

TEST_F(WorkspaceQuotaTest, RejectsPerFileOversize) {
  WorkspaceQuota q(root_);
  EXPECT_FALSE(q.CanAcceptWrite(base::FilePath::FromASCII("big.md"),
                                WorkspaceQuota::kPerFileMaxBytes + 1,
                                std::nullopt));
}

TEST_F(WorkspaceQuotaTest, RejectsTotalOversize) {
  WorkspaceQuota q(root_);
  base::File filler(root_.AppendASCII("filler.md"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes));
  filler.Close();
  EXPECT_FALSE(q.CanAcceptWrite(base::FilePath::FromASCII("more.md"),
                                /*new_bytes=*/1, std::nullopt));
}

TEST_F(WorkspaceQuotaTest, ReplacingExistingFreesItsBytes) {
  WorkspaceQuota q(root_);
  base::File filler(root_.AppendASCII("filler.md"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes));
  filler.Close();
  EXPECT_TRUE(q.CanAcceptWrite(base::FilePath::FromASCII("filler.md"),
                               /*new_bytes=*/1,
                               WorkspaceQuota::kTotalMaxBytes));
}

TEST_F(WorkspaceQuotaTest, RejectsExceedingEntryCount) {
  WorkspaceQuota q(root_);
  for (int i = 0; i < 500; ++i) {
    ASSERT_TRUE(base::WriteFile(
        root_.AppendASCII(base::StringPrintf("f%d.md", i)), "x"));
  }
  q.InvalidateCache();
  EXPECT_FALSE(q.CanAcceptWrite(base::FilePath::FromASCII("overflow.md"),
                                /*new_bytes=*/1, std::nullopt));
}

TEST_F(WorkspaceQuotaTest, ReplacesExistingEmptyFileAtEntryLimit) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("empty.md"), ""));
  for (size_t i = 1; i < WorkspaceQuota::kMaxEntries; ++i) {
    ASSERT_TRUE(base::WriteFile(root_.AppendASCII(std::to_string(i)), ""));
  }
  WorkspaceQuota q(root_);
  EXPECT_TRUE(q.CanAcceptWrite(base::FilePath::FromASCII("empty.md"), 1, 0));
}

TEST_F(WorkspaceQuotaTest, DeletionsCanRecoverAnOverLimitWorkspaceAfterScan) {
  const auto root = root_.AppendASCII("workspace");
  WorkspaceQuota quota(root);
  const WorkspaceQuota::FileChange deletion{
      base::FilePath::FromASCII("empty.md"), 0, std::nullopt};
  EXPECT_FALSE(quota.CanApplyChanges(base::span_from_ref(deletion)));
  ASSERT_TRUE(base::CreateDirectory(root));
  base::File filler(root.AppendASCII("filler.txt"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes + 1));
  filler.Close();
  ASSERT_TRUE(base::WriteFile(root.Append(deletion.path), ""));
  EXPECT_TRUE(quota.CanApplyChanges(base::span_from_ref(deletion)));
}

}  // namespace
}  // namespace dao
