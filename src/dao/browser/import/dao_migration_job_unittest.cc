// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_job.h"

#include <string>
#include <utility>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace dao::import {
namespace {

SourceProfile TestSource(std::string id) {
  SourceProfile source;
  source.id = std::move(id);
  source.kind = SourceKind::kChrome;
  source.supported_categories = {
      DataCategory::kBookmarks, DataCategory::kHistory,
      DataCategory::kPasswords, DataCategory::kTabs, DataCategory::kExtensions};
  return source;
}

TEST(DaoMigrationJobTest, RunsSelectedCategoryThroughOrderedPhases) {
  DaoMigrationJob job(TestSource("chrome:default"),
                      {DataCategory::kBookmarks, DataCategory::kPasswords});

  EXPECT_TRUE(job.StartCategory(DataCategory::kBookmarks));
  EXPECT_EQ(CategoryPhase::kSnapshotting,
            job.GetCategoryState(DataCategory::kBookmarks).phase);
  EXPECT_TRUE(
      job.AdvanceCategory(DataCategory::kBookmarks, CategoryPhase::kReading));
  EXPECT_TRUE(
      job.AdvanceCategory(DataCategory::kBookmarks, CategoryPhase::kWriting));

  CategoryResult result;
  result.imported = 12;
  result.skipped = 2;
  EXPECT_TRUE(job.CompleteCategory(DataCategory::kBookmarks, result));
  EXPECT_EQ(CategoryPhase::kSucceeded,
            job.GetCategoryState(DataCategory::kBookmarks).phase);
  EXPECT_EQ(12u,
            job.GetCategoryState(DataCategory::kBookmarks).result.imported);
  EXPECT_TRUE(job.StartCategory(DataCategory::kPasswords));
}

TEST(DaoMigrationJobTest, RejectsPhaseJumpsAndUnselectedCategories) {
  DaoMigrationJob job(TestSource("arc:personal"), {DataCategory::kHistory});

  EXPECT_FALSE(job.StartCategory(DataCategory::kBookmarks));
  EXPECT_TRUE(job.StartCategory(DataCategory::kHistory));
  EXPECT_FALSE(
      job.AdvanceCategory(DataCategory::kHistory, CategoryPhase::kWriting));
  EXPECT_EQ(CategoryPhase::kSnapshotting,
            job.GetCategoryState(DataCategory::kHistory).phase);
}

TEST(DaoMigrationJobTest, KeepsOtherCategoriesPendingAfterFailure) {
  DaoMigrationJob job(TestSource("edge:default"),
                      {DataCategory::kBookmarks, DataCategory::kTabs});

  ASSERT_TRUE(job.StartCategory(DataCategory::kBookmarks));
  ASSERT_TRUE(job.FailCategory(DataCategory::kBookmarks, "source_changing"));

  EXPECT_EQ(CategoryPhase::kFailed,
            job.GetCategoryState(DataCategory::kBookmarks).phase);
  EXPECT_EQ("source_changing",
            job.GetCategoryState(DataCategory::kBookmarks).error_code);
  EXPECT_EQ(CategoryPhase::kPending,
            job.GetCategoryState(DataCategory::kTabs).phase);
  EXPECT_TRUE(job.StartCategory(DataCategory::kTabs));
}

TEST(DaoMigrationJobTest, FailedCategoryPreservesPartialResult) {
  DaoMigrationJob job(TestSource("chrome:default"),
                      {DataCategory::kExtensions});
  ASSERT_TRUE(job.StartCategory(DataCategory::kExtensions));
  ASSERT_TRUE(
      job.AdvanceCategory(DataCategory::kExtensions, CategoryPhase::kReading));
  ASSERT_TRUE(
      job.AdvanceCategory(DataCategory::kExtensions, CategoryPhase::kWriting));
  CategoryResult partial;
  partial.imported = 2;
  partial.failed = 1;

  ASSERT_TRUE(job.FailCategory(DataCategory::kExtensions,
                               "extension_install_failed", partial));

  const CategoryState& state = job.GetCategoryState(DataCategory::kExtensions);
  EXPECT_EQ(CategoryPhase::kFailed, state.phase);
  EXPECT_EQ(2u, state.result.imported);
  EXPECT_EQ(1u, state.result.failed);
}

TEST(DaoMigrationJobTest, ClampsProgressToKnownTotal) {
  DaoMigrationJob job(TestSource("chrome:work"), {DataCategory::kHistory});

  ASSERT_TRUE(job.StartCategory(DataCategory::kHistory));
  ASSERT_TRUE(job.UpdateProgress(DataCategory::kHistory, 140, 100));

  const CategoryState& state = job.GetCategoryState(DataCategory::kHistory);
  EXPECT_EQ(100u, state.completed_items);
  EXPECT_EQ(100u, state.total_items);
  EXPECT_FALSE(state.indeterminate);
}

TEST(DaoMigrationJobTest, CancelsPendingWorkAtSafeBatchBoundary) {
  DaoMigrationJob job(TestSource("chrome:default"),
                      {DataCategory::kBookmarks, DataCategory::kPasswords});

  ASSERT_TRUE(job.StartCategory(DataCategory::kBookmarks));
  ASSERT_TRUE(
      job.AdvanceCategory(DataCategory::kBookmarks, CategoryPhase::kReading));

  job.RequestCancel();

  EXPECT_TRUE(job.cancel_requested());
  EXPECT_EQ(CategoryPhase::kReading,
            job.GetCategoryState(DataCategory::kBookmarks).phase);
  EXPECT_EQ(CategoryPhase::kCancelled,
            job.GetCategoryState(DataCategory::kPasswords).phase);
  CategoryResult partial;
  partial.imported = 4;
  EXPECT_TRUE(job.CancelRunningCategoryAtBatchBoundary(partial));
  EXPECT_EQ(CategoryPhase::kCancelled,
            job.GetCategoryState(DataCategory::kBookmarks).phase);
  EXPECT_EQ(4u, job.GetCategoryState(DataCategory::kBookmarks).result.imported);
  EXPECT_TRUE(job.IsTerminal());
}

TEST(DaoMigrationJobTest, OwnsSelectedSourceAcrossCatalogRefresh) {
  SourceProfile source = TestSource("chrome:default");
  source.profile_name = "Personal";
  DaoMigrationJob job(source, {DataCategory::kBookmarks});

  source.profile_name.clear();

  EXPECT_EQ("chrome:default", job.source().id);
  EXPECT_EQ("Personal", job.source().profile_name);
}

}  // namespace
}  // namespace dao::import
