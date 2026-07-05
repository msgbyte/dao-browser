// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_service.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_dream_material_collector.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/base/idle/idle.h"

namespace dao {

namespace {

std::string FormatYmd(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  return base::StringPrintf("%04d-%02d-%02d", e.year, e.month,
                            e.day_of_month);
}

// Parses "YYYY-MM-DD" into local midnight. Returns false on bad input.
bool ParseYmd(const std::string& ymd, base::Time* out) {
  std::vector<std::string_view> parts =
      base::SplitStringPiece(ymd, "-", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  int y = 0, m = 0, d = 0;
  if (parts.size() != 3 || !base::StringToInt(parts[0], &y) ||
      !base::StringToInt(parts[1], &m) ||
      !base::StringToInt(parts[2], &d)) {
    return false;
  }
  base::Time::Exploded e = {};
  e.year = y;
  e.month = m;
  e.day_of_month = d;
  return base::Time::FromLocalExploded(e, out);
}

bool IsFutureYmd(const std::string& ymd, base::Time now) {
  base::Time date_midnight;
  base::Time today_midnight;
  if (!ParseYmd(ymd, &date_midnight) ||
      !ParseYmd(FormatYmd(now), &today_midnight)) {
    return true;
  }
  return date_midnight > today_midnight;
}

}  // namespace

DaoDreamService::DaoDreamService(Profile* profile,
                                 DaoAgentMemoryService* memory_service)
    : profile_(profile), memory_service_(memory_service) {
  collector_ =
      std::make_unique<DreamMaterialCollector>(profile, memory_service);
  base::PowerMonitor::GetInstance()->AddPowerSuspendObserver(this);
  tick_timer_.Start(FROM_HERE, kTickInterval,
                    base::BindRepeating(&DaoDreamService::OnSchedulerTick,
                                        weak_factory_.GetWeakPtr()));
}

DaoDreamService::~DaoDreamService() {
  base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
}

void DaoDreamService::Shutdown() {
  base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
  tick_timer_.Stop();
  dream_timeout_timer_.Stop();
  runner_ = nullptr;
  weak_factory_.InvalidateWeakPtrs();
}

void DaoDreamService::SetRunner(Runner* runner) {
  runner_ = runner;
  MaybeStartCatchUpForRecentActivity();
}

void DaoDreamService::ClearRunner(Runner* runner) {
  if (runner_ == runner) {
    runner_ = nullptr;
  }
}

void DaoDreamService::OnResume() {
  MaybeStartCatchUpForRecentActivity();
}

// static
std::string DaoDreamService::DreamDateFor(base::Time now) {
  base::Time::Exploded e;
  now.LocalExplode(&e);
  if (e.hour < 6) {
    return FormatYmd(now - base::Days(1));
  }
  return FormatYmd(now);
}

// static
void DaoDreamService::MaterialWindowFor(const std::string& dream_date,
                                        base::Time now,
                                        base::Time* start,
                                        base::Time* end) {
  base::Time midnight;
  if (!ParseYmd(dream_date, &midnight)) {
    *start = now - base::Hours(24);
    *end = now;
    return;
  }
  *start = midnight + base::Hours(6);
  const base::Time hard_end = midnight + base::Hours(30);  // next day 06:00
  *end = std::min(hard_end, now);
}

// static
bool DaoDreamService::IsNightTime(base::Time now) {
  base::Time::Exploded e;
  now.LocalExplode(&e);
  return e.hour >= 22 || e.hour < 6;
}

int DaoDreamService::GetIdleSeconds() const {
  if (idle_time_callback_) {
    return idle_time_callback_.Run();
  }
  return ui::CalculateIdleTime();
}

bool DaoDreamService::DreamPrefEnabled() const {
  PrefService* pref_service = profile_->GetPrefs();
  return pref_service->GetBoolean(prefs::kDaoAgentMemoryEnabled) &&
         pref_service->GetBoolean(prefs::kDaoDreamEnabled);
}

void DaoDreamService::OnSchedulerTick() {
  if (!DreamPrefEnabled() || state_ != State::kIdle || !runner_) {
    return;
  }
  const base::Time now = clock_->Now();
  if (IsNightTime(now) && GetIdleSeconds() >= kNightlyIdleSeconds) {
    MaybeStartNightly();
    return;
  }
  if (GetIdleSeconds() >= kCatchUpIdleSeconds) {
    MaybeStartCatchUp();
  }
}

void DaoDreamService::MaybeStartNightly() {
  const std::string date = DreamDateFor(clock_->Now());
  memory_service_->GetDreamReportByDate(
      date, base::BindOnce(&DaoDreamService::OnExistingReportChecked,
                           weak_factory_.GetWeakPtr(), date,
                           TriggerKind::kNightly));
}

void DaoDreamService::MaybeStartCatchUp() {
  const std::string yesterday = FormatYmd(clock_->Now() - base::Days(1));
  memory_service_->GetDreamReportByDate(
      yesterday, base::BindOnce(&DaoDreamService::OnExistingReportChecked,
                                weak_factory_.GetWeakPtr(), yesterday,
                                TriggerKind::kCatchUp));
}

void DaoDreamService::MaybeStartCatchUpForRecentActivity() {
  if (!DreamPrefEnabled() || state_ != State::kIdle || !runner_) {
    return;
  }
  if (IsNightTime(clock_->Now())) {
    return;
  }
  MaybeStartCatchUp();
}

void DaoDreamService::OnExistingReportChecked(
    const std::string& dream_date,
    TriggerKind kind,
    std::optional<DreamReport> existing) {
  if (state_ != State::kIdle) {
    return;  // another path won the race
  }
  if (existing) {
    if (existing->status == "completed") {
      return;  // already dreamed
    }
    if (existing->attempt_count >= kMaxAttemptsPerNight) {
      return;  // give up for this date
    }
  }
  StartDream(dream_date, kind);
}

void DaoDreamService::StartDream(const std::string& dream_date,
                                 TriggerKind kind) {
  state_ = State::kCollecting;
  active_dream_date_ = dream_date;
  active_kind_ = kind;

  base::Time start;
  base::Time end;
  MaterialWindowFor(dream_date, clock_->Now(), &start, &end);
  collector_->Collect(
      start, end,
      base::BindOnce(&DaoDreamService::OnMaterialCollected,
                     weak_factory_.GetWeakPtr(), dream_date, kind));
}

void DaoDreamService::OnMaterialCollected(const std::string& dream_date,
                                          TriggerKind kind,
                                          base::DictValue material) {
  // Empty material → silent skip (not a failure).
  const base::ListValue* history = material.FindList("history");
  const base::ListValue* convs = material.FindList("conversations");
  if ((!history || history->empty()) && (!convs || convs->empty())) {
    FinishRun(false, "no dream material collected");
    return;
  }
  if (!runner_) {
    MarkFailed(dream_date, "agent webui unavailable");
    return;
  }

  // Persist debug input if debug mode is on.
  pending_debug_material_json_.clear();
  if (profile_->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug)) {
    base::JSONWriter::WriteWithOptions(
        material, base::JSONWriter::OPTIONS_PRETTY_PRINT,
        &pending_debug_material_json_);
  }
  if (const base::DictValue* stats = material.FindDict("stats")) {
    pending_material_stats_ = stats->Clone();
  }

  state_ = State::kDreaming;
  dream_timeout_timer_.Start(
      FROM_HERE, kDreamTimeout,
      base::BindOnce(&DaoDreamService::OnDreamTimeout,
                     weak_factory_.GetWeakPtr(), dream_date));
  runner_->RunDream(dream_date, material);
}

void DaoDreamService::OnDreamTimeout(const std::string& dream_date) {
  if (state_ != State::kDreaming || dream_date != active_dream_date_) {
    return;
  }
  MarkFailed(dream_date, "timeout");
}

void DaoDreamService::OnDreamResult(const std::string& dream_date,
                                    base::DictValue result) {
  if (state_ != State::kDreaming || dream_date != active_dream_date_) {
    return;  // stale result (timeout already fired, or unexpected)
  }
  dream_timeout_timer_.Stop();
  state_ = State::kSaving;
  PersistResult(dream_date, std::move(result));
}

void DaoDreamService::OnDreamFailed(const std::string& dream_date,
                                    const std::string& error) {
  if (state_ != State::kDreaming || dream_date != active_dream_date_) {
    return;
  }
  dream_timeout_timer_.Stop();
  MarkFailed(dream_date, error);
}

void DaoDreamService::MarkFailed(const std::string& dream_date,
                                 const std::string& error) {
  LOG(ERROR) << "Dream run failed for " << dream_date << ": " << error;
  // Read the existing attempt count, then write a failed row with +1.
  memory_service_->GetDreamReportByDate(
      dream_date,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, std::string date,
             std::string error,
             std::optional<DreamReport> existing) {
            if (!self) {
              return;
            }
            DreamReport report;
            if (existing) {
              report = std::move(*existing);
            }
            if (self->preserve_completed_report_on_failure_ &&
                report.status == "completed") {
              self->FinishRun(false, error);
              return;
            }
            report.dream_date = date;
            report.status = "failed";
            report.attempt_count += 1;
            report.trigger_kind =
                self->active_kind_ == TriggerKind::kManual ? "manual"
                : self->active_kind_ == TriggerKind::kCatchUp ? "catchup"
                                                              : "nightly";
            self->memory_service_->SaveDreamReport(
                std::move(report),
                base::BindOnce(
                    [](base::WeakPtr<DaoDreamService> self, std::string error,
                       bool ok) {
                      if (self) {
                        self->FinishRun(false, error);
                      }
                    },
                    self, error));
          },
          weak_factory_.GetWeakPtr(), dream_date, error));
}

void DaoDreamService::PersistResult(const std::string& dream_date,
                                    base::DictValue result) {
  // 1. Habits → MergePreference (confidence capped; contradict skipped —
  //    surfaced in the report only, the user adjudicates).
  if (const base::ListValue* habits = result.FindList("habits")) {
    for (const base::Value& h : *habits) {
      const base::DictValue* habit = h.GetIfDict();
      if (!habit) {
        continue;
      }
      const std::string* key = habit->FindString("key");
      const std::string* value = habit->FindString("value");
      const std::string* relation = habit->FindString("relation");
      if (!key || !value || key->empty() || value->empty()) {
        continue;
      }
      if (relation && *relation == "contradict") {
        continue;
      }
      double confidence = habit->FindDouble("confidence").value_or(0.5);
      confidence = std::min(confidence, kMaxLLMConfidence);
      memory_service_->MergePreference(*key, *value, confidence,
                                       base::DoNothing());
    }
  }

  // 2. Report row.
  DreamReport report;
  report.dream_date = dream_date;
  const std::string* md = result.FindString("report_markdown");
  report.report_markdown = md ? *md : "";
  if (const base::ListValue* habits = result.FindList("habits")) {
    base::JSONWriter::Write(*habits, &report.habit_candidates);
  }
  base::JSONWriter::Write(pending_material_stats_, &report.material_stats);
  report.status = "completed";
  report.trigger_kind = active_kind_ == TriggerKind::kManual ? "manual"
                        : active_kind_ == TriggerKind::kCatchUp ? "catchup"
                                                                : "nightly";
  report.debug_material_json = pending_debug_material_json_;
  memory_service_->SaveDreamReport(
      std::move(report),
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, bool ok) {
            if (self) {
              self->FinishRun(ok, ok ? "" : "failed to save dream report");
            }
          },
          weak_factory_.GetWeakPtr()));
}

void DaoDreamService::StartManualDream(
    base::OnceCallback<void(bool success, const std::string& error)> callback) {
  StartManualDreamForDate(DreamDateFor(clock_->Now()), std::move(callback));
}

void DaoDreamService::StartManualDreamForDate(
    const std::string& dream_date,
    base::OnceCallback<void(bool success, const std::string& error)> callback) {
  base::Time parsed;
  if (!ParseYmd(dream_date, &parsed)) {
    std::move(callback).Run(false, "invalid dream date");
    return;
  }
  if (IsFutureYmd(dream_date, clock_->Now())) {
    std::move(callback).Run(false, "dream date cannot be in the future");
    return;
  }
  if (state_ != State::kIdle) {
    std::move(callback).Run(false, "dream already running");
    return;
  }
  if (!runner_) {
    std::move(callback).Run(false, "agent webui unavailable");
    return;
  }
  preserve_completed_report_on_failure_ = true;
  manual_callback_ = std::move(callback);
  StartDream(dream_date, TriggerKind::kManual);
}

void DaoDreamService::FinishRun(bool success, const std::string& error) {
  state_ = State::kIdle;
  active_dream_date_.clear();
  pending_debug_material_json_.clear();
  pending_material_stats_.clear();
  preserve_completed_report_on_failure_ = false;
  if (manual_callback_) {
    std::move(manual_callback_).Run(success, error);
  }
}

}  // namespace dao
