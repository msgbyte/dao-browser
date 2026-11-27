// Copyright 2026 Dao Browser Authors. All rights reserved.

#include "dao/browser/agent/workspace/v4a_patch_applier.h"

#include <memory>
#include <string>
#include <vector>

#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "dao/browser/agent/workspace/v4a_patch_parser.h"
#include "dao/browser/agent/workspace/workspace_quota.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

class V4APatchApplierTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    root_ = dir_.GetPath();
    quota_ = std::make_unique<WorkspaceQuota>(root_);
  }

  V4APatch Parse(const std::string& text) {
    auto r = ParseV4APatch(text);
    EXPECT_TRUE(r.has_value()) << r.error().message;
    return *r;
  }

  base::ScopedTempDir dir_;
  base::FilePath root_;
  std::unique_ptr<WorkspaceQuota> quota_;
};

TEST_F(V4APatchApplierTest, AddCreatesFile) {
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Add File: new.md\n"
      "+hello\n"
      "+world\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(std::vector<std::string>{"new.md"}, r->added);

  std::string body;
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("new.md"), &body));
  EXPECT_EQ("hello\nworld\n", body);
}

TEST_F(V4APatchApplierTest, AddFailsIfFileExists) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("dup.md"), "x"));
  auto patch = Parse(
      "*** Begin Patch\n*** Add File: dup.md\n+x\n*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(WorkspaceError::kAlreadyExists, r.error());
}

TEST_F(V4APatchApplierTest, UpdateAppliesHunk) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("notes.md"),
                              "alpha\nbeta\ngamma\n"));
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Update File: notes.md\n"
      "@@\n"
      " alpha\n"
      "-beta\n"
      "+BETA\n"
      " gamma\n"
      "*** End of File\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(std::vector<std::string>{"notes.md"}, r->updated);

  std::string body;
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("notes.md"), &body));
  EXPECT_EQ("alpha\nBETA\ngamma\n", body);
}

TEST_F(V4APatchApplierTest, UpdateRejectsAmbiguousContext) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("notes.md"),
                              "x\nx\nx\n"));
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Update File: notes.md\n"
      "@@\n"
      "-x\n"
      "+Y\n"
      "*** End of File\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(WorkspaceError::kPatchContextMismatch, r.error());
}

TEST_F(V4APatchApplierTest, UpdateMissingContextFails) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("notes.md"), "alpha\n"));
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Update File: notes.md\n"
      "@@\n"
      " missing\n"
      "-also-missing\n"
      "+x\n"
      "*** End of File\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(WorkspaceError::kPatchContextMismatch, r.error());
}

TEST_F(V4APatchApplierTest, UpdateWithMoveRenames) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("old.md"), "alpha\nbeta\n"));
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Update File: old.md\n"
      "*** Move to: new.md\n"
      "@@\n"
      " alpha\n"
      "-beta\n"
      "+BETA\n"
      "*** End of File\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("old.md")));
  std::string body;
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("new.md"), &body));
  EXPECT_EQ("alpha\nBETA\n", body);
  ASSERT_EQ(1u, r->moved.size());
  EXPECT_EQ("old.md", r->moved[0].first);
  EXPECT_EQ("new.md", r->moved[0].second);
}

TEST_F(V4APatchApplierTest, DeleteRemovesFile) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("gone.md"), "x"));
  auto patch = Parse(
      "*** Begin Patch\n*** Delete File: gone.md\n*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_TRUE(r.has_value());
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("gone.md")));
  EXPECT_EQ(std::vector<std::string>{"gone.md"}, r->deleted);
}

TEST_F(V4APatchApplierTest, RollsBackOnPartialFailure) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("a.md"), "old-a"));
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("b.md"), "old-b"));
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Update File: a.md\n"
      "@@\n"
      "-old-a\n"
      "+new-a\n"
      "*** End of File\n"
      "*** Update File: b.md\n"
      "@@\n"
      " missing-context\n"
      "-x\n"
      "+y\n"
      "*** End of File\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(r.has_value());

  // Both files must be untouched.
  std::string a, b;
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("a.md"), &a));
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("b.md"), &b));
  EXPECT_EQ("old-a", a);
  EXPECT_EQ("old-b", b);
}

TEST_F(V4APatchApplierTest, AddsNestedUnicodeFileWithoutWorkspaceStaging) {
  const base::FilePath marker = root_.AppendASCII(".workspace_tmp");
  ASSERT_TRUE(base::WriteFile(marker, "original"));
  auto patch = Parse(
      "*** Begin Patch\n*** Add File: notes/笔记.md\n+hello\n*** End Patch\n");
  ASSERT_TRUE(ApplyV4APatch(root_, quota_.get(), patch).has_value());

  std::string body;
  ASSERT_TRUE(base::ReadFileToString(
      root_.Append(base::FilePath::FromUTF8Unsafe("notes/笔记.md")), &body));
  EXPECT_EQ("hello\n", body);
  ASSERT_TRUE(base::ReadFileToString(marker, &body));
  EXPECT_EQ("original", body);
}

TEST_F(V4APatchApplierTest, RejectsFileAndChildBeforeWritingEither) {
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Add File: notes.md\n+parent\n"
      "*** Add File: notes.md/child.md\n+child\n"
      "*** End Patch\n");
  EXPECT_FALSE(ApplyV4APatch(root_, quota_.get(), patch).has_value());
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("notes.md")));
}

TEST_F(V4APatchApplierTest, PreflightsNamesUsingFilesystemEquivalence) {
  struct Names {
    const char* first;
    const char* other;
  };
  for (const auto& names :
       {Names{"Note.md", "note.md"}, Names{"caf\u00e9.md", "cafe\u0301.md"}}) {
    const auto first = base::FilePath::FromUTF8Unsafe(names.first);
    const auto other = base::FilePath::FromUTF8Unsafe(names.other);
    ASSERT_TRUE(base::WriteFile(root_.Append(first), "probe"));
    const bool aliases = base::PathExists(root_.Append(other));
    for (bool child : {false, true}) {
      SCOPED_TRACE(names.first);
      SCOPED_TRACE(child);
      base::ScopedTempDir project;
      ASSERT_TRUE(project.CreateUniqueTempDirUnderPath(root_));
      WorkspaceQuota quota(project.GetPath());
      std::string second = names.other;
      if (child) {
        second += "/child.md";
      }
      auto patch =
          Parse(std::string("*** Begin Patch\n") +
                "*** Add File: early.md\n+early\n*** Add File: " + names.first +
                "\n+one\n*** Add File: " + second + "\n+two\n*** End Patch\n");
      auto r = ApplyV4APatch(project.GetPath(), &quota, patch);
      if (aliases) {
        ASSERT_FALSE(r.has_value());
        EXPECT_EQ(WorkspaceError::kPatchParseError, r.error());
        EXPECT_TRUE(base::IsDirectoryEmpty(project.GetPath()));
      } else {
        ASSERT_TRUE(r.has_value());
        std::string body;
        ASSERT_TRUE(
            base::ReadFileToString(project.GetPath().Append(first), &body));
        EXPECT_EQ("one\n", body);
        ASSERT_TRUE(base::ReadFileToString(
            project.GetPath().Append(base::FilePath::FromUTF8Unsafe(second)),
            &body));
        EXPECT_EQ("two\n", body);
      }
    }
  }
}

TEST_F(V4APatchApplierTest, AddRejectsBinaryExtension) {
  auto patch = Parse(
      "*** Begin Patch\n*** Add File: bad.png\n+x\n*** End Patch\n");
  auto r = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(WorkspaceError::kBinaryRejected, r.error());
}

TEST_F(V4APatchApplierTest, RejectsCombinedBytesBeforePublishing) {
  base::File filler(root_.AppendASCII("filler.txt"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes - 3));
  filler.Close();
  auto patch = Parse("*** Begin Patch\n*** Add File: a.md\n+a\n"
                     "*** Add File: b.md\n+b\n*** End Patch\n");
  auto result = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kQuotaExceeded, result.error());
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("a.md")));
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("b.md")));
}

TEST_F(V4APatchApplierTest, RejectsCombinedEntriesBeforePublishing) {
  for (size_t i = 1; i < WorkspaceQuota::kMaxEntries; ++i) {
    ASSERT_TRUE(base::WriteFile(root_.AppendASCII(std::to_string(i)), ""));
  }
  auto patch = Parse("*** Begin Patch\n*** Add File: a.md\n+a\n"
                     "*** Add File: b.md\n+b\n*** End Patch\n");
  auto result = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kQuotaExceeded, result.error());
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("a.md")));
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("b.md")));
}

TEST_F(V4APatchApplierTest, NetsEmptyMovesAndDeletesAgainstAddsInEitherOrder) {
  for (size_t i = 2; i < WorkspaceQuota::kMaxEntries; ++i) {
    ASSERT_TRUE(base::WriteFile(root_.AppendASCII(std::to_string(i)), ""));
  }
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("empty.md"), ""));
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("old.md"), "old\n"));
  base::File filler(root_.AppendASCII("2"),
                    base::File::FLAG_OPEN | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes - 4));
  filler.Close();
  auto add_first = Parse("*** Begin Patch\n*** Add File: new.md\n+new\n"
                         "*** Update File: empty.md\n*** Move to: moved.md\n"
                         "*** End of File\n"
                         "*** Delete File: old.md\n*** End Patch\n");
  ASSERT_TRUE(ApplyV4APatch(root_, quota_.get(), add_first).has_value());
  EXPECT_TRUE(base::PathExists(root_.AppendASCII("moved.md")));
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("empty.md")));
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("old.md")));
  auto delete_first = Parse("*** Begin Patch\n*** Delete File: new.md\n"
                            "*** Add File: again.md\n+end\n*** End Patch\n");
  ASSERT_TRUE(ApplyV4APatch(root_, quota_.get(), delete_first).has_value());
  EXPECT_TRUE(base::PathExists(root_.AppendASCII("again.md")));
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("new.md")));
}

TEST_F(V4APatchApplierTest, DeletingBookkeepingDoesNotFreeChargedBytes) {
  base::File filler(root_.AppendASCII("filler.txt"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes));
  filler.Close();
  for (const char* metadata : {"WORKSPACE.md", ".audit.log"}) {
    const auto path = root_.AppendASCII(metadata);
    ASSERT_TRUE(base::WriteFile(path, "metadata"));
    auto patch = Parse(std::string("*** Begin Patch\n*** Delete File: ") +
                       metadata + "\n*** Add File: a.md\n+a\n*** End Patch\n");
    auto result = ApplyV4APatch(root_, quota_.get(), patch);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(WorkspaceError::kQuotaExceeded, result.error());
    EXPECT_TRUE(base::PathExists(path));
    EXPECT_FALSE(base::PathExists(root_.AppendASCII("a.md")));
  }
}

TEST_F(V4APatchApplierTest, RefreshesUsageAfterExternalFileChanges) {
  EXPECT_EQ(0u, quota_->GetUsage().entry_count);
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("external.md"), "changed"));
  base::File filler(root_.AppendASCII("filler.txt"),
                    base::File::FLAG_CREATE | base::File::FLAG_WRITE);
  ASSERT_TRUE(filler.SetLength(WorkspaceQuota::kTotalMaxBytes - 7));
  filler.Close();
  auto patch = Parse("*** Begin Patch\n*** Add File: new.md\n+x\n*** End Patch\n");
  auto result = ApplyV4APatch(root_, quota_.get(), patch);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(WorkspaceError::kQuotaExceeded, result.error());
  EXPECT_FALSE(base::PathExists(root_.AppendASCII("new.md")));
}

}  // namespace
}  // namespace dao
