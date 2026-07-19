// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_service.h"

#include <algorithm>
#include <array>
#include <utility>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_dream_material_collector.h"
#include "dao/browser/agent/dao_weekly_dream_material_collector.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/base/idle/idle.h"

namespace dao {

namespace {

struct CalendarDate {
  int year;
  int month;
  int day;
};

bool IsLeapYear(int year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int DaysInMonth(int year, int month) {
  constexpr std::array<int, 12> kDaysPerMonth = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && IsLeapYear(year)) {
    return 29;
  }
  CHECK_GE(month, 1);
  CHECK_LE(month, 12);
  return kDaysPerMonth[month - 1];
}

void ShiftCalendarDate(CalendarDate* date, int days) {
  while (days < 0) {
    if (date->day > 1) {
      --date->day;
    } else {
      if (--date->month == 0) {
        date->month = 12;
        --date->year;
      }
      date->day = DaysInMonth(date->year, date->month);
    }
    ++days;
  }
  while (days > 0) {
    if (date->day < DaysInMonth(date->year, date->month)) {
      ++date->day;
    } else {
      date->day = 1;
      if (++date->month == 13) {
        date->month = 1;
        ++date->year;
      }
    }
    --days;
  }
}

base::Time BuildLocalTime(const CalendarDate& date, int hour) {
  base::Time::Exploded exploded = {};
  exploded.year = date.year;
  exploded.month = date.month;
  exploded.day_of_month = date.day;
  exploded.hour = hour;
  base::Time result;
  CHECK(base::Time::FromLocalExploded(exploded, &result));
  return result;
}

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

bool ParseStrictYmd(const std::string& ymd, base::Time* out) {
  if (ymd.size() != 10 || ymd[4] != '-' || ymd[7] != '-') {
    return false;
  }
  for (size_t i = 0; i < ymd.size(); ++i) {
    if (i == 4 || i == 7) {
      continue;
    }
    if (ymd[i] < '0' || ymd[i] > '9') {
      return false;
    }
  }
  return ParseYmd(ymd, out) && FormatYmd(*out) == ymd;
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

std::string NextLocalDateLabel(const std::string& ymd) {
  base::Time date_midnight;
  if (!ParseYmd(ymd, &date_midnight)) {
    return ymd;
  }
  base::Time::Exploded exploded;
  date_midnight.LocalExplode(&exploded);
  CalendarDate next{exploded.year, exploded.month, exploded.day_of_month};
  ShiftCalendarDate(&next, 1);
  return base::StringPrintf("%04d-%02d-%02d", next.year, next.month,
                            next.day);
}

const char* TriggerKindToStorageString(DaoDreamService::TriggerKind kind) {
  switch (kind) {
    case DaoDreamService::TriggerKind::kNightly:
      return "nightly";
    case DaoDreamService::TriggerKind::kScheduledWeekly:
      return "scheduled";
    case DaoDreamService::TriggerKind::kCatchUp:
      return "catchup";
    case DaoDreamService::TriggerKind::kManual:
      return "manual";
  }
  return "nightly";
}

bool IsScheduledWeeklyTime(base::Time now) {
  base::Time::Exploded local;
  now.LocalExplode(&local);
  return local.day_of_week == 1 && local.hour >= 6;
}

}  // namespace

DaoDreamService::DreamRunRequest::DreamRunRequest() = default;
DaoDreamService::DreamRunRequest::DreamRunRequest(const DreamRunRequest&) =
    default;
DaoDreamService::DreamRunRequest&
DaoDreamService::DreamRunRequest::operator=(const DreamRunRequest&) = default;
DaoDreamService::DreamRunRequest::DreamRunRequest(DreamRunRequest&&) noexcept =
    default;
DaoDreamService::DreamRunRequest&
DaoDreamService::DreamRunRequest::operator=(DreamRunRequest&&) noexcept =
    default;
DaoDreamService::DreamRunRequest::~DreamRunRequest() = default;

DaoDreamService::DreamRunFailure::DreamRunFailure(std::string code_value,
                                                  std::string message_value)
    : code(std::move(code_value)), message(std::move(message_value)) {}
DaoDreamService::DreamRunFailure::DreamRunFailure(const DreamRunFailure&) =
    default;
DaoDreamService::DreamRunFailure&
DaoDreamService::DreamRunFailure::operator=(const DreamRunFailure&) = default;
DaoDreamService::DreamRunFailure::DreamRunFailure(DreamRunFailure&&) noexcept =
    default;
DaoDreamService::DreamRunFailure&
DaoDreamService::DreamRunFailure::operator=(DreamRunFailure&&) noexcept =
    default;
DaoDreamService::DreamRunFailure::~DreamRunFailure() = default;

DaoDreamService::DaoDreamService(Profile* profile,
                                 DaoAgentMemoryService* memory_service)
    : profile_(profile), memory_service_(memory_service) {
  daily_collector_ =
      std::make_unique<DreamMaterialCollector>(profile, memory_service);
  weekly_collector_ = std::make_unique<WeeklyDreamMaterialCollector>(
      profile, memory_service);
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

void DaoDreamService::FireDreamTimeoutForTesting() {
  if (dream_timeout_timer_.IsRunning()) {
    dream_timeout_timer_.FireNow();
  }
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
void DaoDreamService::LatestCompletedWeeklyWindow(
    base::Time now,
    base::Time* start,
    base::Time* end,
    std::string* week_start,
    std::string* week_end) {
  base::Time::Exploded local_now;
  now.LocalExplode(&local_now);

  CalendarDate end_date{local_now.year, local_now.month,
                        local_now.day_of_month};
  const int days_since_monday = (local_now.day_of_week + 6) % 7;
  ShiftCalendarDate(&end_date, -days_since_monday);
  if (days_since_monday == 0 && local_now.hour < 6) {
    ShiftCalendarDate(&end_date, -7);
  }

  CalendarDate start_date = end_date;
  ShiftCalendarDate(&start_date, -7);

  // Construct both local boundaries independently. In particular, never
  // derive either boundary by adding or subtracting 168 elapsed hours.
  *start = BuildLocalTime(start_date, 6);
  *end = BuildLocalTime(end_date, 6);
  *week_start = base::StringPrintf("%04d-%02d-%02d", start_date.year,
                                   start_date.month, start_date.day);
  *week_end = base::StringPrintf("%04d-%02d-%02d", end_date.year,
                                 end_date.month, end_date.day);
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

bool DaoDreamService::WeeklyDreamPrefEnabled() const {
  return DreamPrefEnabled() &&
         profile_->GetPrefs()->GetBoolean(prefs::kDaoDreamWeeklyEnabled);
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
  if (scheduler_check_in_flight_) {
    return;
  }
  scheduler_check_in_flight_ = true;
  const uint64_t scheduler_check_epoch = ++scheduler_check_epoch_;
  const std::string date = DreamDateFor(clock_->Now());
  memory_service_->GetDreamReportByDate(
      date, base::BindOnce(&DaoDreamService::OnExistingReportLoaded,
                           weak_factory_.GetWeakPtr(), scheduler_check_epoch,
                           date, TriggerKind::kNightly,
                           /*chain_weekly=*/false));
}

void DaoDreamService::MaybeStartCatchUp() {
  if (scheduler_check_in_flight_) {
    return;
  }
  scheduler_check_in_flight_ = true;
  const uint64_t scheduler_check_epoch = ++scheduler_check_epoch_;
  const std::string yesterday = FormatYmd(clock_->Now() - base::Days(1));
  memory_service_->GetDreamReportByDate(
      yesterday,
      base::BindOnce(&DaoDreamService::OnExistingReportLoaded,
                     weak_factory_.GetWeakPtr(), scheduler_check_epoch,
                     yesterday, TriggerKind::kCatchUp,
                     /*chain_weekly=*/true));
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

void DaoDreamService::InvalidatePendingSchedulerCheck() {
  ++scheduler_check_epoch_;
  scheduler_check_in_flight_ = false;
}

void DaoDreamService::DispatchSchedulerReply(base::OnceClosure reply) {
  if (scheduler_reply_interceptor_for_testing_) {
    scheduler_reply_interceptor_for_testing_.Run(std::move(reply));
    return;
  }
  std::move(reply).Run();
}

void DaoDreamService::OnExistingReportLoaded(
    uint64_t scheduler_check_epoch,
    const std::string& dream_date,
    TriggerKind kind,
    bool chain_weekly,
    std::optional<DreamReport> existing) {
  DispatchSchedulerReply(base::BindOnce(
      &DaoDreamService::OnExistingReportChecked,
      weak_factory_.GetWeakPtr(), scheduler_check_epoch, dream_date, kind,
      chain_weekly, std::move(existing)));
}

void DaoDreamService::OnExistingReportChecked(
    uint64_t scheduler_check_epoch,
    const std::string& dream_date,
    TriggerKind kind,
    bool chain_weekly,
    std::optional<DreamReport> existing) {
  if (scheduler_check_epoch != scheduler_check_epoch_) {
    return;
  }
  scheduler_check_in_flight_ = false;
  if (state_ != State::kIdle || !DreamPrefEnabled() || !runner_) {
    return;  // another path won the race
  }
  if (existing) {
    if (existing->status == "completed" || existing->status == "skipped" ||
        existing->attempt_count >= kMaxAttemptsPerNight) {
      if (chain_weekly) {
        MaybeStartWeeklyCatchUp();
      }
      return;
    }
  }
  chain_weekly_after_run_ = chain_weekly;
  StartDream(dream_date, kind);
}

void DaoDreamService::MaybeStartWeeklyCatchUp() {
  if (!WeeklyDreamPrefEnabled() || state_ != State::kIdle || !runner_ ||
      scheduler_check_in_flight_ ||
      GetIdleSeconds() < kCatchUpIdleSeconds) {
    return;
  }

  base::Time window_start;
  base::Time window_end;
  std::string week_start;
  std::string week_end;
  const base::Time now = clock_->Now();
  if (!weekly_defer_until_.is_null() && now < weekly_defer_until_) {
    return;
  }
  LatestCompletedWeeklyWindow(now, &window_start, &window_end, &week_start,
                              &week_end);

  DreamRunRequest request;
  request.request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  request.report_kind = ReportKind::kWeekly;
  request.period_start = week_start;
  request.period_end = week_end;
  request.trigger_kind = IsScheduledWeeklyTime(now)
                             ? TriggerKind::kScheduledWeekly
                             : TriggerKind::kCatchUp;

  scheduler_check_in_flight_ = true;
  const uint64_t scheduler_check_epoch = ++scheduler_check_epoch_;
  memory_service_->GetWeeklyDreamReportByWeekStart(
      week_start,
      base::BindOnce(&DaoDreamService::OnExistingWeeklyReportLoaded,
                     weak_factory_.GetWeakPtr(), scheduler_check_epoch,
                     std::move(request), window_start, window_end));
}

void DaoDreamService::OnExistingWeeklyReportLoaded(
    uint64_t scheduler_check_epoch,
    DreamRunRequest request,
    base::Time window_start,
    base::Time window_end,
    std::optional<WeeklyDreamReport> existing) {
  DispatchSchedulerReply(base::BindOnce(
      &DaoDreamService::OnExistingWeeklyReportChecked,
      weak_factory_.GetWeakPtr(), scheduler_check_epoch, std::move(request),
      window_start, window_end, std::move(existing)));
}

void DaoDreamService::OnExistingWeeklyReportChecked(
    uint64_t scheduler_check_epoch,
    DreamRunRequest request,
    base::Time window_start,
    base::Time window_end,
    std::optional<WeeklyDreamReport> existing) {
  if (scheduler_check_epoch != scheduler_check_epoch_) {
    return;
  }
  scheduler_check_in_flight_ = false;
  if (state_ != State::kIdle || !WeeklyDreamPrefEnabled() || !runner_ ||
      GetIdleSeconds() < kCatchUpIdleSeconds) {
    return;
  }
  const base::Time now = clock_->Now();
  if (!weekly_defer_until_.is_null() && now < weekly_defer_until_) {
    return;
  }

  base::Time latest_start;
  base::Time latest_end;
  std::string latest_week_start;
  std::string latest_week_end;
  LatestCompletedWeeklyWindow(now, &latest_start, &latest_end,
                              &latest_week_start, &latest_week_end);
  if (request.period_start != latest_week_start ||
      request.period_end != latest_week_end) {
    OnSchedulerTick();
    return;
  }
  if (existing &&
      (existing->status == "completed" || existing->status == "skipped" ||
       (existing->status == "failed" &&
        existing->attempt_count >= kMaxAttemptsPerNight))) {
    return;
  }
  StartWeeklyDream(std::move(request), window_start, window_end,
                   std::move(existing));
}

void DaoDreamService::OnManualExistingWeeklyReportChecked(
    DreamRunRequest request,
    base::Time window_start,
    base::Time window_end,
    std::optional<WeeklyDreamReport> existing) {
  if (state_ != State::kCollecting || !active_request_ ||
      active_request_->report_kind != ReportKind::kWeekly ||
      active_request_->trigger_kind != TriggerKind::kManual ||
      active_request_->request_id != request.request_id) {
    return;
  }
  if (!runner_) {
    DeferWeeklyRunAndFinish(request, "dream_runner_unavailable");
    return;
  }
  StartWeeklyDream(std::move(request), window_start, window_end,
                   std::move(existing));
}

void DaoDreamService::StartWeeklyDream(
    DreamRunRequest request,
    base::Time window_start,
    base::Time window_end,
    std::optional<WeeklyDreamReport> existing) {
  state_ = State::kCollecting;
  chain_weekly_after_run_ = false;
  active_request_ = request;
  active_existing_weekly_report_ = std::move(existing);
  pending_weekly_sources_.clear();
  weekly_collector_->Collect(
      window_start, window_end, request.period_start, request.period_end,
      base::BindOnce(&DaoDreamService::OnWeeklyMaterialCollected,
                     weak_factory_.GetWeakPtr(), request.request_id));
  if (weekly_collection_started_callback_for_testing_) {
    std::move(weekly_collection_started_callback_for_testing_).Run();
  }
}

void DaoDreamService::StartDream(const std::string& dream_date,
                                 TriggerKind kind) {
  state_ = State::kCollecting;
  DreamRunRequest request;
  request.request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  request.report_kind = ReportKind::kDaily;
  request.period_start = dream_date;
  request.period_end = NextLocalDateLabel(dream_date);
  request.trigger_kind = kind;
  active_request_ = request;
  active_existing_weekly_report_.reset();
  pending_weekly_sources_.clear();

  base::Time start;
  base::Time end;
  MaterialWindowFor(dream_date, clock_->Now(), &start, &end);
  daily_collector_->Collect(
      start, end,
      base::BindOnce(&DaoDreamService::OnMaterialCollected,
                     weak_factory_.GetWeakPtr(), request.request_id));
}

void DaoDreamService::OnMaterialCollected(const std::string& request_id,
                                          base::DictValue material) {
  if (state_ != State::kCollecting || !active_request_ ||
      request_id != active_request_->request_id) {
    return;
  }
  // Empty material → silent skip (not a failure).
  const base::ListValue* history = material.FindList("history");
  const base::ListValue* convs = material.FindList("conversations");
  if ((!history || history->empty()) && (!convs || convs->empty())) {
    FinishRun(false, "no dream material collected");
    return;
  }
  if (!runner_) {
    DreamRunRequest request = *active_request_;
    state_ = State::kSaving;
    MarkFailed(std::move(request), "agent webui unavailable");
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
                     weak_factory_.GetWeakPtr(), request_id));
  runner_->RunDream(*active_request_, material);
}

void DaoDreamService::OnWeeklyMaterialCollected(
    const std::string& request_id,
    WeeklyDreamMaterial material) {
  if (state_ != State::kCollecting || !active_request_ ||
      active_request_->report_kind != ReportKind::kWeekly ||
      request_id != active_request_->request_id) {
    return;
  }

  pending_weekly_sources_ = std::move(material.local_sources);
  pending_debug_material_json_.clear();
  if (profile_->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug)) {
    base::JSONWriter::WriteWithOptions(
        material.model_material, base::JSONWriter::OPTIONS_PRETTY_PRINT,
        &pending_debug_material_json_);
  }
  if (const base::DictValue* stats =
          material.model_material.FindDict("stats")) {
    pending_material_stats_ = stats->Clone();
  }

  // A model result requires at least one resolvable opaque source reference.
  // Persisting a marker prevents the five-minute scheduler from retrying a
  // genuinely sparse week.
  if (pending_weekly_sources_.empty()) {
    DreamRunRequest request = *active_request_;
    state_ = State::kSaving;
    PersistWeeklySkipped(std::move(request));
    return;
  }
  if (!runner_) {
    DreamRunRequest request = *active_request_;
    DeferWeeklyRunAndFinish(request, "dream_runner_unavailable");
    return;
  }

  state_ = State::kDreaming;
  dream_timeout_timer_.Start(
      FROM_HERE, kDreamTimeout,
      base::BindOnce(&DaoDreamService::OnDreamTimeout,
                     weak_factory_.GetWeakPtr(), request_id));
  // local_sources stays native-only. Only the redacted model pack crosses
  // into the resident WebUI runner.
  runner_->RunDream(*active_request_, material.model_material);
}

void DaoDreamService::PersistWeeklySkipped(DreamRunRequest request) {
  // A sparse manual rerun is not a replacement result. Keep an already
  // completed report and its native source map intact.
  if (request.trigger_kind == TriggerKind::kManual &&
      active_existing_weekly_report_ &&
      active_existing_weekly_report_->status == "completed") {
    FinishRun(false, "weekly_no_material");
    return;
  }

  WeeklyDreamReport report;
  report.week_start = request.period_start;
  report.week_end = request.period_end;
  report.content_json = "{}";
  base::JSONWriter::Write(pending_material_stats_, &report.material_stats);
  report.status = "skipped";
  report.attempt_count = active_existing_weekly_report_
                             ? active_existing_weekly_report_->attempt_count
                             : 0;
  report.trigger_kind = TriggerKindToStorageString(request.trigger_kind);
  report.debug_material_json = pending_debug_material_json_;
  const bool manual_trigger =
      request.trigger_kind == TriggerKind::kManual;
  memory_service_->SaveWeeklyDreamReport(
      std::move(report), {},
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, std::string request_id,
             bool manual_trigger, bool saved) {
            if (!self || self->state_ != State::kSaving ||
                !self->active_request_ ||
                self->active_request_->report_kind != ReportKind::kWeekly ||
                self->active_request_->request_id != request_id) {
              return;
            }
            if (!saved) {
              self->FinishRun(false, "weekly_save_failed");
              return;
            }
            if (manual_trigger) {
              self->FinishRun(false, "weekly_no_material");
              return;
            }
            self->FinishRun(true, "");
          },
          weak_factory_.GetWeakPtr(), request.request_id, manual_trigger));
}

void DaoDreamService::PersistWeeklyFailure(
    DreamRunRequest request,
    const std::string& manual_error) {
  if (request.trigger_kind == TriggerKind::kManual &&
      active_existing_weekly_report_ &&
      active_existing_weekly_report_->status == "completed") {
    FinishRun(false, manual_error);
    return;
  }

  WeeklyDreamReport report;
  report.week_start = request.period_start;
  report.week_end = request.period_end;
  report.content_json = "{}";
  base::JSONWriter::Write(pending_material_stats_, &report.material_stats);
  report.status = "failed";
  report.attempt_count = active_existing_weekly_report_
                             ? active_existing_weekly_report_->attempt_count + 1
                             : 1;
  report.trigger_kind = TriggerKindToStorageString(request.trigger_kind);
  report.debug_material_json = pending_debug_material_json_;
  const bool manual_trigger =
      request.trigger_kind == TriggerKind::kManual;
  memory_service_->SaveWeeklyDreamReport(
      std::move(report), {},
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, std::string request_id,
             bool manual_trigger, std::string manual_error, bool saved) {
            if (!self || self->state_ != State::kSaving ||
                !self->active_request_ ||
                self->active_request_->report_kind != ReportKind::kWeekly ||
                self->active_request_->request_id != request_id) {
              return;
            }
            if (!saved) {
              self->FinishRun(false, "weekly_save_failed");
              return;
            }
            self->FinishRun(false, manual_trigger ? manual_error : "");
          },
          weak_factory_.GetWeakPtr(), request.request_id, manual_trigger,
          manual_error));
}

void DaoDreamService::OnDreamTimeout(const std::string& request_id) {
  if (state_ != State::kDreaming || !active_request_ ||
      request_id != active_request_->request_id) {
    return;
  }
  DreamRunRequest request = *active_request_;
  if (request.report_kind == ReportKind::kWeekly) {
    DeferWeeklyRunAndFinish(request, "weekly_timeout");
    return;
  }
  state_ = State::kSaving;
  MarkFailed(std::move(request), "timeout");
}

void DaoDreamService::OnDreamResult(const std::string& request_id,
                                    base::DictValue result) {
  if (state_ != State::kDreaming || !active_request_ ||
      request_id != active_request_->request_id) {
    return;  // stale result (timeout already fired, or unexpected)
  }
  dream_timeout_timer_.Stop();
  if (active_request_->report_kind == ReportKind::kWeekly) {
    DreamRunRequest request = *active_request_;
    state_ = State::kSaving;
    PersistWeeklyResult(std::move(request), std::move(result));
    return;
  }
  state_ = State::kSaving;
  PersistResult(active_request_->period_start, std::move(result));
}

void DaoDreamService::OnDreamSkipped(const std::string& request_id) {
  if (state_ != State::kDreaming || !active_request_ ||
      request_id != active_request_->request_id) {
    return;
  }
  dream_timeout_timer_.Stop();
  if (active_request_->report_kind == ReportKind::kWeekly) {
    DreamRunRequest request = *active_request_;
    state_ = State::kSaving;
    PersistWeeklySkipped(std::move(request));
    return;
  }
  FinishRun(false, "dream skipped");
}

void DaoDreamService::OnDreamFailed(const std::string& request_id,
                                    DreamRunFailure failure) {
  if (state_ != State::kDreaming || !active_request_ ||
      request_id != active_request_->request_id) {
    return;
  }
  dream_timeout_timer_.Stop();
  DreamRunRequest request = *active_request_;
  if (request.report_kind == ReportKind::kWeekly) {
    if (failure.code == "invalid_output") {
      state_ = State::kSaving;
      PersistWeeklyFailure(std::move(request), "weekly_invalid_output");
      return;
    }
    if (failure.code == "configuration") {
      DeferWeeklyRunAndFinish(request, "weekly_configuration");
      return;
    }
    if (failure.code == "provider") {
      DeferWeeklyRunAndFinish(request, "weekly_provider");
      return;
    }
    FinishRun(false, failure.message);
    return;
  }
  state_ = State::kSaving;
  MarkFailed(std::move(request), failure.message);
}

void DaoDreamService::DeferWeeklyRunAndFinish(
    const DreamRunRequest& request,
    const std::string& manual_error) {
  weekly_defer_until_ = clock_->Now() + kWeeklyTransientDefer;
  FinishRun(false, request.trigger_kind == TriggerKind::kManual
                       ? manual_error
                       : "");
}

void DaoDreamService::MarkFailed(DreamRunRequest request,
                                 const std::string& error) {
  LOG(ERROR) << "Dream run failed for " << request.period_start << ": "
             << error;
  const bool preserve_completed_report =
      preserve_completed_report_on_failure_;
  // Read the existing attempt count, then write a failed row with +1.
  memory_service_->GetDreamReportByDate(
      request.period_start,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, DreamRunRequest request,
             std::string error, bool preserve_completed_report,
             std::optional<DreamReport> existing) {
            if (!self || self->state_ != State::kSaving ||
                !self->active_request_ ||
                self->active_request_->request_id != request.request_id) {
              return;
            }
            DreamReport report;
            if (existing) {
              report = std::move(*existing);
            }
            if (preserve_completed_report && report.status == "completed") {
              self->FinishRun(false, error);
              return;
            }
            report.dream_date = request.period_start;
            report.status = "failed";
            report.attempt_count += 1;
            report.trigger_kind =
                TriggerKindToStorageString(request.trigger_kind);
            self->memory_service_->SaveDreamReport(
                std::move(report),
                base::BindOnce(
                    [](base::WeakPtr<DaoDreamService> self,
                       std::string request_id, std::string error, bool) {
                      if (self && self->state_ == State::kSaving &&
                          self->active_request_ &&
                          self->active_request_->request_id == request_id) {
                        self->FinishRun(false, error);
                      }
                    },
                    self, request.request_id, error));
          },
          weak_factory_.GetWeakPtr(), std::move(request), error,
          preserve_completed_report));
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
  CHECK(active_request_);
  report.trigger_kind =
      TriggerKindToStorageString(active_request_->trigger_kind);
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

void DaoDreamService::PersistWeeklyResult(DreamRunRequest request,
                                          base::DictValue result) {
  WeeklyDreamReport report;
  report.week_start = request.period_start;
  report.week_end = request.period_end;
  if (!base::JSONWriter::Write(result, &report.content_json)) {
    FinishRun(false, "weekly_save_failed");
    return;
  }
  base::JSONWriter::Write(pending_material_stats_, &report.material_stats);
  report.status = "completed";
  report.attempt_count = active_existing_weekly_report_
                             ? active_existing_weekly_report_->attempt_count + 1
                             : 1;
  report.trigger_kind = TriggerKindToStorageString(request.trigger_kind);
  report.debug_material_json = pending_debug_material_json_;

  memory_service_->SaveWeeklyDreamReport(
      std::move(report), std::move(pending_weekly_sources_),
      base::BindOnce(
          [](base::WeakPtr<DaoDreamService> self, std::string request_id,
             bool saved) {
            if (!self || self->state_ != State::kSaving ||
                !self->active_request_ ||
                self->active_request_->report_kind != ReportKind::kWeekly ||
                self->active_request_->request_id != request_id) {
              return;
            }
            self->FinishRun(saved, saved ? "" : "weekly_save_failed");
          },
          weak_factory_.GetWeakPtr(), request.request_id));
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
  InvalidatePendingSchedulerCheck();
  preserve_completed_report_on_failure_ = true;
  manual_callback_ = std::move(callback);
  StartDream(dream_date, TriggerKind::kManual);
}

void DaoDreamService::StartManualWeeklyDream(
    base::OnceCallback<void(bool success, const std::string& error)> callback) {
  base::Time window_start;
  base::Time window_end;
  std::string week_start;
  std::string week_end;
  LatestCompletedWeeklyWindow(clock_->Now(), &window_start, &window_end,
                              &week_start, &week_end);
  StartManualWeeklyDreamForWeekStart(week_start, std::move(callback));
}

void DaoDreamService::StartManualWeeklyDreamForWeekStart(
    const std::string& week_start,
    base::OnceCallback<void(bool success, const std::string& error)> callback) {
  base::Time parsed;
  if (!ParseStrictYmd(week_start, &parsed)) {
    std::move(callback).Run(false, "weekly_invalid_week_start");
    return;
  }

  base::Time::Exploded local_start;
  parsed.LocalExplode(&local_start);
  if (local_start.day_of_week != 1) {
    std::move(callback).Run(false, "weekly_week_start_not_monday");
    return;
  }

  CalendarDate start_date{local_start.year, local_start.month,
                          local_start.day_of_month};
  CalendarDate end_date = start_date;
  ShiftCalendarDate(&end_date, 7);
  const base::Time window_start = BuildLocalTime(start_date, 6);
  const base::Time window_end = BuildLocalTime(end_date, 6);
  if (window_end > clock_->Now()) {
    std::move(callback).Run(false, "weekly_week_incomplete");
    return;
  }
  if (state_ != State::kIdle) {
    std::move(callback).Run(false, "dream_busy");
    return;
  }
  if (!runner_) {
    std::move(callback).Run(false, "dream_runner_unavailable");
    return;
  }
  InvalidatePendingSchedulerCheck();

  DreamRunRequest request;
  request.request_id = base::Uuid::GenerateRandomV4().AsLowercaseString();
  request.report_kind = ReportKind::kWeekly;
  request.period_start = week_start;
  request.period_end = base::StringPrintf("%04d-%02d-%02d", end_date.year,
                                          end_date.month, end_date.day);
  request.trigger_kind = TriggerKind::kManual;

  // The existing-row lookup is part of the run. Occupying the shared state and
  // request here prevents another daily or weekly run from entering while the
  // asynchronous callback is pending.
  state_ = State::kCollecting;
  active_request_ = request;
  active_existing_weekly_report_.reset();
  pending_weekly_sources_.clear();
  pending_debug_material_json_.clear();
  pending_material_stats_.clear();
  chain_weekly_after_run_ = false;
  manual_callback_ = std::move(callback);
  memory_service_->GetWeeklyDreamReportByWeekStart(
      week_start,
      base::BindOnce(&DaoDreamService::OnManualExistingWeeklyReportChecked,
                     weak_factory_.GetWeakPtr(), std::move(request),
                     window_start, window_end));
}

void DaoDreamService::FinishRun(bool success, const std::string& error) {
  const bool start_weekly = chain_weekly_after_run_;
  state_ = State::kIdle;
  active_request_.reset();
  active_existing_weekly_report_.reset();
  pending_weekly_sources_.clear();
  pending_debug_material_json_.clear();
  pending_material_stats_.clear();
  preserve_completed_report_on_failure_ = false;
  chain_weekly_after_run_ = false;
  if (manual_callback_) {
    std::move(manual_callback_).Run(success, error);
  }
  if (start_weekly) {
    MaybeStartWeeklyCatchUp();
  }
}

}  // namespace dao
