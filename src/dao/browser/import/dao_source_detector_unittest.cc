// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_source_detector.h"

#include <algorithm>
#include <array>
#include <string>

#include "base/base_paths.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/scoped_path_override.h"
#include "build/build_config.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao::import {
namespace {

#if BUILDFLAG(IS_WIN) || BUILDFLAG(IS_MAC)
TEST(DaoSourceDetectorTest, DetectsProfilesFromDefaultPlatformRoots) {
  base::ScopedTempDir app_data;
  ASSERT_TRUE(app_data.CreateUniqueTempDir());
#if BUILDFLAG(IS_WIN)
  base::ScopedPathOverride override_app_data(base::DIR_LOCAL_APP_DATA,
                                           app_data.GetPath());
  const std::array<const char*, 3> roots = {
      "Google/Chrome/User Data",
      "Packages/TheBrowserCompany.Arc_test/LocalCache/Local/Arc/User Data",
      "Microsoft/Edge/User Data"};
#else
  base::ScopedPathOverride override_app_data(base::DIR_APP_DATA,
                                           app_data.GetPath());
  const std::array<const char*, 3> roots = {"Google/Chrome", "Arc/User Data",
                                            "Microsoft Edge"};
#endif
  for (const char* relative : roots) {
    const base::FilePath root = app_data.GetPath().AppendASCII(relative);
    ASSERT_TRUE(base::CreateDirectory(root.AppendASCII("Default")));
    ASSERT_TRUE(base::WriteFile(
        root.AppendASCII("Local State"),
        R"({"profile":{"info_cache":{"Default":{"name":"Personal"}}}})"));
  }

  auto result = DaoSourceDetector::DetectDefaultRootsForTesting();

  ASSERT_EQ(3u, result.profiles.size());
  EXPECT_EQ(SourceKind::kChrome, result.profiles[0].kind);
  EXPECT_EQ(SourceKind::kArc, result.profiles[1].kind);
  EXPECT_EQ(SourceKind::kEdge, result.profiles[2].kind);
  for (size_t index = 0; index < result.profiles.size(); ++index) {
    EXPECT_EQ(app_data.GetPath()
                  .AppendASCII(roots[index])
                  .AppendASCII("Default")
                  .NormalizePathSeparators(),
              result.profile_paths.at(result.profiles[index].id)
                  .NormalizePathSeparators());
  }
}
#endif

TEST(DaoSourceDetectorTest, CatalogMatchesPlatformSupport) {
  const auto browsers = DaoSourceDetector::GetBrowserDefinitions();
  bool has_safari = false;
  bool has_firefox = false;
  for (const auto& browser : browsers) {
    has_safari |= browser.kind == SourceKind::kSafari;
    has_firefox |= browser.kind == SourceKind::kFirefox;
    EXPECT_EQ(browser.relative_path != nullptr,
              DaoSourceDetector::UsesChromiumProfile(browser.kind));
  }
  EXPECT_TRUE(has_firefox);
  EXPECT_EQ(BUILDFLAG(IS_MAC), has_safari);
}

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
  const auto& source = result.profiles[0];
  EXPECT_EQ(BUILDFLAG(IS_MAC) ? 5u : 4u, source.supported_categories.size());
  EXPECT_EQ(BUILDFLAG(IS_MAC), source.passwords_use_keychain);
  EXPECT_EQ(BUILDFLAG(IS_MAC),
            std::ranges::find(source.supported_categories,
                              DataCategory::kPasswords) !=
                source.supported_categories.end());
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
