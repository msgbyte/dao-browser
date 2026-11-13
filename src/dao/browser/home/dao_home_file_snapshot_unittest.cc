// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_file_snapshot.h"

#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(DaoHomeFileSnapshotTest, PreservesBinaryBytesAndHashesCanonicalOrder) {
  const HomeFileSnapshot files = {{"index.html", "<main>Home</main>"},
                                  {"assets/icon.bin", std::string("\0\xff"
                                                                  "A",
                                                                  3)}};
  const auto encoded = EncodeHomeFileSnapshot(files);
  EXPECT_EQ("AP9B", *encoded.FindString("assets/icon.bin"));
  const auto decoded = DecodeHomeFileSnapshot(encoded);
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(files, *decoded);
  EXPECT_EQ("4d900a319ac274f851c7008534883b5292a1b938f34411f925c9b6b6688d9094",
            HomeFileSnapshotDigest(files));
  EXPECT_NE(HomeFileSnapshotDigest({{"a", "bc"}}),
            HomeFileSnapshotDigest({{"ab", "c"}}));
}

TEST(DaoHomeFileSnapshotTest, RejectsTraversalAliasesAndConflictingPaths) {
  for (const std::string& path : {"../outside", "/absolute", "a//b", "a/./b",
                                  "a\\b", "a:b"}) {
    SCOPED_TRACE(path);
    EXPECT_FALSE(ValidateHomeFileSnapshot({{path, "x"}}).has_value());
  }
  EXPECT_FALSE(
      ValidateHomeFileSnapshot({{"a", "x"}, {"a/b", "y"}}).has_value());
  EXPECT_FALSE(
      ValidateHomeFileSnapshot({{"Index.html", "x"}, {"index.html", "y"}})
          .has_value());
  EXPECT_FALSE(
      ValidateHomeFileSnapshot({{"Dir/a", "x"}, {"dir/b", "y"}}).has_value());
}

TEST(DaoHomeFileSnapshotTest, AppliesHostPlatformFilenameRestrictions) {
  for (const char* path :
       {"src/aux.js", "CON.txt", "prn/entry.js", "nul.txt", "com1.txt",
        "lpt9/icon.svg", "assets/star*.svg", "assets/query?.txt",
        "assets/quote\".txt", "assets/less<.txt", "assets/greater>.txt",
        "assets/pipe|.txt", "assets/trailing.", "assets/trailing ",
        "directory./entry.js", "directory /entry.js"}) {
    SCOPED_TRACE(path);
    const HomeFileSnapshot files = {{path, "contents"}};
    const auto valid = ValidateHomeFileSnapshot(files);
    const auto decoded = DecodeHomeFileSnapshot(EncodeHomeFileSnapshot(files));
#if BUILDFLAG(IS_WIN)
    ASSERT_FALSE(valid.has_value());
    EXPECT_EQ(HomeError::kInvalidPath, valid.error());
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(HomeError::kInvalidPath, decoded.error());
#else
    ASSERT_TRUE(valid.has_value());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(files, *decoded);
#endif
  }
}

TEST(DaoHomeFileSnapshotTest, EnforcesDecodedLimitsAndRejectsInvalidEncoding) {
  HomeFileSnapshot files;
  for (size_t i = 0; i < kMaxHomeSnapshotFiles; ++i) {
    files.emplace("files/" + std::to_string(i), "");
  }
  ASSERT_TRUE(ValidateHomeFileSnapshot(files).has_value());
  files.emplace("extra", "");
  EXPECT_EQ(HomeError::kQuotaExceeded, ValidateHomeFileSnapshot(files).error());
  EXPECT_EQ(HomeError::kQuotaExceeded,
            ValidateHomeFileSnapshot(
                {{"large.bin", std::string(kMaxHomeSnapshotBytes + 1, 'x')}})
                .error());
  EXPECT_EQ(
      HomeError::kInvalidArgument,
      DecodeHomeFileSnapshot(base::DictValue().Set("a", "!bad!")).error());
  EXPECT_EQ(HomeError::kInvalidArgument,
            DecodeHomeFileSnapshot(base::DictValue().Set("a", 42)).error());
}

}  // namespace
}  // namespace dao
