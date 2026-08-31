// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/activity/dao_foreground_activity_store.h"

#include <optional>
#include <tuple>
#include <utility>

#include "base/i18n/time_formatting.h"
#include "base/logging.h"
#include "base/numerics/checked_math.h"
#include "base/strings/stringprintf.h"
#include "sql/database.h"
#include "sql/statement.h"
#include "sql/transaction.h"

namespace dao {

DaoForegroundActivitySnapshot::DaoForegroundActivitySnapshot() = default;
DaoForegroundActivitySnapshot::~DaoForegroundActivitySnapshot() = default;
DaoForegroundActivitySnapshot::DaoForegroundActivitySnapshot(
    const DaoForegroundActivitySnapshot&) = default;
DaoForegroundActivitySnapshot& DaoForegroundActivitySnapshot::operator=(
    const DaoForegroundActivitySnapshot&) = default;
DaoForegroundActivitySnapshot::DaoForegroundActivitySnapshot(
    DaoForegroundActivitySnapshot&&) noexcept = default;
DaoForegroundActivitySnapshot& DaoForegroundActivitySnapshot::operator=(
    DaoForegroundActivitySnapshot&&) noexcept = default;

namespace {

constexpr char kDatabaseName[] = "DaoForegroundActivity.db";
constexpr char kTrackingStartedAtKey[] = "tracking_started_at";
constexpr int kPreviousDatesToRetain = 370;

std::optional<base::Time> ParseLocalDate(const std::string& value) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
    return std::nullopt;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    if (i != 4 && i != 7 && (value[i] < '0' || value[i] > '9')) {
      return std::nullopt;
    }
  }

  auto number = [&value](size_t offset, size_t length) {
    int result = 0;
    for (size_t i = offset; i < offset + length; ++i) {
      result = result * 10 + value[i] - '0';
    }
    return result;
  };
  base::Time::Exploded exploded = {};
  exploded.year = number(0, 4);
  exploded.month = number(5, 2);
  exploded.day_of_month = number(8, 2);
  base::Time parsed;
  if (!base::Time::FromUTCExploded(exploded, &parsed)) {
    return std::nullopt;
  }
  base::Time::Exploded round_trip;
  parsed.UTCExplode(&round_trip);
  if (round_trip.year != exploded.year || round_trip.month != exploded.month ||
      round_trip.day_of_month != exploded.day_of_month) {
    return std::nullopt;
  }
  return parsed;
}

std::string FormatDate(base::Time time, bool local) {
  base::Time::Exploded exploded;
  if (local) {
    time.LocalExplode(&exploded);
  } else {
    time.UTCExplode(&exploded);
  }
  return base::StringPrintf("%04d-%02d-%02d", exploded.year, exploded.month,
                            exploded.day_of_month);
}

std::optional<std::string> RetainedFromDate(const std::string& today) {
  std::optional<base::Time> parsed = ParseLocalDate(today);
  if (!parsed) {
    return std::nullopt;
  }
  return FormatDate(*parsed - base::Days(kPreviousDatesToRetain), false);
}

bool IsValidBucket(int bucket) {
  return bucket >= static_cast<int>(DaoForegroundActivityBucket::kMorning) &&
         bucket <=
             static_cast<int>(DaoForegroundActivityBucket::kNightAfterEvening);
}

bool IsValidDelta(const DaoForegroundActivityDelta& delta) {
  return ParseLocalDate(delta.local_date).has_value() && !delta.host.empty() &&
         IsValidBucket(static_cast<int>(delta.bucket)) &&
         delta.foreground_ms >= 0;
}

}  // namespace

DaoForegroundActivityStore::DaoForegroundActivityStore(
    const base::FilePath& profile_path)
    : db_path_(profile_path.AppendASCII(kDatabaseName)) {
  // The store is constructed on the UI thread and used on its store sequence.
  DETACH_FROM_SEQUENCE(sequence_checker_);
}

DaoForegroundActivityStore::~DaoForegroundActivityStore() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

base::expected<void, DaoForegroundActivityStoreError>
DaoForegroundActivityStore::Initialize() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  db_ = std::make_unique<sql::Database>(
      sql::DatabaseOptions(), sql::Database::Tag("DaoForegroundActivity"));
  if (!db_->Open(db_path_)) {
    LOG(ERROR) << "Failed to open DaoForegroundActivity database";
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }
  std::ignore = db_->Execute("PRAGMA journal_mode=WAL");

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin() ||
      !db_->Execute("CREATE TABLE IF NOT EXISTS foreground_activity ("
                    "local_date TEXT NOT NULL,"
                    "bucket INTEGER NOT NULL,"
                    "host TEXT NOT NULL,"
                    "foreground_ms INTEGER NOT NULL CHECK (foreground_ms >= 0),"
                    "PRIMARY KEY (local_date, bucket, host))") ||
      !db_->Execute("CREATE TABLE IF NOT EXISTS metadata ("
                    "key TEXT PRIMARY KEY,"
                    "value TEXT NOT NULL)") ||
      !transaction.Commit()) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }

  return base::ok();
}

base::expected<DaoForegroundActivitySnapshot, DaoForegroundActivityStoreError>
DaoForegroundActivityStore::StartTracking(base::Time tracking_started_at) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!db_ || tracking_started_at.is_null()) {
    return base::unexpected(DaoForegroundActivityStoreError::kInvalidInput);
  }

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }

  sql::Statement metadata(db_->GetCachedStatement(
      SQL_FROM_HERE, "SELECT value FROM metadata WHERE key = ?"));
  metadata.BindString(0, kTrackingStartedAtKey);
  const bool found_metadata = metadata.Step();
  if (!found_metadata && !metadata.Succeeded()) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }

  std::string stored_timestamp;
  if (found_metadata) {
    if (metadata.GetColumnType(0) != sql::ColumnType::kText) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
    stored_timestamp = metadata.ColumnString(0);
    if (metadata.Step() || !metadata.Succeeded()) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
  } else {
    stored_timestamp = base::TimeFormatAsIso8601(tracking_started_at);
    sql::Statement insert(db_->GetCachedStatement(
        SQL_FROM_HERE, "INSERT INTO metadata(key, value) VALUES (?, ?)"));
    insert.BindString(0, kTrackingStartedAtKey);
    insert.BindString(1, stored_timestamp);
    if (!insert.Run()) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
  }

  base::Time stored_tracking_started_at;
  if (!base::Time::FromUTCString(stored_timestamp.c_str(),
                                 &stored_tracking_started_at) ||
      base::TimeFormatAsIso8601(stored_tracking_started_at) !=
          stored_timestamp) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }

  const std::string initialization_date = FormatDate(tracking_started_at, true);
  const std::optional<std::string> retained_from =
      RetainedFromDate(initialization_date);
  if (!retained_from) {
    return base::unexpected(DaoForegroundActivityStoreError::kInvalidInput);
  }
  sql::Statement prune(db_->GetCachedStatement(
      SQL_FROM_HERE, "DELETE FROM foreground_activity WHERE local_date < ?"));
  prune.BindString(0, *retained_from);
  if (!prune.Run() || !transaction.Commit()) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }

  tracking_started_at_ = stored_tracking_started_at;
  last_pruned_date_ = initialization_date;
  DaoForegroundActivitySnapshot snapshot;
  snapshot.available = true;
  snapshot.tracking_started_at = tracking_started_at_;
  snapshot.retained_from_date = *retained_from;
  return snapshot;
}

base::expected<DaoForegroundActivitySnapshot, DaoForegroundActivityStoreError>
DaoForegroundActivityStore::ApplyDeltasAndQuery(
    const std::string& start_date,
    const std::string& end_date,
    const std::string& today,
    const std::vector<DaoForegroundActivityDelta>& deltas) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const std::optional<std::string> retained_from = RetainedFromDate(today);
  if (!db_ || tracking_started_at_.is_null() || !ParseLocalDate(start_date) ||
      !ParseLocalDate(end_date) || !retained_from || start_date > end_date) {
    return base::unexpected(DaoForegroundActivityStoreError::kInvalidInput);
  }
  bool has_expired_delta = false;
  for (const DaoForegroundActivityDelta& delta : deltas) {
    if (!IsValidDelta(delta)) {
      return base::unexpected(DaoForegroundActivityStoreError::kInvalidInput);
    }
    has_expired_delta |= delta.local_date < *retained_from;
  }

  sql::Transaction transaction(db_.get());
  if (!transaction.Begin()) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }

  sql::Statement read_current(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT foreground_ms FROM foreground_activity "
      "WHERE local_date = ? AND bucket = ? AND host = ?"));
  sql::Statement replace(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "INSERT OR REPLACE INTO foreground_activity "
      "(local_date, bucket, host, foreground_ms) VALUES (?, ?, ?, ?)"));
  for (const DaoForegroundActivityDelta& delta : deltas) {
    read_current.Reset(true);
    read_current.BindString(0, delta.local_date);
    read_current.BindInt(1, static_cast<int>(delta.bucket));
    read_current.BindString(2, delta.host);
    const bool found = read_current.Step();
    if (!found && !read_current.Succeeded()) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }

    int64_t current = 0;
    if (found) {
      if (read_current.GetColumnType(0) != sql::ColumnType::kInteger) {
        return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
      }
      current = read_current.ColumnInt64(0);
    }
    read_current.Reset(false);
    base::CheckedNumeric<int64_t> total(current);
    total += delta.foreground_ms;
    if (current < 0 || !total.IsValid() || total.ValueOrDie() < 0) {
      return base::unexpected(DaoForegroundActivityStoreError::kInvalidInput);
    }

    replace.Reset(true);
    replace.BindString(0, delta.local_date);
    replace.BindInt(1, static_cast<int>(delta.bucket));
    replace.BindString(2, delta.host);
    replace.BindInt64(3, total.ValueOrDie());
    if (!replace.Run()) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
  }

  if (today != last_pruned_date_ || has_expired_delta) {
    sql::Statement prune(db_->GetCachedStatement(
        SQL_FROM_HERE, "DELETE FROM foreground_activity WHERE local_date < ?"));
    prune.BindString(0, *retained_from);
    if (!prune.Run()) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
  }

  DaoForegroundActivitySnapshot snapshot;
  snapshot.available = true;
  snapshot.tracking_started_at = tracking_started_at_;
  snapshot.retained_from_date = *retained_from;
  sql::Statement query(db_->GetCachedStatement(
      SQL_FROM_HERE,
      "SELECT local_date, bucket, host, foreground_ms "
      "FROM foreground_activity WHERE local_date >= ? AND local_date <= ? "
      "ORDER BY local_date, bucket, host"));
  query.BindString(0, start_date);
  query.BindString(1, end_date);
  while (query.Step()) {
    if (query.GetColumnType(0) != sql::ColumnType::kText ||
        query.GetColumnType(1) != sql::ColumnType::kInteger ||
        query.GetColumnType(2) != sql::ColumnType::kText ||
        query.GetColumnType(3) != sql::ColumnType::kInteger) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
    DaoForegroundActivityRow row{
        query.ColumnString(0),
        static_cast<DaoForegroundActivityBucket>(query.ColumnInt(1)),
        query.ColumnString(2), query.ColumnInt64(3)};
    if (!IsValidDelta(row)) {
      return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
    }
    snapshot.rows.push_back(std::move(row));
  }
  if (!query.Succeeded() || !transaction.Commit()) {
    return base::unexpected(DaoForegroundActivityStoreError::kDatabase);
  }
  last_pruned_date_ = today;
  return snapshot;
}

}  // namespace dao
