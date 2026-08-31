// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_STORE_H_
#define DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_STORE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/types/expected.h"

namespace sql {
class Database;
}

namespace dao {

enum class DaoForegroundActivityBucket {
  kMorning = 0,
  kAfternoon = 1,
  kEvening = 2,
  kNightBeforeMorning = 3,
  kNightAfterEvening = 4,
};

struct DaoForegroundActivityRow {
  std::string local_date;
  DaoForegroundActivityBucket bucket;
  std::string host;
  int64_t foreground_ms = 0;
};

using DaoForegroundActivityDelta = DaoForegroundActivityRow;

struct DaoForegroundActivitySnapshot {
  DaoForegroundActivitySnapshot();
  ~DaoForegroundActivitySnapshot();
  DaoForegroundActivitySnapshot(const DaoForegroundActivitySnapshot&);
  DaoForegroundActivitySnapshot& operator=(
      const DaoForegroundActivitySnapshot&);
  DaoForegroundActivitySnapshot(DaoForegroundActivitySnapshot&&) noexcept;
  DaoForegroundActivitySnapshot& operator=(
      DaoForegroundActivitySnapshot&&) noexcept;

  bool available = false;
  base::Time tracking_started_at;
  std::string retained_from_date;
  std::vector<DaoForegroundActivityRow> rows;
};

enum class DaoForegroundActivityStoreError {
  kInvalidInput,
  kDatabase,
};

// Blocking aggregate store. All methods must run on one sequence.
class DaoForegroundActivityStore {
 public:
  explicit DaoForegroundActivityStore(const base::FilePath& profile_path);
  ~DaoForegroundActivityStore();

  DaoForegroundActivityStore(const DaoForegroundActivityStore&) = delete;
  DaoForegroundActivityStore& operator=(const DaoForegroundActivityStore&) =
      delete;

  base::expected<void, DaoForegroundActivityStoreError> Initialize();
  base::expected<DaoForegroundActivitySnapshot, DaoForegroundActivityStoreError>
  StartTracking(base::Time tracking_started_at);
  base::expected<DaoForegroundActivitySnapshot, DaoForegroundActivityStoreError>
  ApplyDeltasAndQuery(const std::string& start_date,
                      const std::string& end_date,
                      const std::string& today,
                      const std::vector<DaoForegroundActivityDelta>& deltas);

 private:
  base::FilePath db_path_;
  std::unique_ptr<sql::Database> db_;
  base::Time tracking_started_at_;
  std::string last_pruned_date_;

  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace dao

#endif  // DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_STORE_H_
