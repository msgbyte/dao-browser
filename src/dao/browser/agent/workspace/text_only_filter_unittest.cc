// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/text_only_filter.h"

#include <string>

#include "base/files/file_path.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(TextOnlyFilterTest, AcceptsAllowedExtensions) {
  EXPECT_TRUE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("notes.md"))));
  EXPECT_TRUE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("data.json"))));
  EXPECT_TRUE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("table.csv"))));
  EXPECT_TRUE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("styles.css"))));
  EXPECT_TRUE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("runtime.js"))));
  EXPECT_TRUE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("Document.TXT"))));
}

TEST(TextOnlyFilterTest, RejectsBinaryExtensions) {
  EXPECT_FALSE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("image.png"))));
  EXPECT_FALSE(
      IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("archive.zip"))));
  EXPECT_FALSE(IsTextExtensionAllowed(base::FilePath(FILE_PATH_LITERAL("noext"))));
}

TEST(TextOnlyFilterTest, NulByteProbeDetectsBinary) {
  std::string binary("abc\0def", 7);
  EXPECT_TRUE(ContainsNulByte(binary));
}

TEST(TextOnlyFilterTest, NulByteProbeAcceptsText) {
  EXPECT_FALSE(ContainsNulByte("plain text content"));
  EXPECT_FALSE(ContainsNulByte(""));
}

}  // namespace
}  // namespace dao
