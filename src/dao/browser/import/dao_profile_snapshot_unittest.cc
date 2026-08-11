// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_profile_snapshot.h"

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao::import {
namespace {

TEST(DaoProfileSnapshotTest, CopiesStableFileAndSqliteSidecars) {
  base::ScopedTempDir source_dir;
  ASSERT_TRUE(source_dir.CreateUniqueTempDir());
  ASSERT_TRUE(
      base::WriteFile(source_dir.GetPath().AppendASCII("History"), "database"));
  ASSERT_TRUE(
      base::WriteFile(source_dir.GetPath().AppendASCII("History-wal"), "wal"));
  ASSERT_TRUE(
      base::WriteFile(source_dir.GetPath().AppendASCII("History-shm"), "shm"));

  SnapshotRequest request;
  request.source_profile = source_dir.GetPath();
  request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("History"))};
  request.include_sqlite_sidecars = true;
  SnapshotResult result = DaoProfileSnapshot::CreateForTesting(request);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(base::PathExists(result.path.AppendASCII("History")));
  EXPECT_TRUE(base::PathExists(result.path.AppendASCII("History-wal")));
  EXPECT_TRUE(base::PathExists(result.path.AppendASCII("History-shm")));
}

TEST(DaoProfileSnapshotTest, ReportsMissingSourceWithoutLeavingSnapshot) {
  base::ScopedTempDir source_dir;
  ASSERT_TRUE(source_dir.CreateUniqueTempDir());

  SnapshotRequest request;
  request.source_profile = source_dir.GetPath();
  request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Bookmarks"))};
  SnapshotResult result = DaoProfileSnapshot::CreateForTesting(request);

  EXPECT_FALSE(result.success);
  EXPECT_EQ("source_missing", result.error_code);
  EXPECT_TRUE(result.path.empty());
}

TEST(DaoProfileSnapshotTest, RecursivelyCopiesSessionDirectory) {
  base::ScopedTempDir source_dir;
  ASSERT_TRUE(source_dir.CreateUniqueTempDir());
  const base::FilePath sessions = source_dir.GetPath().AppendASCII("Sessions");
  ASSERT_TRUE(base::CreateDirectory(sessions));
  ASSERT_TRUE(base::WriteFile(sessions.AppendASCII("Session_1"), "session"));

  SnapshotRequest request;
  request.source_profile = source_dir.GetPath();
  request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Sessions"))};
  SnapshotResult result = DaoProfileSnapshot::CreateForTesting(request);

  ASSERT_TRUE(result.success);
  EXPECT_TRUE(base::PathExists(
      result.path.AppendASCII("Sessions").AppendASCII("Session_1")));
}

TEST(DaoProfileSnapshotTest, StopsBeforeCopyWhenCancelled) {
  base::ScopedTempDir source_dir;
  ASSERT_TRUE(source_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(source_dir.GetPath().AppendASCII("Bookmarks"),
                              "bookmarks"));

  SnapshotRequest request;
  request.source_profile = source_dir.GetPath();
  request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Bookmarks"))};
  request.cancellation = base::MakeRefCounted<SnapshotCancellationFlag>();
  request.cancellation->Cancel();
  SnapshotResult result = DaoProfileSnapshot::CreateForTesting(request);

  EXPECT_FALSE(result.success);
  EXPECT_EQ("cancelled", result.error_code);
  EXPECT_TRUE(result.path.empty());
}

TEST(DaoProfileSnapshotTest, RemovesTemporaryDirectoryWithResultLifetime) {
  base::ScopedTempDir source_dir;
  ASSERT_TRUE(source_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::WriteFile(source_dir.GetPath().AppendASCII("Bookmarks"),
                              "bookmarks"));

  base::FilePath snapshot_path;
  {
    SnapshotRequest request;
    request.source_profile = source_dir.GetPath();
    request.relative_paths = {base::FilePath(FILE_PATH_LITERAL("Bookmarks"))};
    SnapshotResult result = DaoProfileSnapshot::CreateForTesting(request);
    ASSERT_TRUE(result.success);
    snapshot_path = result.path;
    EXPECT_TRUE(base::DirectoryExists(snapshot_path));
  }

  EXPECT_FALSE(base::PathExists(snapshot_path));
}

}  // namespace
}  // namespace dao::import
