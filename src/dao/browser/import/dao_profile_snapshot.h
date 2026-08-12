// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_PROFILE_SNAPSHOT_H_
#define DAO_BROWSER_IMPORT_DAO_PROFILE_SNAPSHOT_H_

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/callback_forward.h"
#include "base/memory/ref_counted.h"
#include "base/task/sequenced_task_runner.h"

namespace dao::import {

class SnapshotCancellationFlag
    : public base::RefCountedThreadSafe<SnapshotCancellationFlag> {
 public:
  SnapshotCancellationFlag();

  void Cancel();
  bool IsCancelled() const;

 private:
  friend class base::RefCountedThreadSafe<SnapshotCancellationFlag>;
  ~SnapshotCancellationFlag();

  std::atomic_bool cancelled_{false};
};

struct SnapshotRequest {
  SnapshotRequest();
  SnapshotRequest(const SnapshotRequest&);
  SnapshotRequest& operator=(const SnapshotRequest&);
  ~SnapshotRequest();

  base::FilePath source_profile;
  std::vector<base::FilePath> relative_paths;
  scoped_refptr<SnapshotCancellationFlag> cancellation;
  bool include_sqlite_sidecars = false;
  int max_attempts = 3;
};

struct SnapshotResult {
  SnapshotResult();
  SnapshotResult(SnapshotResult&&);
  SnapshotResult& operator=(SnapshotResult&&);
  SnapshotResult(const SnapshotResult&) = delete;
  SnapshotResult& operator=(const SnapshotResult&) = delete;
  ~SnapshotResult();

  bool success = false;
  std::string error_code;
  base::FilePath path;

 private:
  friend class DaoProfileSnapshot;
  using TempDirPtr =
      std::unique_ptr<base::ScopedTempDir, base::OnTaskRunnerDeleter>;
  TempDirPtr temp_dir;
};

class DaoProfileSnapshot {
 public:
  using CreateCallback = base::OnceCallback<void(SnapshotResult result)>;

  static void Create(SnapshotRequest request, CreateCallback callback);
  static SnapshotResult CreateForTesting(const SnapshotRequest& request);

 private:
  static SnapshotResult CreateOnBlockingThread(SnapshotRequest request);
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_PROFILE_SNAPSHOT_H_
