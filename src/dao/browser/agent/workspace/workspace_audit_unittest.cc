// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/workspace_audit.h"

#include <algorithm>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

AuditEntry MakeEntry(std::string ts,
                     std::string op,
                     std::string path,
                     std::string detail) {
  AuditEntry e;
  e.ts = std::move(ts);
  e.op = std::move(op);
  e.path = std::move(path);
  e.detail = std::move(detail);
  return e;
}

class WorkspaceAuditTest : public ::testing::Test {
 protected:
  void SetUp() override { ASSERT_TRUE(dir_.CreateUniqueTempDir()); }
  base::ScopedTempDir dir_;
};

TEST_F(WorkspaceAuditTest, AppendsLineToFile) {
  WorkspaceAudit audit(dir_.GetPath(), /*ring_buffer_size=*/200);
  audit.Append(MakeEntry("2026-05-17T00:00:00Z", "write", "notes.md",
                         "\"bytes\":42,\"created\":true"));
  std::string out;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII(".audit.log"), &out));
  EXPECT_NE(std::string::npos, out.find("\"op\":\"write\""));
  EXPECT_NE(std::string::npos, out.find("\"path\":\"notes.md\""));
  EXPECT_EQ('\n', out.back());
}

TEST_F(WorkspaceAuditTest, RingBufferKeepsRecent) {
  WorkspaceAudit audit(dir_.GetPath(), /*ring_buffer_size=*/3);
  for (int i = 0; i < 5; ++i) {
    audit.Append(MakeEntry("2026-05-17T00:00:00Z", "write",
                           base::StringPrintf("f%d.md", i), ""));
  }
  auto recent = audit.Snapshot();
  ASSERT_EQ(3u, recent.size());
  EXPECT_EQ("f2.md", recent[0].path);
  EXPECT_EQ("f4.md", recent[2].path);
}

TEST_F(WorkspaceAuditTest, AppendOnlyAcrossInstances) {
  {
    WorkspaceAudit a1(dir_.GetPath(), 10);
    a1.Append(MakeEntry("t1", "write", "a.md", ""));
  }
  {
    WorkspaceAudit a2(dir_.GetPath(), 10);
    a2.Append(MakeEntry("t2", "write", "b.md", ""));
  }
  std::string out;
  ASSERT_TRUE(base::ReadFileToString(
      dir_.GetPath().AppendASCII(".audit.log"), &out));
  EXPECT_NE(std::string::npos, out.find("a.md"));
  EXPECT_NE(std::string::npos, out.find("b.md"));
  // Two lines = two newlines.
  EXPECT_EQ(2, std::count(out.begin(), out.end(), '\n'));
}

}  // namespace
}  // namespace dao
