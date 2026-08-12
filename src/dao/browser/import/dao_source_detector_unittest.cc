// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_source_detector.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao::import {
namespace {

TEST(DaoSourceDetectorTest, DetectsAndOrdersChromiumProfiles) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath root = temp_dir.GetPath().AppendASCII("Chrome");
  ASSERT_TRUE(base::CreateDirectory(root.AppendASCII("Default")));
  ASSERT_TRUE(base::CreateDirectory(root.AppendASCII("Profile 2")));
  ASSERT_TRUE(base::WriteFile(
      root.AppendASCII("Local State"),
      R"({"profile":{"info_cache":{"Profile 2":{"name":"Work"},"Default":{"name":"Personal"}}}})"));

  DaoSourceDetector::DetectionResult result =
      DaoSourceDetector::DetectFromRootsForTesting(
          {{SourceKind::kChrome, "Google Chrome", root}});

  ASSERT_EQ(2u, result.profiles.size());
  EXPECT_EQ("Personal", result.profiles[0].profile_name);
  EXPECT_EQ("Work", result.profiles[1].profile_name);
  EXPECT_EQ(SourceKind::kChrome, result.profiles[0].kind);
  EXPECT_EQ(5u, result.profiles[0].supported_categories.size());
  EXPECT_FALSE(result.profiles[0].id.empty());
  EXPECT_EQ(std::string::npos,
            result.profiles[0].id.find(temp_dir.GetPath().AsUTF8Unsafe()));
  EXPECT_EQ(root.AppendASCII("Default"),
            result.profile_paths.at(result.profiles[0].id));
}

TEST(DaoSourceDetectorTest, SkipsMissingCorruptAndUnknownProfiles) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath missing_root = temp_dir.GetPath().AppendASCII("Missing");
  const base::FilePath corrupt_root = temp_dir.GetPath().AppendASCII("Corrupt");
  ASSERT_TRUE(base::CreateDirectory(corrupt_root.AppendASCII("Default")));
  ASSERT_TRUE(base::WriteFile(corrupt_root.AppendASCII("Local State"), "{"));

  DaoSourceDetector::DetectionResult result =
      DaoSourceDetector::DetectFromRootsForTesting({
          {SourceKind::kArc, "Arc", missing_root},
          {SourceKind::kEdge, "Microsoft Edge", corrupt_root},
      });

  EXPECT_TRUE(result.profiles.empty());
  EXPECT_TRUE(result.profile_paths.empty());
}

TEST(DaoSourceDetectorTest, ProfileIdsAreStableAndSourceSpecific) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath profile_path = temp_dir.GetPath().AppendASCII("Default");
  ASSERT_TRUE(base::CreateDirectory(profile_path));

  const std::string first = DaoSourceDetector::BuildProfileIdForTesting(
      SourceKind::kChrome, profile_path);
  const std::string second = DaoSourceDetector::BuildProfileIdForTesting(
      SourceKind::kChrome, profile_path);
  const std::string other_source = DaoSourceDetector::BuildProfileIdForTesting(
      SourceKind::kArc, profile_path);

  EXPECT_EQ(first, second);
  EXPECT_NE(first, other_source);
  EXPECT_EQ(std::string::npos, first.find(profile_path.AsUTF8Unsafe()));
}

}  // namespace
}  // namespace dao::import
