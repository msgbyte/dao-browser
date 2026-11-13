// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/cli/dao_home_workspace.h"

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_util.h"
#include "base/test/multiprocess_test.h"
#include "base/test/test_timeouts.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/multiprocess_func_list.h"

#if BUILDFLAG(IS_POSIX)
#include <unistd.h>
#endif

namespace dao {
namespace {

TEST(DaoHomeWorkspaceTest, PrivateTemporaryBinaryCheckoutAndDiff) {
  const HomeFileSnapshot files{{"index.html", "<html></html>"},
                               {"assets/icon.png", std::string("\0\1\xff", 3)}};
  auto workspace = DaoHomeWorkspace::Create(files, base::DictValue());
  ASSERT_TRUE(workspace.has_value());
  const auto path = (*workspace)->path();
  EXPECT_TRUE(path.IsAbsolute());
  EXPECT_TRUE(base::StartsWith(path.BaseName().AsUTF8Unsafe(), "dao-home-"));
#if BUILDFLAG(IS_POSIX)
  int permissions = 0;
  ASSERT_TRUE(base::GetPosixFilePermissions(path, &permissions));
  EXPECT_EQ(0700, permissions);
#endif
  EXPECT_EQ(files, (*workspace)->Capture().value());
  EXPECT_EQ(files, (*workspace)->ReadBase().value());
  ASSERT_TRUE(
      base::WriteFile(path.AppendASCII("project/index.html"), "changed"));
  EXPECT_FALSE((*workspace)->IsClean().value());
  EXPECT_EQ("UNPUBLISHED_CHANGES", (*workspace)->Clean(false).error());
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
  EXPECT_FALSE(base::PathExists(path));
}

TEST(DaoHomeWorkspaceTest, RejectsFileAndByteQuotaBeforeWriting) {
  HomeFileSnapshot files;
  for (size_t i = 0; i <= kMaxHomeSnapshotFiles; ++i)
    files.emplace("file-" + std::to_string(i), "");
  EXPECT_EQ("QUOTA_EXCEEDED", DaoHomeWorkspace::Create(files, {}).error());
  files = {{"asset.bin", std::string(kMaxHomeSnapshotBytes + 1, 'x')}};
  EXPECT_EQ("QUOTA_EXCEEDED", DaoHomeWorkspace::Create(files, {}).error());
}

TEST(DaoHomeWorkspaceTest, RejectsRelativeAndSubstitutedPaths) {
  EXPECT_FALSE(DaoHomeWorkspace::Open(base::FilePath(FILE_PATH_LITERAL(".")))
                   .has_value());
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "original"}}, {});
  ASSERT_TRUE(workspace.has_value());
  const auto path = (*workspace)->path();
  base::ScopedTempDir other;
  ASSERT_TRUE(other.CreateUniqueTempDir());
  EXPECT_FALSE(DaoHomeWorkspace::Open(other.GetPath()).has_value());
#if BUILDFLAG(IS_POSIX)
  ASSERT_TRUE(base::DeleteFile(path.AppendASCII("project/index.html")));
  ASSERT_TRUE(base::CreateSymbolicLink(other.GetPath(),
                                       path.AppendASCII("project/linked")));
  EXPECT_FALSE((*workspace)->Capture().has_value());
  ASSERT_TRUE(base::DeleteFile(path.AppendASCII("project/linked")));
#endif
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
}

TEST(DaoHomeWorkspaceTest, AdvancesBaseWithoutOverwritingEdits) {
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  ASSERT_TRUE(workspace.has_value());
  ASSERT_TRUE(
      base::WriteFile((*workspace)->path().AppendASCII("project/index.html"),
                      "edited while publish was in flight"));
  const HomeFileSnapshot published{{"index.html", "published"}};
  ASSERT_TRUE((*workspace)->AdvanceBase("revision-2", published).has_value());
  EXPECT_EQ(published, (*workspace)->ReadBase().value());
  EXPECT_EQ("edited while publish was in flight",
            (*workspace)->Capture()->at("index.html"));
  EXPECT_FALSE((*workspace)->IsClean().value());
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
}

TEST(DaoHomeWorkspaceTest, RejectsDevelopmentArtifacts) {
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  ASSERT_TRUE(workspace.has_value());
  ASSERT_TRUE(base::CreateDirectory(
      (*workspace)->path().AppendASCII("project/node_modules")));
  ASSERT_TRUE(base::WriteFile(
      (*workspace)->path().AppendASCII("project/node_modules/a.js"), "x"));
  EXPECT_FALSE((*workspace)->Capture().has_value());
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
}

MULTIPROCESS_TEST_MAIN(HomeWorkspaceLockContender) {
  auto workspace = DaoHomeWorkspace::Open(
      base::CommandLine::ForCurrentProcess()->GetSwitchValuePath("workspace"));
  return !workspace.has_value() && workspace.error() == "WORKSPACE_BUSY" ? 0
                                                                         : 1;
}

TEST(DaoHomeWorkspaceTest, FailedCleanupKeepsMetadataAndExclusiveLock) {
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  ASSERT_TRUE(workspace.has_value());
  const auto path = (*workspace)->path();
  const auto generated = path.AppendASCII("project/generated");
  ASSERT_TRUE(base::CreateDirectory(generated));
  for (int i = 0; i <= 2000; ++i)
    ASSERT_TRUE(base::WriteFile(generated.AppendASCII(std::to_string(i)), ""));
  EXPECT_EQ("QUOTA_EXCEEDED", (*workspace)->Clean(true).error());
  EXPECT_TRUE(base::PathExists(path.AppendASCII(".dao-home/state.json")));
  EXPECT_EQ((HomeFileSnapshot{{"index.html", "base"}}),
            (*workspace)->ReadBase().value());
  // POSIX record locks can be reacquired by the same process. Test a real
  // competing process, as each CLI invocation runs separately.
  auto command_line = base::GetMultiProcessTestChildBaseCommandLine();
  command_line.AppendSwitchPath("workspace", path);
  auto contender = base::SpawnMultiProcessTestChild(
      "HomeWorkspaceLockContender", command_line, {});
  ASSERT_TRUE(contender.IsValid());
  int exit_code = -1;
  ASSERT_TRUE(base::WaitForMultiprocessTestChildExit(
      contender, TestTimeouts::action_timeout(), &exit_code));
  EXPECT_EQ(0, exit_code);
  workspace->reset();
  auto reopened = DaoHomeWorkspace::Open(path);
  ASSERT_TRUE(reopened.has_value());
  ASSERT_TRUE(base::DeleteFile(generated.AppendASCII("2000")));
  EXPECT_TRUE((*reopened)->Clean(true).has_value());
  EXPECT_FALSE(base::PathExists(path));
}

TEST(DaoHomeWorkspaceTest, RejectsDevelopmentArtifactsBeforeCreatingCheckout) {
  for (const auto* path :
       {"node_modules/a.js", "package.json", ".git/config", "src/editor.swp"}) {
    SCOPED_TRACE(path);
    EXPECT_EQ(
        "WORKSPACE_INVALID",
        DaoHomeWorkspace::Create({{"index.html", "base"}, {path, "x"}}, {})
            .error());
  }
}

TEST(DaoHomeWorkspaceTest, RoundTripsRunnableBuildAssets) {
  const HomeFileSnapshot files{{"index.html", "base"},
                               {"dist/app.js", "application"},
                               {"build/styles.css", "styles"}};
  auto workspace = DaoHomeWorkspace::Create(files, {});
  ASSERT_TRUE(workspace.has_value());
  ASSERT_TRUE((*workspace)->Capture().has_value());
  EXPECT_EQ(files, (*workspace)->Capture().value());
  EXPECT_TRUE((*workspace)->Clean(false).has_value());
}

TEST(DaoHomeWorkspaceTest, PrunesOnlyUnreferencedSnapshotsAfterSavingState) {
  const HomeFileSnapshot base{{"index.html", "base"}};
  const HomeFileSnapshot pending{{"index.html", "pending"}};
  const HomeFileSnapshot candidate{{"index.html", "candidate"}};
  auto workspace = DaoHomeWorkspace::Create(base, {});
  ASSERT_TRUE(workspace.has_value());
  ASSERT_TRUE((*workspace)->SaveSnapshot(pending).has_value());
  ASSERT_TRUE((*workspace)->SaveSnapshot(candidate).has_value());
  (*workspace)
      ->state()
      .Set("candidate_digest", HomeFileSnapshotDigest(candidate));
  (*workspace)
      ->state()
      .Set("pending_operation",
           base::DictValue().Set("digest", HomeFileSnapshotDigest(pending)));
  ASSERT_TRUE((*workspace)->SaveState().has_value());
  EXPECT_EQ(base, (*workspace)->ReadBase().value());
  EXPECT_EQ(
      pending,
      (*workspace)->ReadSnapshot(HomeFileSnapshotDigest(pending)).value());
  EXPECT_EQ(
      candidate,
      (*workspace)->ReadSnapshot(HomeFileSnapshotDigest(candidate)).value());
  ASSERT_TRUE((*workspace)->AdvanceBase("revision-2", candidate).has_value());
  EXPECT_FALSE(base::PathExists(
      (*workspace)
          ->path()
          .AppendASCII(".dao-home/base")
          .AppendASCII(HomeFileSnapshotDigest(base) + ".json")));
  EXPECT_FALSE(base::PathExists(
      (*workspace)
          ->path()
          .AppendASCII(".dao-home/base")
          .AppendASCII(HomeFileSnapshotDigest(pending) + ".json")));
  EXPECT_EQ(candidate, (*workspace)->ReadBase().value());
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
}

#if BUILDFLAG(IS_POSIX)
TEST(DaoHomeWorkspaceTest, RejectsHardLinksOutsideTheCheckout) {
  base::ScopedTempDir outside;
  ASSERT_TRUE(outside.CreateUniqueTempDir());
  const auto original = outside.GetPath().AppendASCII("private-source");
  ASSERT_TRUE(base::WriteFile(original, "private"));
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  ASSERT_TRUE(workspace.has_value());
  const auto link_path =
      (*workspace)->path().AppendASCII("project/external.txt");
  ASSERT_EQ(0, link(original.value().c_str(), link_path.value().c_str()));
  EXPECT_FALSE((*workspace)->Capture().has_value());
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
  std::string bytes;
  ASSERT_TRUE(base::ReadFileToString(original, &bytes));
  EXPECT_EQ("private", bytes);
}
#endif

TEST(DaoHomeWorkspaceTest, RecoversExactCommittedPublicationAfterResponseLoss) {
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  ASSERT_TRUE(workspace.has_value());
  const HomeFileSnapshot published{{"index.html", "published"}};
  const auto digest = HomeFileSnapshotDigest(published);
  ASSERT_TRUE((*workspace)->SaveSnapshot(published).has_value());
  (*workspace)
      ->state()
      .Set("pending_operation", base::DictValue()
                                    .Set("kind", "publish")
                                    .Set("operation_id", "operation-1")
                                    .Set("digest", digest));
  ASSERT_TRUE((*workspace)->SaveState().has_value());
  const auto path = (*workspace)->path();
  workspace->reset();
  workspace = DaoHomeWorkspace::Open(path);
  ASSERT_TRUE(workspace.has_value());
  ASSERT_TRUE(base::WriteFile(path.AppendASCII("project/index.html"),
                              "newer local edit"));
  auto outcome = base::DictValue()
                     .Set("status", "committed")
                     .Set("operation_id", "other-operation")
                     .Set("revision", "revision-2")
                     .Set("digest", digest);
  auto missing = base::DictValue()
                     .Set("status", "not_found")
                     .Set("operation_id", "operation-1");
  EXPECT_EQ("OUTCOME_UNKNOWN", (*workspace)->RecoverOperation(missing).error());
  EXPECT_TRUE((*workspace)->state().Find("pending_operation"));
  EXPECT_EQ("base", (*workspace)->ReadBase()->at("index.html"));
  EXPECT_EQ("UNPUBLISHED_CHANGES", (*workspace)->Clean(false).error());
  EXPECT_EQ("OUTCOME_UNKNOWN", (*workspace)->RecoverOperation(outcome).error());
  outcome.Set("operation_id", "operation-1");
  outcome.Set("digest", "wrong");
  EXPECT_EQ("OUTCOME_UNKNOWN", (*workspace)->RecoverOperation(outcome).error());
  outcome.Set("digest", digest);
  ASSERT_TRUE((*workspace)->RecoverOperation(outcome).has_value());
  EXPECT_FALSE((*workspace)->state().Find("pending_operation"));
  EXPECT_EQ(published, (*workspace)->ReadBase().value());
  EXPECT_EQ("newer local edit", (*workspace)->Capture()->at("index.html"));
  EXPECT_TRUE((*workspace)->Clean(true).has_value());
}

TEST(DaoHomeWorkspaceTest, CreatesNoFilesInCurrentDirectory) {
  base::ScopedTempDir cwd;
  ASSERT_TRUE(cwd.CreateUniqueTempDir());
  base::FilePath previous;
  ASSERT_TRUE(base::GetCurrentDirectory(&previous));
  ASSERT_TRUE(base::SetCurrentDirectory(cwd.GetPath()));
#if BUILDFLAG(IS_POSIX)
  EXPECT_TRUE(base::SetPosixFilePermissions(cwd.GetPath(), 0500));
#endif
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  EXPECT_TRUE(base::SetCurrentDirectory(previous));
#if BUILDFLAG(IS_POSIX)
  EXPECT_TRUE(base::SetPosixFilePermissions(cwd.GetPath(), 0700));
#endif
  ASSERT_TRUE(workspace.has_value());
  EXPECT_TRUE(base::IsDirectoryEmpty(cwd.GetPath()));
  EXPECT_TRUE((*workspace)->Clean(false).has_value());
}

TEST(DaoHomeWorkspaceTest, RejectsTamperedIdentityAndSnapshot) {
  auto workspace = DaoHomeWorkspace::Create({{"index.html", "base"}}, {});
  ASSERT_TRUE(workspace.has_value());
  const auto path = (*workspace)->path();
  const auto digest = *(*workspace)->state().FindString("base_digest");
  const auto snapshot =
      path.AppendASCII(".dao-home/base").AppendASCII(digest + ".json");
  ASSERT_TRUE(base::WriteFile(snapshot, "{}"));
  EXPECT_FALSE((*workspace)->ReadBase().has_value());
  (*workspace)->state().Set("identity", "forged");
  ASSERT_TRUE((*workspace)->SaveState().has_value());
  workspace->reset();
  EXPECT_FALSE(DaoHomeWorkspace::Open(path).has_value());
  // Remove the verified test fixture after intentionally corrupting its
  // metadata.
  EXPECT_TRUE(base::DeletePathRecursively(path));
}

}  // namespace
}  // namespace dao
