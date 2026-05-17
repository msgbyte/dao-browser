// Copyright 2026 Dao Browser Authors. All rights reserved.

#include "dao/browser/agent/workspace/v4a_patch_applier.h"

#include <memory>
#include <string>
#include <tuple>
#include <vector>

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
    staging_ = root_.AppendASCII(".workspace_tmp");
    ASSERT_TRUE(base::CreateDirectory(staging_));
    quota_ = std::make_unique<WorkspaceQuota>(root_);
  }

  V4APatch Parse(const std::string& text) {
    auto r = ParseV4APatch(text);
    EXPECT_TRUE(r.has_value()) << r.error().message;
    return *r;
  }

  base::ScopedTempDir dir_;
  base::FilePath root_;
  base::FilePath staging_;
  std::unique_ptr<WorkspaceQuota> quota_;
};

TEST_F(V4APatchApplierTest, AddCreatesFile) {
  auto patch = Parse(
      "*** Begin Patch\n"
      "*** Add File: new.md\n"
      "+hello\n"
      "+world\n"
      "*** End Patch\n");
  auto r = ApplyV4APatch(root_, staging_, "req1", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req2", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req3", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req4", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req5", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req6", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req7", quota_.get(), patch);
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
  auto r = ApplyV4APatch(root_, staging_, "req8", quota_.get(), patch);
  ASSERT_FALSE(r.has_value());

  // Both files must be untouched.
  std::string a, b;
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("a.md"), &a));
  ASSERT_TRUE(base::ReadFileToString(root_.AppendASCII("b.md"), &b));
  EXPECT_EQ("old-a", a);
  EXPECT_EQ("old-b", b);
}

TEST_F(V4APatchApplierTest, StagingDirCleanedUpAfterSuccess) {
  auto patch = Parse(
      "*** Begin Patch\n*** Add File: x.md\n+x\n*** End Patch\n");
  std::ignore =
      ApplyV4APatch(root_, staging_, "req9", quota_.get(), patch);
  EXPECT_FALSE(base::PathExists(staging_.AppendASCII("req9")));
}

TEST_F(V4APatchApplierTest, StagingDirCleanedUpAfterFailure) {
  ASSERT_TRUE(base::WriteFile(root_.AppendASCII("dup.md"), "x"));
  auto patch = Parse(
      "*** Begin Patch\n*** Add File: dup.md\n+x\n*** End Patch\n");
  std::ignore =
      ApplyV4APatch(root_, staging_, "req10", quota_.get(), patch);
  EXPECT_FALSE(base::PathExists(staging_.AppendASCII("req10")));
}

TEST_F(V4APatchApplierTest, AddRejectsBinaryExtension) {
  auto patch = Parse(
      "*** Begin Patch\n*** Add File: bad.png\n+x\n*** End Patch\n");
  auto r = ApplyV4APatch(root_, staging_, "req11", quota_.get(), patch);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(WorkspaceError::kBinaryRejected, r.error());
}

}  // namespace
}  // namespace dao
