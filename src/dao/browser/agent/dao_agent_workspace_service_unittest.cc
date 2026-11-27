// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_service.h"

#include <memory>
#include <string>
#include <tuple>
#include <utility>

#include "base/containers/span.h"
#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "dao/browser/agent/dao_agent_workspace_types.h"
#include "dao/browser/agent/workspace/workspace_file.h"
#include "dao/browser/agent/workspace/workspace_quota.h"
#include "storage/browser/blob/scoped_file.h"
#include "testing/gtest/include/gtest/gtest.h"

#if BUILDFLAG(IS_WIN)
#include <windows.h>
#else
#include <sys/file.h>
#include <sys/stat.h>
#endif

namespace dao {
namespace {

std::string PublicationIdForTest(base::PlatformFile file) {
#if BUILDFLAG(IS_WIN)
  BY_HANDLE_FILE_INFORMATION info{};
  EXPECT_TRUE(::GetFileInformationByHandle(file, &info));
  return std::to_string(info.dwVolumeSerialNumber) + "-" +
         std::to_string((uint64_t{info.nFileIndexHigh} << 32) |
                        info.nFileIndexLow);
#else
  struct stat info{};
  EXPECT_EQ(0, fstat(file, &info));
  return std::to_string(static_cast<uint64_t>(info.st_dev)) + "-" +
         std::to_string(info.st_ino);
#endif
}

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

  base::expected<WriteResult, WorkspaceError> IngestDownload(
      const base::FilePath& temp,
      const std::string& relative) {
    return Sync<WriteResult>([&](auto cb) {
      service_->IngestDownloadedFile(
          relative,
          storage::ScopedFile(temp, storage::ScopedFile::DELETE_ON_SCOPE_OUT,
                              base::SequencedTaskRunner::GetCurrentDefault()),
          "", std::move(cb));
    });
  }

  base::test::TaskEnvironment task_env_;
  base::ScopedTempDir profile_dir_;
  std::unique_ptr<DaoAgentWorkspaceService> service_;
};

TEST_F(DaoAgentWorkspaceServiceTest, WorkspaceRootCreatedLazily) {
  base::FilePath ws = profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");

  auto result = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "hello", std::move(cb)); });
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(base::PathExists(ws.AppendASCII("notes.md")));
}

TEST_F(DaoAgentWorkspaceServiceTest, RecoversOnlyLegacyStagingFilesOnStartup) {
  const auto root = service_->workspace_root();
  service_.reset();
  task_env_.RunUntilIdle();

  const auto staging = root.AppendASCII(".workspace_tmp");
  const char owned_name[] = "12345678-1234-4abc-9abc-123456789abc";
  const auto pending = staging.AppendASCII(owned_name);
  const auto keep = staging.AppendASCII("my-notes.txt");
  const auto user_hidden =
      root.AppendASCII(".dao-12345678-1234-4abc-9abc-123456789abc");
  const auto directory =
      staging.AppendASCII("22345678-1234-4abc-9abc-123456789abc");
  ASSERT_TRUE(base::CreateDirectory(directory));
  ASSERT_TRUE(base::WriteFile(pending, "unfinished"));
  ASSERT_TRUE(base::WriteFile(keep, "notes"));
  ASSERT_TRUE(base::WriteFile(user_hidden, "user file"));
  ASSERT_TRUE(base::WriteFile(directory.AppendASCII(owned_name), "keep"));
#if BUILDFLAG(IS_POSIX)
  const auto link = staging.AppendASCII("32345678-1234-4abc-9abc-123456789abc");
  ASSERT_TRUE(base::CreateSymbolicLink(keep, link));
#endif

  service_ = std::make_unique<DaoAgentWorkspaceService>(profile_dir_.GetPath());
  task_env_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(pending));
  EXPECT_TRUE(base::PathExists(keep));
  EXPECT_TRUE(base::PathExists(user_hidden));
  EXPECT_TRUE(base::PathExists(directory.AppendASCII(owned_name)));
#if BUILDFLAG(IS_POSIX)
  EXPECT_TRUE(base::IsLink(link));
#endif
}

TEST_F(DaoAgentWorkspaceServiceTest, RecoversOnlyIdleRegisteredPublications) {
  const auto root = service_->workspace_root();
  service_.reset();
  task_env_.RunUntilIdle();
  const auto staging =
      root.AppendASCII("docs").Append(kWorkspacePublishDirectory);
  ASSERT_TRUE(base::CreateDirectory(staging));
  const auto directory = OpenWorkspaceDirectory(root, {});
#if BUILDFLAG(IS_WIN)
  ASSERT_FALSE(directory.empty());
  const std::string root_id =
      PublicationIdForTest(directory.back().GetPlatformFile());
#else
  ASSERT_TRUE(directory.is_valid());
  const std::string root_id = PublicationIdForTest(directory.get());
#endif
  const auto create_slot = [&](const char* content) {
    const auto seed = staging.AppendASCII("seed");
    base::File file(seed, base::File::FLAG_CREATE | base::File::FLAG_WRITE);
    EXPECT_TRUE(file.IsValid());
    EXPECT_TRUE(file.WriteAtCurrentPosAndCheck(
        base::as_byte_span(std::string_view(content))));
    const auto registered = staging.AppendASCII(
        "dir-" + root_id + "." + PublicationIdForTest(file.GetPlatformFile()));
    file.Close();
    EXPECT_TRUE(base::Move(seed, registered));
    return registered;
  };
  const auto partial = create_slot("unfinished");
  const auto active = create_slot("active");
  base::File lock(active, base::File::FLAG_OPEN | base::File::FLAG_READ |
                              base::File::FLAG_WIN_EXCLUSIVE_READ);
  ASSERT_TRUE(lock.IsValid());
#if !BUILDFLAG(IS_WIN)
  ASSERT_EQ(0, flock(lock.GetPlatformFile(), LOCK_EX | LOCK_NB));
#endif
  const auto unrelated = staging.AppendASCII("dir-" + root_id + ".0-0");
  ASSERT_TRUE(base::WriteFile(unrelated, "user file"));

  service_ = std::make_unique<DaoAgentWorkspaceService>(profile_dir_.GetPath());
  task_env_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(partial));
  EXPECT_TRUE(base::PathExists(active));
  EXPECT_TRUE(base::PathExists(unrelated));
  lock.Close();
  service_.reset();
  task_env_.RunUntilIdle();
  service_ = std::make_unique<DaoAgentWorkspaceService>(profile_dir_.GetPath());
  task_env_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(active));
  EXPECT_TRUE(base::PathExists(unrelated));
}

TEST_F(DaoAgentWorkspaceServiceTest, CreatePublicationPreservesExistingTarget) {
  task_env_.RunUntilIdle();
  const auto root = service_->workspace_root();
  const auto path = base::FilePath::FromASCII("notes.md");
  const auto temp = profile_dir_.GetPath().AppendASCII("body");
  ASSERT_TRUE(base::WriteFile(root.Append(path), "notes"));
  ASSERT_TRUE(base::WriteFile(temp, "replacement"));
  base::File source(temp, base::File::FLAG_OPEN | base::File::FLAG_READ);
  ASSERT_TRUE(source.IsValid());
  auto result =
      CopyToWorkspaceFile(root, path, source, WorkspacePublishMode::kCreate);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kAlreadyExists, result.error());
  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(root.Append(path), &contents));
  EXPECT_EQ("notes", contents);
  EXPECT_FALSE(base::PathExists(root.Append(kWorkspacePublishDirectory)));
}

TEST_F(DaoAgentWorkspaceServiceTest,
       QueuedWriteCompletesAfterServiceDestruction) {
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Write("queued.md", "hello", std::move(cb));
    service_.reset();
  });
  ASSERT_TRUE(result.has_value());
  std::string content;
  ASSERT_TRUE(base::ReadFileToString(profile_dir_.GetPath()
                                         .AppendASCII("DaoAgentWorkspace")
                                         .AppendASCII("queued.md"),
                                     &content));
  EXPECT_EQ("hello", content);
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
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "old", std::move(cb)); });
  auto write = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "new", std::move(cb)); });
  ASSERT_TRUE(write.has_value());
  EXPECT_FALSE(write->created);
}

TEST_F(DaoAgentWorkspaceServiceTest, ListRecursesOnlyVisibleDirectories) {
  auto write = Sync<WriteResult>([&](auto cb) {
    service_->Write("files/notes.md", "hello", std::move(cb));
  });
  ASSERT_TRUE(write.has_value());
  const auto root = profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");
  const auto files = root.AppendASCII("files");
  const auto hidden = files.AppendASCII(".private");
  ASSERT_TRUE(base::CreateDirectory(hidden));
  ASSERT_TRUE(base::WriteFile(hidden.AppendASCII("hidden.md"), "hidden"));
#if BUILDFLAG(IS_POSIX)
  ASSERT_TRUE(base::CreateSymbolicLink(files, root.AppendASCII("alias")));
  ASSERT_TRUE(base::CreateSymbolicLink(files.AppendASCII("notes.md"),
                                       root.AppendASCII("alias.md")));
#endif
  auto list = Sync<ListResult>(
      [&](auto cb) { service_->List("", /*recursive=*/true, std::move(cb)); });
  ASSERT_TRUE(list.has_value());
  EXPECT_EQ(2u, list->total);
  EXPECT_FALSE(list->truncated);
  ASSERT_EQ(2u, list->entries.size());
  EXPECT_EQ("files/", list->entries[0].path);
  EXPECT_TRUE(list->entries[0].is_dir);
  EXPECT_EQ("files/notes.md", list->entries[1].path);
  EXPECT_EQ(5u, list->entries[1].size_bytes);
  EXPECT_FALSE(list->entries[1].mtime.empty());
}

TEST_F(DaoAgentWorkspaceServiceTest, ReadPaginatesLargeFile) {
  std::string content;
  for (int i = 0; i < 1000; ++i) {
    content += base::StringPrintf("line %d\n", i);
  }
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("big.md", content, std::move(cb)); });

  auto read = Sync<ReadResult>([&](auto cb) {
    service_->Read("big.md", /*offset=*/10, /*limit=*/5, std::move(cb));
  });
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ(1000, read->total_lines);
  EXPECT_EQ(5, read->returned_lines);
  EXPECT_TRUE(read->truncated);
  EXPECT_EQ("line 10\nline 11\nline 12\nline 13\nline 14\n", read->content);
}

TEST_F(DaoAgentWorkspaceServiceTest, ReadPreservesLinesAcrossChunksAndAtEof) {
  const std::string long_line(150000, 'x');
  ASSERT_TRUE(Sync<WriteResult>([&](auto cb) {
                service_->Write("lines.txt", "\n" + long_line + "\r\n\nlast",
                                std::move(cb));
              }).has_value());
  auto middle = Sync<ReadResult>(
      [&](auto cb) { service_->Read("lines.txt", 1, 1, std::move(cb)); });
  ASSERT_TRUE(middle.has_value());
  EXPECT_EQ(long_line + "\r\n", middle->content);
  EXPECT_EQ(4, middle->total_lines);
  EXPECT_TRUE(middle->truncated);
  auto tail = Sync<ReadResult>(
      [&](auto cb) { service_->Read("lines.txt", 2, 5, std::move(cb)); });
  ASSERT_TRUE(tail.has_value());
  EXPECT_EQ("\nlast\n", tail->content);
  EXPECT_EQ(2, tail->returned_lines);
  EXPECT_FALSE(tail->truncated);
}

TEST_F(DaoAgentWorkspaceServiceTest, ReadNotFound) {
  auto result = Sync<ReadResult>(
      [&](auto cb) { service_->Read("missing.md", 0, 100, std::move(cb)); });
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
  auto result = Sync<WriteResult>(
      [&](auto cb) { service_->Write("../escape.md", "x", std::move(cb)); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kInvalidPath, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteRejectsFullWorkspace) {
  task_env_.RunUntilIdle();
  base::File filler(service_->workspace_root().AppendASCII("filler.txt"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.IsValid());
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes));
  filler.Close();
  auto result = Sync<WriteResult>(
      [&](auto cb) { service_->Write("extra.md", "x", std::move(cb)); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kQuotaExceeded, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, DownloadPublishesAllBytesAfterProbe) {
  const auto temp = profile_dir_.GetPath().AppendASCII("response");
  const std::string content = std::string(8192, 'a') + "last line\n";
  ASSERT_TRUE(base::WriteFile(temp, content));
  auto result = IngestDownload(temp, "pages/response.txt");
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->created);
  EXPECT_EQ(content.size(), result->bytes_written);
  std::string stored;
  const auto target =
      service_->workspace_root().AppendASCII("pages").AppendASCII(
          "response.txt");
  ASSERT_TRUE(base::ReadFileToString(target, &stored));
  EXPECT_EQ(content, stored);
  task_env_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(temp));
}

TEST_F(DaoAgentWorkspaceServiceTest, DownloadRejectsBinaryAndCleansUp) {
  auto original = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "original", std::move(cb)); });
  ASSERT_TRUE(original.has_value());
  const auto temp = profile_dir_.GetPath().AppendASCII("response");
  std::string content(8192, 'x');
  content.back() = '\0';
  ASSERT_TRUE(base::WriteFile(temp, content));
  auto result = IngestDownload(temp, "notes.md");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kBinaryRejected, result.error());
  std::string stored;
  ASSERT_TRUE(base::ReadFileToString(
      service_->workspace_root().AppendASCII("notes.md"), &stored));
  EXPECT_EQ("original", stored);
  task_env_.RunUntilIdle();
  EXPECT_FALSE(base::PathExists(temp));
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteUpdatesIndexAndAudit) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "hi\n", std::move(cb)); });

  base::FilePath ws = profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");
  std::string index;
  ASSERT_TRUE(base::ReadFileToString(ws.AppendASCII("WORKSPACE.md"), &index));
  EXPECT_NE(std::string::npos, index.find("`notes.md`"));

  std::string audit;
  ASSERT_TRUE(base::ReadFileToString(ws.AppendASCII(".audit.log"), &audit));
  EXPECT_NE(std::string::npos, audit.find("\"op\":\"write\""));
  EXPECT_NE(std::string::npos, audit.find("\"path\":\"notes.md\""));
}

TEST_F(DaoAgentWorkspaceServiceTest, WriteIsAtomic) {
  base::FilePath ws = profile_dir_.GetPath().AppendASCII("DaoAgentWorkspace");
  ASSERT_TRUE(base::CreateDirectory(ws.AppendASCII("notes.md")));
  auto result = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "hello", std::move(cb)); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kIoError, result.error());

  EXPECT_TRUE(base::DirectoryExists(ws.AppendASCII("notes.md")));
  base::FileEnumerator enumerator(ws, /*recursive=*/true,
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

  auto read = Sync<ReadResult>(
      [&](auto cb) { service_->Read("notes.md", 0, 100, std::move(cb)); });
  ASSERT_TRUE(read.has_value());
  EXPECT_EQ("alpha\nBETA\ngamma\n", read->content);
}

TEST_F(DaoAgentWorkspaceServiceTest, EditRejectsNotUnique) {
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Write("notes.md", "x\nx\nx\n", std::move(cb));
  });
  auto result = Sync<WriteResult>(
      [&](auto cb) { service_->Edit("notes.md", "x", "Y", std::move(cb)); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kEditNotUnique, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditNotFoundWhenMissing) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "alpha", std::move(cb)); });
  auto result = Sync<WriteResult>([&](auto cb) {
    service_->Edit("notes.md", "missing", "X", std::move(cb));
  });
  ASSERT_FALSE(result.has_value());
  // old_str not present → kNotFound (per spec §7 error table).
  EXPECT_EQ(WorkspaceError::kNotFound, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditOnNonexistentFile) {
  auto result = Sync<WriteResult>(
      [&](auto cb) { service_->Edit("ghost.md", "x", "y", std::move(cb)); });
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kNotFound, result.error());
}

TEST_F(DaoAgentWorkspaceServiceTest, EditAuditsAsEditNotWrite) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("notes.md", "alpha", std::move(cb)); });
  std::ignore = Sync<WriteResult>([&](auto cb) {
    service_->Edit("notes.md", "alpha", "BETA", std::move(cb));
  });
  std::string audit;
  ASSERT_TRUE(base::ReadFileToString(profile_dir_.GetPath()
                                         .AppendASCII("DaoAgentWorkspace")
                                         .AppendASCII(".audit.log"),
                                     &audit));
  EXPECT_NE(std::string::npos, audit.find("\"op\":\"edit\""));
}

TEST_F(DaoAgentWorkspaceServiceTest, ApplyPatchAddUpdateDelete) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("keep.md", "hello\n", std::move(cb)); });
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("gone.md", "bye\n", std::move(cb)); });

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
  auto r = Sync<PatchResult>(
      [&](auto cb) { service_->ApplyPatch(patch, std::move(cb)); });
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(1u, r->added.size());
  EXPECT_EQ(1u, r->updated.size());
  EXPECT_EQ(1u, r->deleted.size());
}

TEST_F(DaoAgentWorkspaceServiceTest, ApplyPatchRollsBack) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("a.md", "old-a\n", std::move(cb)); });
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
  auto r = Sync<PatchResult>(
      [&](auto cb) { service_->ApplyPatch(patch, std::move(cb)); });
  ASSERT_FALSE(r.has_value());
  // a.md must be unchanged.
  auto read = Sync<ReadResult>(
      [&](auto cb) { service_->Read("a.md", 0, 100, std::move(cb)); });
  EXPECT_EQ("old-a\n", read->content);
}

TEST_F(DaoAgentWorkspaceServiceTest, ApplyPatchAuditsAggregateOp) {
  std::ignore = Sync<WriteResult>(
      [&](auto cb) { service_->Write("a.md", "x\n", std::move(cb)); });
  std::string patch =
      "*** Begin Patch\n"
      "*** Add File: b.md\n+y\n"
      "*** End Patch\n";
  std::ignore = Sync<PatchResult>(
      [&](auto cb) { service_->ApplyPatch(patch, std::move(cb)); });
  std::string audit;
  ASSERT_TRUE(base::ReadFileToString(profile_dir_.GetPath()
                                         .AppendASCII("DaoAgentWorkspace")
                                         .AppendASCII(".audit.log"),
                                     &audit));
  EXPECT_NE(std::string::npos, audit.find("\"op\":\"apply_patch\""));
}

}  // namespace
}  // namespace dao
