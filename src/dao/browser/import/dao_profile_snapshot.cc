// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_profile_snapshot.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "base/files/file.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/notreached.h"
#include "base/task/thread_pool.h"

namespace dao::import {
namespace {

enum class CopyOutcome {
  kCopied,
  kMissing,
  kPermissionDenied,
  kChanging,
  kFailed,
  kCancelled,
};

bool FileInfoMatches(const base::File::Info& left,
                     const base::File::Info& right) {
  return left.size == right.size && left.last_modified == right.last_modified;
}

CopyOutcome CopyStableFile(const base::FilePath& source,
                           const base::FilePath& destination,
                           int max_attempts,
                           const SnapshotCancellationFlag* cancellation,
                           bool required) {
  for (int attempt = 0; attempt < std::max(1, max_attempts); ++attempt) {
    if (cancellation && cancellation->IsCancelled()) {
      return CopyOutcome::kCancelled;
    }

    base::File::Info before;
    if (!base::GetFileInfo(source, &before)) {
      if (!base::PathExists(source)) {
        return required ? CopyOutcome::kMissing : CopyOutcome::kCopied;
      }
      return CopyOutcome::kPermissionDenied;
    }
    if (before.is_directory) {
      return CopyOutcome::kFailed;
    }

    if (!base::CreateDirectory(destination.DirName()) ||
        !base::CopyFile(source, destination)) {
      return CopyOutcome::kFailed;
    }

    base::File::Info after;
    base::File::Info copied;
    if (base::GetFileInfo(source, &after) &&
        base::GetFileInfo(destination, &copied) &&
        FileInfoMatches(before, after) && before.size == copied.size) {
      return CopyOutcome::kCopied;
    }
    base::DeleteFile(destination);
  }
  return CopyOutcome::kChanging;
}

CopyOutcome CopyStableDirectory(const base::FilePath& source,
                                const base::FilePath& destination,
                                int max_attempts,
                                const SnapshotCancellationFlag* cancellation) {
  if (!base::DirectoryExists(source)) {
    return CopyOutcome::kMissing;
  }
  if (!base::CreateDirectory(destination)) {
    return CopyOutcome::kFailed;
  }
  base::FileEnumerator files(source, true, base::FileEnumerator::FILES);
  for (base::FilePath file = files.Next(); !file.empty(); file = files.Next()) {
    base::FilePath relative;
    if (!source.AppendRelativePath(file, &relative)) {
      return CopyOutcome::kFailed;
    }
    CopyOutcome outcome = CopyStableFile(file, destination.Append(relative),
                                         max_attempts, cancellation, true);
    if (outcome != CopyOutcome::kCopied) {
      return outcome;
    }
  }
  return cancellation && cancellation->IsCancelled() ? CopyOutcome::kCancelled
                                                     : CopyOutcome::kCopied;
}

std::string ErrorCodeForOutcome(CopyOutcome outcome) {
  switch (outcome) {
    case CopyOutcome::kCopied:
      return std::string();
    case CopyOutcome::kMissing:
      return "source_missing";
    case CopyOutcome::kPermissionDenied:
      return "permission_denied";
    case CopyOutcome::kChanging:
      return "source_changing";
    case CopyOutcome::kFailed:
      return "copy_failed";
    case CopyOutcome::kCancelled:
      return "cancelled";
  }
  NOTREACHED();
}

}  // namespace

SnapshotCancellationFlag::SnapshotCancellationFlag() = default;
SnapshotCancellationFlag::~SnapshotCancellationFlag() = default;

void SnapshotCancellationFlag::Cancel() {
  cancelled_.store(true, std::memory_order_relaxed);
}

bool SnapshotCancellationFlag::IsCancelled() const {
  return cancelled_.load(std::memory_order_relaxed);
}

SnapshotRequest::SnapshotRequest() = default;
SnapshotRequest::SnapshotRequest(const SnapshotRequest&) = default;
SnapshotRequest& SnapshotRequest::operator=(const SnapshotRequest&) = default;
SnapshotRequest::~SnapshotRequest() = default;

SnapshotResult::SnapshotResult()
    : temp_dir(nullptr, base::OnTaskRunnerDeleter(nullptr)) {}
SnapshotResult::SnapshotResult(SnapshotResult&&) = default;
SnapshotResult& SnapshotResult::operator=(SnapshotResult&&) = default;
SnapshotResult::~SnapshotResult() = default;

// static
void DaoProfileSnapshot::Create(SnapshotRequest request,
                                CreateCallback callback) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE,
      {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
       base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
      base::BindOnce(&DaoProfileSnapshot::CreateOnBlockingThread,
                     std::move(request)),
      std::move(callback));
}

// static
SnapshotResult DaoProfileSnapshot::CreateForTesting(
    const SnapshotRequest& request) {
  return CreateOnBlockingThread(request);
}

// static
SnapshotResult DaoProfileSnapshot::CreateOnBlockingThread(
    SnapshotRequest request) {
  SnapshotResult result;
  if (request.cancellation && request.cancellation->IsCancelled()) {
    result.error_code = "cancelled";
    return result;
  }

  SnapshotResult::TempDirPtr temp_dir(
      new base::ScopedTempDir(),
      base::OnTaskRunnerDeleter(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})));
  if (!temp_dir->CreateUniqueTempDir()) {
    result.error_code = "copy_failed";
    return result;
  }

  for (const base::FilePath& relative_path : request.relative_paths) {
    if (relative_path.IsAbsolute() || relative_path.ReferencesParent()) {
      result.error_code = "copy_failed";
      return result;
    }

    const base::FilePath source = request.source_profile.Append(relative_path);
    const base::FilePath destination =
        temp_dir->GetPath().Append(relative_path);
    CopyOutcome outcome =
        base::DirectoryExists(source)
            ? CopyStableDirectory(source, destination, request.max_attempts,
                                  request.cancellation.get())
            : CopyStableFile(source, destination, request.max_attempts,
                             request.cancellation.get(), true);
    if (outcome != CopyOutcome::kCopied) {
      result.error_code = ErrorCodeForOutcome(outcome);
      return result;
    }

    if (!request.include_sqlite_sidecars || base::DirectoryExists(source)) {
      continue;
    }
    for (std::string_view suffix : {"-wal", "-shm"}) {
      outcome = CopyStableFile(
          base::FilePath(source.value() + std::string(suffix)),
          base::FilePath(destination.value() + std::string(suffix)),
          request.max_attempts, request.cancellation.get(), false);
      if (outcome != CopyOutcome::kCopied) {
        result.error_code = ErrorCodeForOutcome(outcome);
        return result;
      }
    }
  }

  result.success = true;
  result.path = temp_dir->GetPath();
  result.temp_dir = std::move(temp_dir);
  return result;
}

}  // namespace dao::import
