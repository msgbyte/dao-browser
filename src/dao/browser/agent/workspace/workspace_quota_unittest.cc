// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_quota.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

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
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("WORKSPACE.md"), "yy"));
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

TEST_F(WorkspaceQuotaTest, AcceptsWriteWithinQuota) {
  WorkspaceQuota q(root_);
  EXPECT_TRUE(q.CanAcceptWrite("new.md", /*new_bytes=*/1024,
                               /*replacing_existing_bytes=*/0));
}

TEST_F(WorkspaceQuotaTest, RejectsPerFileOversize) {
  WorkspaceQuota q(root_);
  EXPECT_FALSE(q.CanAcceptWrite("big.md", /*new_bytes=*/6 * 1024 * 1024,
                                /*replacing_existing_bytes=*/0));
}

TEST_F(WorkspaceQuotaTest, RejectsTotalOversize) {
  WorkspaceQuota q(root_);
  // Simulate near-full workspace by writing a 99MB sentinel.
  std::string filler(99 * 1024 * 1024, 'x');
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("filler.md"), filler));
  q.InvalidateCache();
  EXPECT_FALSE(q.CanAcceptWrite("more.md", /*new_bytes=*/2 * 1024 * 1024,
                                /*replacing_existing_bytes=*/0));
}

TEST_F(WorkspaceQuotaTest, ReplacingExistingFreesItsBytes) {
  WorkspaceQuota q(root_);
  std::string filler(99 * 1024 * 1024, 'x');
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("filler.md"), filler));
  q.InvalidateCache();
  // Replacing the 99MB file with 1MB should fit.
  EXPECT_TRUE(q.CanAcceptWrite(
      "filler.md", /*new_bytes=*/1 * 1024 * 1024,
      /*replacing_existing_bytes=*/99 * 1024 * 1024));
}

TEST_F(WorkspaceQuotaTest, RejectsExceedingEntryCount) {
  WorkspaceQuota q(root_);
  for (int i = 0; i < 500; ++i) {
    ASSERT_TRUE(base::WriteFile(
        root_.AppendASCII(base::StringPrintf("f%d.md", i)), "x"));
  }
  q.InvalidateCache();
  EXPECT_FALSE(q.CanAcceptWrite("overflow.md", /*new_bytes=*/1,
                                /*replacing_existing_bytes=*/0));
}

}  // namespace
}  // namespace dao
