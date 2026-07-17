// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_service.h"

#include "base/files/file_path.h"
#include "base/logging.h"
#include "base/task/thread_pool.h"
#include "dao/browser/agent/dao_agent_memory_store.h"

namespace dao {

namespace {

constexpr char kDbFileName[] = "DaoAgentMemory.db";

}  // namespace

DaoAgentMemoryService::DaoAgentMemoryService(
    const base::FilePath& profile_path)
    : background_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})),
      store_(std::make_unique<DaoAgentMemoryStore>(
          profile_path.AppendASCII(kDbFileName))) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);

  background_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoAgentMemoryService::InitOnBackgroundSequence,
                     base::Unretained(this)));
}

DaoAgentMemoryService::~DaoAgentMemoryService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  // DaoAgentMemoryStore must be destroyed on the background sequence.
  background_task_runner_->DeleteSoon(FROM_HERE, std::move(store_));
}

void DaoAgentMemoryService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
}

void DaoAgentMemoryService::InitOnBackgroundSequence() {
  if (!store_->Init()) {
    LOG(ERROR) << "Failed to initialize DaoAgentMemoryStore";
  }
}

// --- Conversation Messages ---

void DaoAgentMemoryService::SaveConversationMessages(
    const std::string& session_id,
    std::vector<ConversationMessage> messages,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string sid,
             std::vector<ConversationMessage> msgs) {
            return store->SaveConversationMessages(sid, msgs);
          },
          store_.get(), session_id, std::move(messages)),
      std::move(callback));
}

void DaoAgentMemoryService::LoadConversationMessages(
    const std::string& session_id,
    int limit,
    base::OnceCallback<void(std::vector<ConversationMessage>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string sid, int lim) {
            return store->LoadConversationMessages(sid, lim);
          },
          store_.get(), session_id, limit),
      std::move(callback));
}

void DaoAgentMemoryService::LoadRecentMessages(
    int limit,
    base::OnceCallback<void(std::vector<ConversationMessage>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int lim) {
            return store->LoadRecentMessages(lim);
          },
          store_.get(), limit),
      std::move(callback));
}

void DaoAgentMemoryService::LoadConversationMessagesInRange(
    base::Time start,
    base::Time end,
    int limit,
    base::OnceCallback<void(std::vector<ConversationMessage>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, base::Time start_time,
             base::Time end_time, int lim) {
            return store->LoadConversationMessagesInRange(start_time, end_time,
                                                          lim);
          },
          store_.get(), start, end, limit),
      std::move(callback));
}

// --- Conversation Summaries ---

void DaoAgentMemoryService::SaveConversationSummary(
    ConversationSummary summary,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, ConversationSummary s) {
            return store->SaveConversationSummary(s);
          },
          store_.get(), std::move(summary)),
      std::move(callback));
}

void DaoAgentMemoryService::LoadConversationSummaries(
    int limit,
    base::OnceCallback<void(std::vector<ConversationSummary>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int lim) {
            return store->LoadConversationSummaries(lim);
          },
          store_.get(), limit),
      std::move(callback));
}

void DaoAgentMemoryService::LoadConversationSummariesInRange(
    base::Time start,
    base::Time end,
    int limit,
    base::OnceCallback<void(std::vector<ConversationSummary>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, base::Time start_time,
             base::Time end_time, int lim) {
            return store->LoadConversationSummariesInRange(start_time,
                                                           end_time, lim);
          },
          store_.get(), start, end, limit),
      std::move(callback));
}

void DaoAgentMemoryService::FindSummaryByDomain(
    const std::string& domain,
    base::OnceCallback<void(std::optional<ConversationSummary>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string dom) {
            return store->FindSummaryByDomain(dom);
          },
          store_.get(), domain),
      std::move(callback));
}

// --- Preferences ---

void DaoAgentMemoryService::MergePreference(
    const std::string& key,
    const std::string& value,
    double confidence,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string k, std::string v,
             double c) { return store->MergePreference(k, v, c); },
          store_.get(), key, value, confidence),
      std::move(callback));
}

void DaoAgentMemoryService::GetPreferences(
    int limit,
    double min_confidence,
    base::OnceCallback<void(std::vector<Preference>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int lim, double min_c) {
            return store->GetPreferences(lim, min_c);
          },
          store_.get(), limit, min_confidence),
      std::move(callback));
}

void DaoAgentMemoryService::DeletePreference(
    int64_t id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t i) {
            return store->DeletePreference(i);
          },
          store_.get(), id),
      std::move(callback));
}

// --- Episodes ---

void DaoAgentMemoryService::SaveEpisode(
    Episode episode,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, Episode e) {
            return store->SaveEpisode(e);
          },
          store_.get(), std::move(episode)),
      std::move(callback));
}

void DaoAgentMemoryService::GetEpisodesByDomain(
    const std::string& domain,
    int limit,
    base::OnceCallback<void(std::vector<Episode>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string dom, int lim) {
            return store->GetEpisodesByDomain(dom, lim);
          },
          store_.get(), domain, limit),
      std::move(callback));
}

void DaoAgentMemoryService::SearchEpisodes(
    const std::string& query,
    const std::string& domain,
    int limit,
    base::OnceCallback<void(std::vector<Episode>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string q, std::string dom,
             int lim) { return store->SearchEpisodes(q, dom, lim); },
          store_.get(), query, domain, limit),
      std::move(callback));
}

void DaoAgentMemoryService::UpdateEpisodeConfidence(
    int64_t id,
    double confidence,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t i, double c) {
            return store->UpdateEpisodeConfidence(i, c);
          },
          store_.get(), id, confidence),
      std::move(callback));
}

void DaoAgentMemoryService::DeleteEpisode(
    int64_t id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t i) {
            return store->DeleteEpisode(i);
          },
          store_.get(), id),
      std::move(callback));
}

// --- Episodes (extended) ---

void DaoAgentMemoryService::GetDomainEpisodeCountWithAction(
    const std::string& domain,
    base::OnceCallback<void(int)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string dom) {
            return store->GetDomainEpisodeCountWithAction(dom);
          },
          store_.get(), domain),
      std::move(callback));
}

// --- Scenarios ---

void DaoAgentMemoryService::SavePersonalScenario(
    ScenarioDefinition scenario,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, ScenarioDefinition s) {
            return store->SavePersonalScenario(s);
          },
          store_.get(), std::move(scenario)),
      std::move(callback));
}

void DaoAgentMemoryService::GetPersonalScenarios(
    base::OnceCallback<void(std::vector<ScenarioDefinition>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) {
            return store->GetPersonalScenarios();
          },
          store_.get()),
      std::move(callback));
}

void DaoAgentMemoryService::DeleteScenario(
    const std::string& id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string i) {
            return store->DeleteScenario(i);
          },
          store_.get(), id),
      std::move(callback));
}

void DaoAgentMemoryService::UpdateScenarioStats(
    const std::string& id,
    const std::string& stat_column,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string i, std::string col) {
            return store->UpdateScenarioStats(i, col);
          },
          store_.get(), id, stat_column),
      std::move(callback));
}

// --- Action Feedback ---

void DaoAgentMemoryService::RecordActionFeedback(
    ActionFeedback feedback,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, ActionFeedback f) {
            return store->RecordActionFeedback(f);
          },
          store_.get(), std::move(feedback)),
      std::move(callback));
}

void DaoAgentMemoryService::GetCooldownScore(
    const std::string& domain,
    const std::string& scenario_id,
    base::OnceCallback<void(double)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string dom, std::string sid) {
            return store->GetCooldownScore(dom, sid);
          },
          store_.get(), domain, scenario_id),
      std::move(callback));
}

void DaoAgentMemoryService::ClearDismissedFeedback(
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) {
            return store->ClearDismissedFeedback();
          },
          store_.get()),
      std::move(callback));
}

// --- Deletion ---

void DaoAgentMemoryService::DeleteConversation(
    const std::string& session_id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string sid) {
            return store->DeleteConversation(sid);
          },
          store_.get(), session_id),
      std::move(callback));
}

void DaoAgentMemoryService::ClearAll(
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) { return store->ClearAll(); },
          store_.get()),
      std::move(callback));
}

// --- Stats ---

void DaoAgentMemoryService::GetStorageStats(
    base::OnceCallback<void(StorageStats)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) { return store->GetStorageStats(); },
          store_.get()),
      std::move(callback));
}

void DaoAgentMemoryService::ExecuteReadOnlySqlForDebug(
    const std::string& sql,
    int max_rows,
    base::OnceCallback<void(MemorySqlQueryResult)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string query, int limit) {
            return store->ExecuteReadOnlySqlForDebug(query, limit);
          },
          store_.get(), sql, max_rows),
      std::move(callback));
}

// --- Memory Context ---

void DaoAgentMemoryService::GetMemoryContext(
    const std::string&,
    const std::string& domain,
    const std::string&,
    base::OnceCallback<void(MemoryContext)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string dom) {
            MemoryContext ctx;
            // Top 5 preferences with confidence >= 0.6
            ctx.preferences = store->GetPreferences(5, 0.6);
            // Up to 3 domain episodes
            ctx.episodes = store->GetEpisodesByDomain(dom, 3);
            // Relevant summary by domain
            ctx.relevant_summary = store->FindSummaryByDomain(dom);
            return ctx;
          },
          store_.get(), domain),
      std::move(callback));
}

// --- Dream Reports ---

void DaoAgentMemoryService::SaveDreamReport(
    DreamReport report,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, DreamReport r) {
            return store->SaveDreamReport(r);
          },
          store_.get(), std::move(report)),
      std::move(callback));
}

void DaoAgentMemoryService::GetDreamReportByDate(
    const std::string& date,
    base::OnceCallback<void(std::optional<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string d) {
            return store->GetDreamReportByDate(d);
          },
          store_.get(), date),
      std::move(callback));
}

void DaoAgentMemoryService::GetDreamReports(
    int limit,
    base::OnceCallback<void(std::vector<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int lim) {
            return store->GetDreamReports(lim);
          },
          store_.get(), limit),
      std::move(callback));
}

void DaoAgentMemoryService::GetDreamReportsInDateRange(
    const std::string& start_date,
    const std::string& end_date,
    int limit,
    base::OnceCallback<void(std::vector<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string start, std::string end,
             int lim) {
            return store->GetDreamReportsInDateRange(start, end, lim);
          },
          store_.get(), start_date, end_date, limit),
      std::move(callback));
}

void DaoAgentMemoryService::GetLatestDreamReport(
    base::OnceCallback<void(std::optional<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) {
            return store->GetLatestDreamReport();
          },
          store_.get()),
      std::move(callback));
}

void DaoAgentMemoryService::GetLatestUnviewedDreamReport(
    base::OnceCallback<void(std::optional<DreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) {
            return store->GetLatestUnviewedDreamReport();
          },
          store_.get()),
      std::move(callback));
}

void DaoAgentMemoryService::MarkDreamReportViewed(
    int64_t id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t report_id) {
            return store->MarkDreamReportViewed(report_id);
          },
          store_.get(), id),
      std::move(callback));
}

// --- Weekly Dream Reports ---

void DaoAgentMemoryService::SaveWeeklyDreamReport(
    WeeklyDreamReport report,
    std::vector<WeeklyDreamSource> sources,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, WeeklyDreamReport weekly_report,
             std::vector<WeeklyDreamSource> weekly_sources) {
            return store->SaveWeeklyDreamReport(weekly_report, weekly_sources);
          },
          store_.get(), std::move(report), std::move(sources)),
      std::move(callback));
}

void DaoAgentMemoryService::GetWeeklyDreamReportByWeekStart(
    const std::string& week_start,
    base::OnceCallback<void(std::optional<WeeklyDreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string start) {
            return store->GetWeeklyDreamReportByWeekStart(start);
          },
          store_.get(), week_start),
      std::move(callback));
}

void DaoAgentMemoryService::GetWeeklyDreamReports(
    int limit,
    base::OnceCallback<void(std::vector<WeeklyDreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int lim) {
            return store->GetWeeklyDreamReports(lim);
          },
          store_.get(), limit),
      std::move(callback));
}

void DaoAgentMemoryService::GetLatestWeeklyDreamReportBefore(
    const std::string& week_start,
    base::OnceCallback<void(std::optional<WeeklyDreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, std::string start) {
            return store->GetLatestWeeklyDreamReportBefore(start);
          },
          store_.get(), week_start),
      std::move(callback));
}

void DaoAgentMemoryService::GetLatestUnviewedWeeklyDreamReport(
    base::OnceCallback<void(std::optional<WeeklyDreamReport>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store) {
            return store->GetLatestUnviewedWeeklyDreamReport();
          },
          store_.get()),
      std::move(callback));
}

void DaoAgentMemoryService::GetWeeklyDreamSources(
    int64_t report_id,
    base::OnceCallback<void(std::vector<WeeklyDreamSource>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t id) {
            return store->GetWeeklyDreamSources(id);
          },
          store_.get(), report_id),
      std::move(callback));
}

void DaoAgentMemoryService::GetWeeklyDreamSource(
    int64_t report_id,
    const std::string& ref_id,
    base::OnceCallback<void(std::optional<WeeklyDreamSource>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t id, std::string ref) {
            return store->GetWeeklyDreamSource(id, ref);
          },
          store_.get(), report_id, ref_id),
      std::move(callback));
}

void DaoAgentMemoryService::MarkWeeklyDreamReportViewed(
    int64_t id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t report_id) {
            return store->MarkWeeklyDreamReportViewed(report_id);
          },
          store_.get(), id),
      std::move(callback));
}

void DaoAgentMemoryService::DeleteWeeklyDreamReport(
    int64_t id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](DaoAgentMemoryStore* store, int64_t report_id) {
            return store->DeleteWeeklyDreamReport(report_id);
          },
          store_.get(), id),
      std::move(callback));
}

}  // namespace dao
