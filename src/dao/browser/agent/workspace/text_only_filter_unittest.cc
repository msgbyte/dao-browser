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
  EXPECT_TRUE(IsTextExtensionAllowed(base::FilePath("notes.md")));
  EXPECT_TRUE(IsTextExtensionAllowed(base::FilePath("data.json")));
  EXPECT_TRUE(IsTextExtensionAllowed(base::FilePath("table.csv")));
  EXPECT_TRUE(IsTextExtensionAllowed(base::FilePath("Document.TXT")));
}

TEST(TextOnlyFilterTest, RejectsBinaryExtensions) {
  EXPECT_FALSE(IsTextExtensionAllowed(base::FilePath("image.png")));
  EXPECT_FALSE(IsTextExtensionAllowed(base::FilePath("archive.zip")));
  EXPECT_FALSE(IsTextExtensionAllowed(base::FilePath("noext")));
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
