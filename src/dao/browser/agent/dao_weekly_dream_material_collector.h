// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_WEEKLY_DREAM_MATERIAL_COLLECTOR_H_
#define DAO_BROWSER_AGENT_DAO_WEEKLY_DREAM_MATERIAL_COLLECTOR_H_

#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/history/core/browser/history_types.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

class Profile;

namespace dao {

class DaoAgentMemoryService;

// The model-safe weekly material and its separate native-only source map.
struct WeeklyDreamMaterial {
  WeeklyDreamMaterial();
  WeeklyDreamMaterial(WeeklyDreamMaterial&&);
  WeeklyDreamMaterial& operator=(WeeklyDreamMaterial&&);
  ~WeeklyDreamMaterial();

  base::DictValue model_material;
  std::vector<WeeklyDreamSource> local_sources;
};

// Collects bounded weekly evidence without placing page URLs or conversation
// session IDs in model_material. Only one collection may be in flight.
class WeeklyDreamMaterialCollector {
 public:
  using CollectCallback = base::OnceCallback<void(WeeklyDreamMaterial)>;

  static constexpr int kMaxDomains = 50;
  static constexpr int kMaxPageSources = 24;
  static constexpr int kMaxConversationSources = 10;
  static constexpr int kMaxFallbackMessages = 20;
  static constexpr int kMaxDailyReports = 7;
  static constexpr int kMaxTitlesPerDomain = 5;
  static constexpr int kMaxTextChars = 240;

  WeeklyDreamMaterialCollector(Profile* profile,
                               DaoAgentMemoryService* memory_service);
  ~WeeklyDreamMaterialCollector();

  WeeklyDreamMaterialCollector(const WeeklyDreamMaterialCollector&) = delete;
  WeeklyDreamMaterialCollector& operator=(
      const WeeklyDreamMaterialCollector&) = delete;

  void Collect(base::Time window_start,
               base::Time window_end,
               const std::string& week_start,
               const std::string& week_end,
               CollectCallback callback);

 private:
  void OnHistoryLoaded(std::vector<history::AnnotatedVisit> visits);
  void OnConversationSummariesLoaded(
      std::vector<ConversationSummary> summaries);
  void OnConversationMessagesLoaded(
      std::vector<ConversationMessage> messages);
  void OnDailyReportsLoaded(std::vector<DreamReport> reports);
  void OnPreviousWeeklyReportLoaded(
      std::optional<WeeklyDreamReport> report);
  void OnAllPartsLoaded();

  raw_ptr<Profile> profile_;
  raw_ptr<DaoAgentMemoryService> memory_service_;

  base::Time window_start_;
  base::Time window_end_;
  std::string week_start_;
  std::string week_end_;
  CollectCallback callback_;
  base::RepeatingClosure barrier_;

  std::vector<history::AnnotatedVisit> history_visits_;
  std::vector<ConversationSummary> conversation_summaries_;
  std::vector<ConversationMessage> conversation_messages_;
  std::vector<DreamReport> daily_reports_;
  std::optional<WeeklyDreamReport> previous_weekly_report_;

  base::CancelableTaskTracker history_tracker_;
  base::WeakPtrFactory<WeeklyDreamMaterialCollector> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_WEEKLY_DREAM_MATERIAL_COLLECTOR_H_
