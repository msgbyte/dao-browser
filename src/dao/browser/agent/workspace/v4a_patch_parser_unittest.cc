// Copyright 2026 Dao Browser Authors. All rights reserved.

#include "dao/browser/agent/workspace/v4a_patch_parser.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(V4APatchParserTest, ParsesAddFile) {
  std::string p =
      "*** Begin Patch\n"
      "*** Add File: notes.md\n"
      "+line one\n"
      "+line two\n"
      "*** End Patch\n";
  auto result = ParseV4APatch(p);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(1u, result->ops.size());
  EXPECT_EQ(V4AFileOp::Kind::kAdd, result->ops[0].kind);
  EXPECT_EQ("notes.md", result->ops[0].path);
  ASSERT_EQ(2u, result->ops[0].add_lines.size());
  EXPECT_EQ("line one", result->ops[0].add_lines[0]);
  EXPECT_EQ("line two", result->ops[0].add_lines[1]);
}

TEST(V4APatchParserTest, ParsesDeleteFile) {
  std::string p =
      "*** Begin Patch\n"
      "*** Delete File: gone.md\n"
      "*** End Patch\n";
  auto result = ParseV4APatch(p);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(1u, result->ops.size());
  EXPECT_EQ(V4AFileOp::Kind::kDelete, result->ops[0].kind);
  EXPECT_EQ("gone.md", result->ops[0].path);
}

TEST(V4APatchParserTest, ParsesUpdateFileWithHunk) {
  std::string p =
      "*** Begin Patch\n"
      "*** Update File: notes.md\n"
      "@@ first section\n"
      " context line\n"
      "-old line\n"
      "+new line\n"
      "*** End of File\n"
      "*** End Patch\n";
  auto result = ParseV4APatch(p);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(1u, result->ops.size());
  const auto& op = result->ops[0];
  EXPECT_EQ(V4AFileOp::Kind::kUpdate, op.kind);
  EXPECT_EQ("notes.md", op.path);
  EXPECT_FALSE(op.move_to.has_value());
  ASSERT_EQ(1u, op.hunks.size());
  ASSERT_TRUE(op.hunks[0].anchor.has_value());
  EXPECT_EQ("first section", *op.hunks[0].anchor);
  ASSERT_EQ(3u, op.hunks[0].lines.size());
  EXPECT_EQ(V4AHunkLine::Kind::kContext, op.hunks[0].lines[0].kind);
  EXPECT_EQ(V4AHunkLine::Kind::kRemove, op.hunks[0].lines[1].kind);
  EXPECT_EQ(V4AHunkLine::Kind::kAdd, op.hunks[0].lines[2].kind);
}

TEST(V4APatchParserTest, ParsesUpdateWithMoveTo) {
  std::string p =
      "*** Begin Patch\n"
      "*** Update File: old.md\n"
      "*** Move to: new.md\n"
      "@@\n"
      " same\n"
      "-x\n"
      "+y\n"
      "*** End of File\n"
      "*** End Patch\n";
  auto result = ParseV4APatch(p);
  ASSERT_TRUE(result.has_value());
  ASSERT_TRUE(result->ops[0].move_to.has_value());
  EXPECT_EQ("new.md", *result->ops[0].move_to);
}

TEST(V4APatchParserTest, ParsesMultipleHunks) {
  std::string p =
      "*** Begin Patch\n"
      "*** Update File: notes.md\n"
      "@@\n"
      " a\n"
      "-b\n"
      "+B\n"
      "@@\n"
      " c\n"
      "-d\n"
      "+D\n"
      "*** End of File\n"
      "*** End Patch\n";
  auto result = ParseV4APatch(p);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2u, result->ops[0].hunks.size());
}

TEST(V4APatchParserTest, ParsesMultipleFiles) {
  std::string p =
      "*** Begin Patch\n"
      "*** Add File: a.md\n"
      "+hello\n"
      "*** Delete File: b.md\n"
      "*** End Patch\n";
  auto result = ParseV4APatch(p);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(2u, result->ops.size());
}

TEST(V4APatchParserTest, ErrorOnMissingBeginPatch) {
  auto result = ParseV4APatch("*** Add File: x.md\n+hi\n*** End Patch\n");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(1, result.error().line_number);
}

TEST(V4APatchParserTest, ErrorOnMissingEndPatch) {
  auto result = ParseV4APatch(
      "*** Begin Patch\n*** Add File: x.md\n+hi\n");
  ASSERT_FALSE(result.has_value());
}

TEST(V4APatchParserTest, ErrorOnUpdateMissingEndOfFile) {
  auto result = ParseV4APatch(
      "*** Begin Patch\n"
      "*** Update File: x.md\n"
      "@@\n"
      " a\n"
      "-b\n"
      "+B\n"
      "*** End Patch\n");
  ASSERT_FALSE(result.has_value());
}

TEST(V4APatchParserTest, ErrorOnUnknownDirective) {
  auto result = ParseV4APatch(
      "*** Begin Patch\n*** Frobnicate: x.md\n*** End Patch\n");
  ASSERT_FALSE(result.has_value());
}

TEST(V4APatchParserTest, ErrorOnMoveToWithoutUpdate) {
  auto result = ParseV4APatch(
      "*** Begin Patch\n"
      "*** Add File: x.md\n"
      "*** Move to: y.md\n"
      "+hi\n"
      "*** End Patch\n");
  ASSERT_FALSE(result.has_value());
}

}  // namespace
}  // namespace dao
