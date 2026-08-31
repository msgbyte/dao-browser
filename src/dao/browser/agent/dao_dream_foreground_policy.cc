// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_foreground_policy.h"

#include <algorithm>
#include <utility>

#include "base/notreached.h"
#include "base/numerics/safe_math.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"

namespace dao {

DreamForegroundActivitySummary::DreamForegroundActivitySummary() = default;
DreamForegroundActivitySummary::~DreamForegroundActivitySummary() = default;
DreamForegroundActivitySummary::DreamForegroundActivitySummary(
    const DreamForegroundActivitySummary&) = default;
DreamForegroundActivitySummary& DreamForegroundActivitySummary::operator=(
    const DreamForegroundActivitySummary&) = default;
DreamForegroundActivitySummary::DreamForegroundActivitySummary(
    DreamForegroundActivitySummary&&) noexcept = default;
DreamForegroundActivitySummary& DreamForegroundActivitySummary::operator=(
    DreamForegroundActivitySummary&&) noexcept = default;

namespace {

bool ParseLocalDate(const std::string& value, base::Time* result) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
    return false;
  }
  base::Time::Exploded exploded = {};
  if (!base::StringToInt(value.substr(0, 4), &exploded.year) ||
      !base::StringToInt(value.substr(5, 2), &exploded.month) ||
      !base::StringToInt(value.substr(8, 2), &exploded.day_of_month) ||
      !base::Time::FromLocalExploded(exploded, result)) {
    return false;
  }
  base::Time::Exploded round_trip;
  result->LocalExplode(&round_trip);
  return round_trip.year == exploded.year &&
         round_trip.month == exploded.month &&
         round_trip.day_of_month == exploded.day_of_month;
}

std::string LocalDate(base::Time time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02d", exploded.year, exploded.month,
                            exploded.day_of_month);
}

base::Time NextLocalDay(base::Time day) {
  base::Time::Exploded exploded;
  day.LocalExplode(&exploded);
  base::Time calendar;
  if (!base::Time::FromUTCExploded(exploded, &calendar)) {
    return base::Time();
  }
  calendar += base::Days(1);
  calendar.UTCExplode(&exploded);
  base::Time next;
  return base::Time::FromLocalExploded(exploded, &next) ? next : base::Time();
}

std::optional<std::pair<base::Time, base::Time>> RowInterval(
    const DaoForegroundActivityRow& row) {
  base::Time day;
  if (!ParseLocalDate(row.local_date, &day)) {
    return std::nullopt;
  }
  int start_hour = 0;
  int end_hour = 0;
  switch (row.bucket) {
    case DaoForegroundActivityBucket::kNightBeforeMorning:
      end_hour = 6;
      break;
    case DaoForegroundActivityBucket::kMorning:
      start_hour = 6;
      end_hour = 12;
      break;
    case DaoForegroundActivityBucket::kAfternoon:
      start_hour = 12;
      end_hour = 18;
      break;
    case DaoForegroundActivityBucket::kEvening:
      start_hour = 18;
      end_hour = 22;
      break;
    case DaoForegroundActivityBucket::kNightAfterEvening:
      start_hour = 22;
      break;
    default:
      return std::nullopt;
  }
  auto local_hour = [day](int hour) {
    base::Time::Exploded exploded;
    day.LocalExplode(&exploded);
    exploded.hour = hour;
    base::Time result;
    return base::Time::FromLocalExploded(exploded, &result) ? result
                                                            : base::Time();
  };
  base::Time start = local_hour(start_hour);
  base::Time end = end_hour ? local_hour(end_hour) : NextLocalDay(day);
  if (start.is_null() || end.is_null()) {
    return std::nullopt;
  }
  return std::pair(start, end);
}

std::optional<size_t> DreamBucketIndex(DaoForegroundActivityBucket bucket) {
  switch (bucket) {
    case DaoForegroundActivityBucket::kMorning:
      return 0;
    case DaoForegroundActivityBucket::kAfternoon:
      return 1;
    case DaoForegroundActivityBucket::kEvening:
      return 2;
    case DaoForegroundActivityBucket::kNightBeforeMorning:
    case DaoForegroundActivityBucket::kNightAfterEvening:
      return 3;
    default:
      return std::nullopt;
  }
}

}  // namespace

DreamForegroundDatePolicy ResolveDreamForegroundDatePolicy(
    const std::string& local_date,
    const DaoForegroundActivitySnapshot& snapshot,
    base::Time query_time) {
  DreamForegroundDatePolicy result;
  base::Time day_start;
  if (!ParseLocalDate(local_date, &day_start) || query_time.is_null() ||
      snapshot.tracking_started_at.is_null()) {
    return result;
  }

  const std::string tracking_date = LocalDate(snapshot.tracking_started_at);
  if (local_date < tracking_date) {
    result.source = DreamForegroundSource::kChromiumHistoryLegacy;
    result.coverage = DreamForegroundCoverage::kLegacy;
    result.use_legacy = true;
    return result;
  }

  if (!snapshot.available || local_date < snapshot.retained_from_date ||
      local_date > LocalDate(query_time)) {
    return result;
  }

  result.use_native = true;
  const base::Time day_end = NextLocalDay(day_start);
  if (day_end.is_null()) {
    return DreamForegroundDatePolicy();
  }
  const base::Time covered_start =
      std::max(day_start, snapshot.tracking_started_at);
  const base::Time covered_end = std::min(day_end, query_time);
  if (covered_end > covered_start) {
    result.coverage_seconds = (covered_end - covered_start).InSeconds();
  }
  if (local_date == tracking_date || local_date == LocalDate(query_time)) {
    result.coverage = DreamForegroundCoverage::kPartial;
  } else {
    result.coverage = DreamForegroundCoverage::kFull;
  }
  return result;
}

DreamForegroundRangePolicy ResolveDreamForegroundRangePolicy(
    const std::string& start_local_date,
    const std::string& end_local_date,
    const DaoForegroundActivitySnapshot& snapshot,
    base::Time query_time) {
  DreamForegroundRangePolicy result;
  base::Time day;
  base::Time end;
  if (!ParseLocalDate(start_local_date, &day) ||
      !ParseLocalDate(end_local_date, &end) || day > end) {
    return result;
  }

  bool first = true;
  bool saw_unavailable = false;
  for (;;) {
    const DreamForegroundDatePolicy date =
        ResolveDreamForegroundDatePolicy(LocalDate(day), snapshot, query_time);
    saw_unavailable |= date.coverage == DreamForegroundCoverage::kUnavailable;
    base::CheckedNumeric<int64_t> coverage(result.coverage_seconds);
    coverage += date.coverage_seconds;
    if (!coverage.AssignIfValid(&result.coverage_seconds)) {
      return DreamForegroundRangePolicy();
    }
    result.use_native |= date.use_native;
    result.use_legacy |= date.use_legacy;
    if (first) {
      result.source = date.source;
      result.coverage = date.coverage;
      first = false;
    } else {
      if (result.source != date.source) {
        result.source = DreamForegroundSource::kMixed;
      }
      if (result.coverage != date.coverage) {
        result.coverage = DreamForegroundCoverage::kMixed;
      }
    }
    if (day == end) {
      break;
    }
    day = NextLocalDay(day);
    if (day.is_null() || day > end) {
      break;
    }
  }
  if (saw_unavailable) {
    result.coverage = DreamForegroundCoverage::kUnavailable;
    result.coverage_seconds = 0;
  }
  return result;
}

DreamForegroundRangePolicy ResolveDreamForegroundWindowPolicy(
    base::Time window_start,
    base::Time window_end,
    const DaoForegroundActivitySnapshot& snapshot,
    base::Time query_time) {
  if (window_start.is_null() || window_end <= window_start ||
      query_time.is_null()) {
    return DreamForegroundRangePolicy();
  }
  DreamForegroundRangePolicy result = ResolveDreamForegroundRangePolicy(
      LocalDate(window_start), LocalDate(window_end - base::Microseconds(1)),
      snapshot, query_time);
  if (result.coverage == DreamForegroundCoverage::kUnavailable ||
      snapshot.tracking_started_at.is_null()) {
    result.coverage_seconds = 0;
    return result;
  }
  const base::Time covered_start =
      std::max(window_start, snapshot.tracking_started_at);
  const base::Time covered_end = std::min(window_end, query_time);
  result.coverage_seconds = covered_end > covered_start
                                ? (covered_end - covered_start).InSeconds()
                                : 0;
  if (result.source == DreamForegroundSource::kDaoActiveTabV1) {
    result.coverage = covered_start == window_start && covered_end == window_end
                          ? DreamForegroundCoverage::kFull
                          : DreamForegroundCoverage::kPartial;
  }
  return result;
}

bool IsDreamForegroundRowInWindow(const DaoForegroundActivityRow& row,
                                  base::Time window_start,
                                  base::Time window_end,
                                  base::Time query_time) {
  const std::optional<std::pair<base::Time, base::Time>> interval =
      RowInterval(row);
  const base::Time effective_end = std::min(window_end, query_time);
  return interval && window_start < effective_end &&
         interval->first < effective_end && interval->second > window_start;
}

std::optional<DreamForegroundActivitySummary> SummarizeDreamForegroundActivity(
    const std::vector<DaoForegroundActivityRow>& rows,
    const std::set<std::string>& excluded_domains) {
  DreamForegroundActivitySummary result;
  for (const auto& row : rows) {
    const std::optional<size_t> bucket = DreamBucketIndex(row.bucket);
    if (row.host.empty() || row.foreground_ms < 0 || !bucket) {
      return std::nullopt;
    }
    if (IsDreamDomainExcluded(row.host, excluded_domains)) {
      continue;
    }
    base::CheckedNumeric<int64_t> total(result.total_foreground_ms);
    total += row.foreground_ms;
    base::CheckedNumeric<int64_t> host(result.foreground_ms_by_host[row.host]);
    host += row.foreground_ms;
    base::CheckedNumeric<int64_t> global_bucket(
        result.foreground_ms_by_bucket[*bucket]);
    global_bucket += row.foreground_ms;
    base::CheckedNumeric<int64_t> host_bucket(
        result.foreground_ms_by_host_and_bucket[row.host][*bucket]);
    host_bucket += row.foreground_ms;
    if (!total.AssignIfValid(&result.total_foreground_ms) ||
        !host.AssignIfValid(&result.foreground_ms_by_host[row.host]) ||
        !global_bucket.AssignIfValid(
            &result.foreground_ms_by_bucket[*bucket]) ||
        !host_bucket.AssignIfValid(
            &result.foreground_ms_by_host_and_bucket[row.host][*bucket])) {
      return std::nullopt;
    }
  }
  return result;
}

const char* DreamForegroundSourceName(DreamForegroundSource source) {
  switch (source) {
    case DreamForegroundSource::kDaoActiveTabV1:
      return "dao_active_tab_v1";
    case DreamForegroundSource::kChromiumHistoryLegacy:
      return "chromium_history_legacy";
    case DreamForegroundSource::kMixed:
      return "mixed";
  }
  NOTREACHED();
}

const char* DreamForegroundCoverageName(DreamForegroundCoverage coverage) {
  switch (coverage) {
    case DreamForegroundCoverage::kFull:
      return "full";
    case DreamForegroundCoverage::kPartial:
      return "partial";
    case DreamForegroundCoverage::kLegacy:
      return "legacy";
    case DreamForegroundCoverage::kMixed:
      return "mixed";
    case DreamForegroundCoverage::kUnavailable:
      return "unavailable";
  }
  NOTREACHED();
}

}  // namespace dao
