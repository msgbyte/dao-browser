// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_service.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

class DaoAgentWorkspaceServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(profile_dir_.CreateUniqueTempDir());
    service_ =
        std::make_unique<DaoAgentWorkspaceService>(profile_dir_.GetPath());
  }

  template <typename T, typename Op>
  base::expected<T, WorkspaceError> Sync(Op op) {
    base::expected<T, WorkspaceError> out =
        base::unexpected(WorkspaceError::kIoError);
    base::RunLoop loop;
    op(base::BindLambdaForTesting(
        [&](base::expected<T, WorkspaceError> result) {
          out = std::move(result);
          loop.Quit();
        }));
    loop.Run();
    return out;
  }

  base::test::TaskEnvironment task_env_;
  base::ScopedTempDir profile_dir_;
  std::unique_ptr<DaoAgentWorkspaceService> service_;
};

TEST_F(DaoAgentWorkspaceServiceTest, WorkspaceRootCreatedLazily) {
  base::FilePath ws =
      profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");

  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "hello", std::move(cb));
  });
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(base::PathExists(ws.AppendASCII("notes.md")));
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteCreatesFileAndReadReturnsIt) {
  auto write = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "hello\nworld\n", std::move(cb));
  });
  ASSERT_TRUE(write.has_value());
  EXPECT_EQ(12u, write->bytes_written);
  EXPECT_TRUE(write->created);

  auto read = Sync<ReadResult>([&](auto cb) {
    service_->Read("notes.md", /*offset=*/0, /*limit=*/100, std::move(cb));
  });
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ("hello\nworld\n", read->content);
  EXPECT_EQ(2, read->total_lines);
  EXPECT_EQ(2, read->returned_lines);
  EXPECT_FALSE(read->truncated);
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteOverwritesExistingFile) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "old", std::move(cb));
  });
  auto write = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "new", std::move(cb));
  });
  ASSERT_TRUE(write.has_value());
  EXPECT_FALSE(write->created);
}

TEST_F(DaoAgentWorkspaceServiceTest, ReadPaginatesLargeFile) {
  std::string content;
  for (int i = 0; i < 1000; ++i) {
    content += base::StringPrintf("line %d\n", i);
  }
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("big.md", content, std::move(cb));
  });

  auto read = Sync<ReadResult>([&](auto cb) {
    service_->Read("big.md", /*offset=*/10, /*limit=*/5, std::move(cb));
  });
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(1000, read->total_lines);
  EXPECT_EQ(5, read->returned_lines);
  EXPECT_TRUE(read->truncated);
  EXPECT_EQ("line 10\nline 11\nline 12\nline 13\nline 14\n", read->content);
}

TEST_F(DaoAgentWorkspaceServiceTest, ReadNotFound) {
  auto result = Sync<ReadResult>([&](auto cb) {
    service_->Read("missing.md", 0, 100, std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kNotFound, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteRejectsBinaryExtension) {
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("photo.png", "fakepayload", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kBinaryRejected, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteRejectsNulByte) {
  std::string nul_payload(10, 'a');
  nul_payload[3] = '\0';
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("trick.md", nul_payload, std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kBinaryRejected, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteRejectsInvalidPath) {
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("../escape.md", "x", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteRejectsPerFileQuota) {
  std::string big(6 * 1024 * 1024, 'x');
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("big.md", big, std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kQuotaExceeded, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteUpdatesIndexAndAudit) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "hi\n", std::move(cb)); });

  base::FilePath ws =
      profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");
  std::string index;
  ASSERT_TRUE(base::ReadFileToString(ws.AppendASCII("WORKSPACE.md"), &index));
  EXPECT_NE(std::string::npos, index.find("`notes.md`"));

  std::string audit;
  ASSERT_TRUE(base::ReadFileToString(ws.AppendASCII(".audit.log"), &audit));
  EXPECT_NE(std::string::npos, audit.find("\"op\":\"write\""));
  EXPECT_NE(std::string::npos, audit.find("\"path\":\"notes.md\""));
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteIsAtomic) {
  // Force a write failure mid-flight by pre-creating a directory at the
  // target path — the staged rename will fail and the temp file should
  // be cleaned up rather than left in place.
  base::FilePath ws =
      profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");
  ASSERT_TRUE(base::CreateDirectory(ws.AppendASCII("notes.md")));
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "hello", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kIoError, result.error());

  // .workspace_tmp should be empty (no leftover).
  base::FileEnumerator enumerator(ws.AppendASCII(".workspace_tmp"),
                                  /*recursive=*/true,
                                  base::FileEnumerator::FILES);
  EXPECT_TRUE(enumerator.Next().empty());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditUniqueMatchReplaces) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "alpha\nbeta\ngamma\n", std::move(cb));
  });
  auto edit = Sync<WriteResult>([&](auto cb) {
    service_->Edit("notes.md", "beta", "BETA", std::move(cb));
  });
  ASSERT_TRUE(edit.has_value());

  auto read = Sync<ReadResult>([&](auto cb) {
    service_->Read("notes.md", 0, 100, std::move(cb));
  });
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ("alpha\nBETA\ngamma\n", read->content);
}

TEST_F(DaoAgentWorkspaceServiceTest, EditRejectsNotUnique) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "x\nx\nx\n", std::move(cb));
  });
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Edit("notes.md", "x", "Y", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kEditNotUnique, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditNotFoundWhenMissing) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "alpha", std::move(cb));
  });
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Edit("notes.md", "missing", "X", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  // old_str not present → kNotFound (per spec §7 error table).
  EXPECT_EQ(WorkspaceError::kNotFound, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditOnNonexistentFile) {
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Edit("ghost.md", "x", "y", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kNotFound, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditAuditsAsEditNotWrite) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "alpha", std::move(cb));
  });
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Edit("notes.md", "alpha", "BETA", std::move(cb));
  });
  std::string audit;
  ASSERT_TRUE(base::ReadFileToString(
      profile_dir_.GetPath()
          .AppendASCII("DaoAgentWorkspace")
          .AppendASCII(".audit.log"),
      &audit));
  EXPECT_NE(std::string::npos, audit.find("\"op\":\"edit\""));
}

TEST_F(DaoAgentWorkspaceServiceTest, ApplyPatchAddUpdateDelete) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("keep.md", "hello\n", std::move(cb));
  });
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("gone.md", "bye\n", std::move(cb));
  });

  std::string patch =
      "*** Begin Patch\n"
      "*** Add File: brand_new.md\n"
      "+fresh\n"
      "*** Update File: keep.md\n"
      "@@\n"
      "-hello\n"
      "+HELLO\n"
      "*** End of File\n"
      "*** Delete File: gone.md\n"
      "*** End Patch\n";
  auto r = Sync<PatchResult>([&](auto cb) {
    service_->ApplyPatch(patch, std::move(cb));
  });
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(1u, r->added.size());
  EXPECT_EQ(1u, r->updated.size());
  EXPECT_EQ(1u, r->deleted.size());
}

TEST_F(DaoAgentWorkspaceServiceTest, ApplyPatchRollsBack) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("a.md", "old-a\n", std::move(cb));
  });
  std::string patch =
      "*** Begin Patch\n"
      "*** Update File: a.md\n"
      "@@\n"
      "-old-a\n"
      "+new-a\n"
      "*** End of File\n"
      "*** Update File: a.md\n"  // duplicate destination → rejected
      "@@\n"
      "-x\n"
      "+y\n"
      "*** End of File\n"
      "*** End Patch\n";
  auto r = Sync<PatchResult>([&](auto cb) {
    service_->ApplyPatch(patch, std::move(cb));
  });
  ASSERT_FALSE(r.has_value());
  // a.md must be unchanged.
  auto read = Sync<ReadResult>([&](auto cb) {
    service_->Read("a.md", 0, 100, std::move(cb));
  });
  EXPECT_EQ("old-a\n", read->content);
}

TEST_F(DaoAgentWorkspaceServiceTest, ApplyPatchAuditsAggregateOp) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("a.md", "x\n", std::move(cb));
  });
  std::string patch =
      "*** Begin Patch\n"
      "*** Add File: b.md\n+y\n"
      "*** End Patch\n";
  std::ignore = Sync<PatchResult>([&](auto cb) {
    service_->ApplyPatch(patch, std::move(cb));
  });
  std::string audit;
  ASSERT_TRUE(base::ReadFileToString(
      profile_dir_.GetPath()
          .AppendASCII("DaoAgentWorkspace")
          .AppendASCII(".audit.log"),
      &audit));
  EXPECT_NE(std::string::npos, audit.find("\"op\":\"apply_patch\""));
}

}  // namespace
}  // namespace dao
