// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

namespace base {
class FilePath;
}

namespace dao {

class DaoAgentMemoryStore;

// Profile-keyed service that manages agent memory. All DB operations are
// posted to a background sequence. Callbacks are invoked on the calling
// (typically UI) thread.
class DaoAgentMemoryService : public KeyedService {
 public:
  explicit DaoAgentMemoryService(const base::FilePath& profile_path);
  ~DaoAgentMemoryService() override;

  DaoAgentMemoryService(const DaoAgentMemoryService&) = delete;
  DaoAgentMemoryService& operator=(const DaoAgentMemoryService&) = delete;

  // KeyedService:
  void Shutdown() override;

  // Conversation messages
  void SaveConversationMessages(
      const std::string& session_id,
      std::vector<ConversationMessage> messages,
      base::OnceCallback<void(bool)> callback);
  void LoadConversationMessages(
      const std::string& session_id,
      int limit,
      base::OnceCallback<void(std::vector<ConversationMessage>)> callback);
  void LoadRecentMessages(
      int limit,
      base::OnceCallback<void(std::vector<ConversationMessage>)> callback);
  void LoadConversationMessagesInRange(
      base::Time start,
      base::Time end,
      int limit,
      base::OnceCallback<void(std::vector<ConversationMessage>)> callback);

  // Conversation summaries
  void SaveConversationSummary(
      ConversationSummary summary,
      base::OnceCallback<void(bool)> callback);
  void LoadConversationSummaries(
      int limit,
      base::OnceCallback<void(std::vector<ConversationSummary>)> callback);
  void LoadConversationSummariesInRange(
      base::Time start,
      base::Time end,
      int limit,
      base::OnceCallback<void(std::vector<ConversationSummary>)> callback);
  void FindSummaryByDomain(
      const std::string& domain,
      base::OnceCallback<void(std::optional<ConversationSummary>)> callback);

  // Preferences
  void MergePreference(const std::string& key,
                       const std::string& value,
                       double confidence,
                       base::OnceCallback<void(bool)> callback);
  void GetPreferences(
      int limit,
      double min_confidence,
      base::OnceCallback<void(std::vector<Preference>)> callback);
  void DeletePreference(int64_t id, base::OnceCallback<void(bool)> callback);

  // Episodes
  void SaveEpisode(Episode episode, base::OnceCallback<void(bool)> callback);
  void GetEpisodesByDomain(
      const std::string& domain,
      int limit,
      base::OnceCallback<void(std::vector<Episode>)> callback);
  void SearchEpisodes(
      const std::string& query,
      const std::string& domain,
      int limit,
      base::OnceCallback<void(std::vector<Episode>)> callback);
  void UpdateEpisodeConfidence(int64_t id,
                               double confidence,
                               base::OnceCallback<void(bool)> callback);
  void DeleteEpisode(int64_t id, base::OnceCallback<void(bool)> callback);

  // Episodes (extended)
  void GetDomainEpisodeCountWithAction(
      const std::string& domain,
      base::OnceCallback<void(int)> callback);

  // Scenarios
  void SavePersonalScenario(ScenarioDefinition scenario,
                            base::OnceCallback<void(bool)> callback);
  void GetPersonalScenarios(
      base::OnceCallback<void(std::vector<ScenarioDefinition>)> callback);
  void DeleteScenario(const std::string& id,
                      base::OnceCallback<void(bool)> callback);
  void UpdateScenarioStats(const std::string& id,
                           const std::string& stat_column,
                           base::OnceCallback<void(bool)> callback);

  // Action feedback
  void RecordActionFeedback(ActionFeedback feedback,
                            base::OnceCallback<void(bool)> callback);
  void GetCooldownScore(const std::string& domain,
                        const std::string& scenario_id,
                        base::OnceCallback<void(double)> callback);
  void ClearDismissedFeedback(base::OnceCallback<void(bool)> callback);

  // Deletion
  void DeleteConversation(const std::string& session_id,
                          base::OnceCallback<void(bool)> callback);
  void ClearAll(base::OnceCallback<void(bool)> callback);

  // Stats
  void GetStorageStats(base::OnceCallback<void(StorageStats)> callback);

  // Debug SQL browser.
  void ExecuteReadOnlySqlForDebug(
      const std::string& sql,
      int max_rows,
      base::OnceCallback<void(MemorySqlQueryResult)> callback);

  // Dream reports
  void SaveDreamReport(DreamReport report,
                       base::OnceCallback<void(bool)> callback);
  void GetDreamReportByDate(
      const std::string& date,
      base::OnceCallback<void(std::optional<DreamReport>)> callback);
  void GetDreamReports(
      int limit,
      base::OnceCallback<void(std::vector<DreamReport>)> callback);
  void GetDreamReportsInDateRange(
      const std::string& start_date,
      const std::string& end_date,
      int limit,
      base::OnceCallback<void(std::vector<DreamReport>)> callback);
  void GetLatestDreamReport(
      base::OnceCallback<void(std::optional<DreamReport>)> callback);
  void GetLatestUnviewedDreamReport(
      base::OnceCallback<void(std::optional<DreamReport>)> callback);
  void MarkDreamReportViewed(int64_t id,
                             base::OnceCallback<void(bool)> callback);

  // Weekly Dream reports and their local source locators.
  void SaveWeeklyDreamReport(
      WeeklyDreamReport report,
      std::vector<WeeklyDreamSource> sources,
      base::OnceCallback<void(bool)> callback);
  void GetWeeklyDreamReportByWeekStart(
      const std::string& week_start,
      base::OnceCallback<void(std::optional<WeeklyDreamReport>)> callback);
  void GetWeeklyDreamReports(
      int limit,
      base::OnceCallback<void(std::vector<WeeklyDreamReport>)> callback);
  void GetLatestWeeklyDreamReportBefore(
      const std::string& week_start,
      base::OnceCallback<void(std::optional<WeeklyDreamReport>)> callback);
  void GetLatestUnviewedWeeklyDreamReport(
      base::OnceCallback<void(std::optional<WeeklyDreamReport>)> callback);
  void GetWeeklyDreamSources(
      int64_t report_id,
      base::OnceCallback<void(std::vector<WeeklyDreamSource>)> callback);
  void GetWeeklyDreamSource(
      int64_t report_id,
      const std::string& ref_id,
      base::OnceCallback<void(std::optional<WeeklyDreamSource>)> callback);
  void MarkWeeklyDreamReportViewed(int64_t id,
                                   base::OnceCallback<void(bool)> callback);
  void DeleteWeeklyDreamReport(int64_t id,
                               base::OnceCallback<void(bool)> callback);

  // Memory context (combined query for LLM injection)
  void GetMemoryContext(
      const std::string& url,
      const std::string& domain,
      const std::string& session_id,
      base::OnceCallback<void(MemoryContext)> callback);

 private:
  void InitOnBackgroundSequence();

  scoped_refptr<base::SequencedTaskRunner> background_task_runner_;
  // Owned by the background sequence — created on UI, used on background.
  std::unique_ptr<DaoAgentMemoryStore> store_;

  SEQUENCE_CHECKER(ui_sequence_checker_);
  base::WeakPtrFactory<DaoAgentMemoryService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_SERVICE_H_
