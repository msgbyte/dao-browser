// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_FOREGROUND_POLICY_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_FOREGROUND_POLICY_H_

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/time/time.h"
#include "dao/browser/activity/dao_foreground_activity_store.h"

namespace dao {

enum class DreamForegroundSource {
  kDaoActiveTabV1,
  kChromiumHistoryLegacy,
  kMixed,
};

enum class DreamForegroundCoverage {
  kFull,
  kPartial,
  kLegacy,
  kMixed,
  kUnavailable,
};

struct DreamForegroundDatePolicy {
  DreamForegroundSource source = DreamForegroundSource::kDaoActiveTabV1;
  DreamForegroundCoverage coverage = DreamForegroundCoverage::kUnavailable;
  int64_t coverage_seconds = 0;
  bool use_native = false;
  bool use_legacy = false;
};

using DreamForegroundRangePolicy = DreamForegroundDatePolicy;

struct DreamForegroundActivitySummary {
  DreamForegroundActivitySummary();
  ~DreamForegroundActivitySummary();
  DreamForegroundActivitySummary(const DreamForegroundActivitySummary&);
  DreamForegroundActivitySummary& operator=(
      const DreamForegroundActivitySummary&);
  DreamForegroundActivitySummary(DreamForegroundActivitySummary&&) noexcept;
  DreamForegroundActivitySummary& operator=(
      DreamForegroundActivitySummary&&) noexcept;

  int64_t total_foreground_ms = 0;
  std::map<std::string, int64_t> foreground_ms_by_host;
  std::array<int64_t, 4> foreground_ms_by_bucket = {};
  std::map<std::string, std::array<int64_t, 4>>
      foreground_ms_by_host_and_bucket;
};

DreamForegroundDatePolicy ResolveDreamForegroundDatePolicy(
    const std::string& local_date,
    const DaoForegroundActivitySnapshot& snapshot,
    base::Time query_time);
DreamForegroundRangePolicy ResolveDreamForegroundRangePolicy(
    const std::string& start_local_date,
    const std::string& end_local_date,
    const DaoForegroundActivitySnapshot& snapshot,
    base::Time query_time);
DreamForegroundRangePolicy ResolveDreamForegroundWindowPolicy(
    base::Time window_start,
    base::Time window_end,
    const DaoForegroundActivitySnapshot& snapshot,
    base::Time query_time);
bool IsDreamForegroundRowInWindow(const DaoForegroundActivityRow& row,
                                  base::Time window_start,
                                  base::Time window_end,
                                  base::Time query_time);
std::optional<DreamForegroundActivitySummary> SummarizeDreamForegroundActivity(
    const std::vector<DaoForegroundActivityRow>& rows,
    const std::set<std::string>& excluded_domains);

const char* DreamForegroundSourceName(DreamForegroundSource source);
const char* DreamForegroundCoverageName(DreamForegroundCoverage coverage);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_FOREGROUND_POLICY_H_
