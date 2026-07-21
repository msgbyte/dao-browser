// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/power_monitor/power_observer.h"
#include "base/time/clock.h"
#include "base/time/default_clock.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

class Profile;

namespace dao {

class DaoAgentMemoryService;
class DreamMaterialCollector;
class WeeklyDreamMaterialCollector;
struct WeeklyDreamMaterial;

// Orchestrates the Dream Analysis pipeline: trigger decision (nightly /
// catch-up / manual), material collection, hand-off to the agent WebUI
// for LLM summarization, and persistence of results. The LLM call itself
// happens in the resident agent WebUI (see DaoAgentDreamHandler); this
// service exposes the material pack to it and receives the result.
class DaoDreamService : public KeyedService,
                        public base::PowerSuspendObserver {
 public:
  enum class State { kIdle, kCollecting, kDreaming, kSaving };
  enum class ReportKind { kDaily, kWeekly };
  enum class TriggerKind {
    kNightly,
    kScheduledWeekly,
    kCatchUp,
    kManual,
  };

  struct DreamRunRequest {
    DreamRunRequest();
    DreamRunRequest(const DreamRunRequest&);
    DreamRunRequest& operator=(const DreamRunRequest&);
    DreamRunRequest(DreamRunRequest&&) noexcept;
    DreamRunRequest& operator=(DreamRunRequest&&) noexcept;
    ~DreamRunRequest();

    std::string request_id;
    ReportKind report_kind = ReportKind::kDaily;
    std::string period_start;
    std::string period_end;
    TriggerKind trigger_kind = TriggerKind::kNightly;
  };

  struct DreamRunFailure {
    DreamRunFailure(std::string code_value, std::string message_value);
    DreamRunFailure(const DreamRunFailure&);
    DreamRunFailure& operator=(const DreamRunFailure&);
    DreamRunFailure(DreamRunFailure&&) noexcept;
    DreamRunFailure& operator=(DreamRunFailure&&) noexcept;
    ~DreamRunFailure();

    std::string code;
    std::string message;
  };

  // The WebUI side registers itself here to receive dream-run requests.
  class Runner {
   public:
    virtual ~Runner() = default;
    // Asks the WebUI to run the LLM summarization for `material`. The
    // runner replies through OnDreamResult / OnDreamFailed.
    virtual void RunDream(const DreamRunRequest& request,
                          const base::DictValue& material) = 0;
  };

  DaoDreamService(Profile* profile, DaoAgentMemoryService* memory_service);
  ~DaoDreamService() override;

  DaoDreamService(const DaoDreamService&) = delete;
  DaoDreamService& operator=(const DaoDreamService&) = delete;

  // KeyedService:
  void Shutdown() override;

  // base::PowerSuspendObserver:
  void OnResume() override;

  // Test hooks: inject a clock and an idle-seconds supplier.
  void SetClockForTesting(const base::Clock* clock) { clock_ = clock; }
  void SetIdleTimeCallbackForTesting(base::RepeatingCallback<int()> callback) {
    idle_time_callback_ = std::move(callback);
  }
  void SetWeeklyCollectionStartedCallbackForTesting(
      base::OnceClosure callback) {
    weekly_collection_started_callback_for_testing_ = std::move(callback);
  }
  void SetSchedulerReplyInterceptorForTesting(
      base::RepeatingCallback<void(base::OnceClosure)> callback) {
    scheduler_reply_interceptor_for_testing_ = std::move(callback);
  }
  // Runs one scheduler tick immediately (test convenience).
  void TickForTesting() { OnSchedulerTick(); }
  // Fires the active request timeout without waiting five minutes.
  void FireDreamTimeoutForTesting();

  State state() const { return state_; }

  // WebUI runner registration (one at a time; last wins).
  void SetRunner(Runner* runner);
  void ClearRunner(Runner* runner);

  // Manual trigger from the settings UI. Bypasses night/idle/done checks.
  // `callback` fires when the run completes or fails. On failure, `error`
  // carries the runner/service reason when one is available.
  void StartManualDream(
      base::OnceCallback<void(bool success, const std::string& error)> callback);
  void StartManualDreamForDate(
      const std::string& dream_date,
      base::OnceCallback<void(bool success, const std::string& error)> callback);

  // Manual weekly triggers bypass the automatic weekly-report preference but
  // still accept only fully completed local Monday-to-Monday windows.
  void StartManualWeeklyDream(
      base::OnceCallback<void(bool success, const std::string& error)> callback);
  void StartManualWeeklyDreamForWeekStart(
      const std::string& week_start,
      base::OnceCallback<void(bool success, const std::string& error)> callback);

  // Result entry points called by the WebUI handler.
  void OnDreamResult(const std::string& request_id, base::DictValue result);
  void OnDreamSkipped(const std::string& request_id);
  void OnDreamFailed(const std::string& request_id,
                     DreamRunFailure failure);

  // Computes the dream-day label ("YYYY-MM-DD", local) for `now`.
  // 22:00–24:00 → today; 00:00–06:00 → yesterday; daytime → today.
  static std::string DreamDateFor(base::Time now);

  // Material window [start, end) for a dream date: date 06:00 local →
  // min(date+1 06:00, now).
  static void MaterialWindowFor(const std::string& dream_date,
                                base::Time now,
                                base::Time* start,
                                base::Time* end);

  // Returns the latest completed local Dream week. Both boundaries are built
  // independently from local Monday 06:00 calendar fields so DST changes do
  // not shift either boundary.
  static void LatestCompletedWeeklyWindow(base::Time now,
                                          base::Time* start,
                                          base::Time* end,
                                          std::string* week_start,
                                          std::string* week_end);

  // True if local time is within the night window 22:00–06:00.
  static bool IsNightTime(base::Time now);

 private:
  void OnSchedulerTick();
  void MaybeStartNightly();
  void MaybeStartCatchUp();
  void MaybeStartCatchUpForRecentActivity();
  void MaybeStartWeeklyCatchUp();
  void InvalidatePendingSchedulerCheck();
  void DispatchSchedulerReply(base::OnceClosure reply);
  void StartDream(const std::string& dream_date, TriggerKind kind);
  void StartDailyDream(DreamRunRequest request,
                       std::optional<DreamReport> existing);
  void OnManualExistingDailyReportChecked(
      DreamRunRequest request,
      std::optional<DreamReport> existing);
  void OnExistingReportLoaded(uint64_t scheduler_check_epoch,
                              const std::string& dream_date,
                              TriggerKind kind,
                              bool chain_weekly,
                              std::optional<DreamReport> existing);
  void OnExistingReportChecked(uint64_t scheduler_check_epoch,
                               const std::string& dream_date,
                               TriggerKind kind,
                               bool chain_weekly,
                               std::optional<DreamReport> existing);
  void OnExistingWeeklyReportLoaded(
      uint64_t scheduler_check_epoch,
      DreamRunRequest request,
      base::Time window_start,
      base::Time window_end,
      std::optional<WeeklyDreamReport> existing);
  void OnExistingWeeklyReportChecked(
      uint64_t scheduler_check_epoch,
      DreamRunRequest request,
      base::Time window_start,
      base::Time window_end,
      std::optional<WeeklyDreamReport> existing);
  void OnManualExistingWeeklyReportChecked(
      DreamRunRequest request,
      base::Time window_start,
      base::Time window_end,
      std::optional<WeeklyDreamReport> existing);
  void StartWeeklyDream(DreamRunRequest request,
                        base::Time window_start,
                        base::Time window_end,
                        std::optional<WeeklyDreamReport> existing);
  void OnMaterialCollected(const std::string& request_id,
                           base::DictValue material);
  void OnWeeklyMaterialCollected(const std::string& request_id,
                                 WeeklyDreamMaterial material);
  void PersistWeeklySkipped(DreamRunRequest request);
  void PersistWeeklyFailure(DreamRunRequest request,
                            const std::string& manual_error);
  void OnDreamTimeout(const std::string& request_id);
  void DeferWeeklyRunAndFinish(const DreamRunRequest& request,
                               const std::string& manual_error);
  void MarkFailed(DreamRunRequest request, const std::string& error);
  void PersistResult(const std::string& dream_date, base::DictValue result);
  void PersistWeeklyResult(DreamRunRequest request, base::DictValue result);
  void FinishRun(bool success, const std::string& error);

  int GetIdleSeconds() const;
  bool DreamPrefEnabled() const;
  bool WeeklyDreamPrefEnabled() const;

  static constexpr base::TimeDelta kTickInterval = base::Minutes(5);
  static constexpr int kNightlyIdleSeconds = 3600;
  static constexpr int kCatchUpIdleSeconds = 600;
  static constexpr base::TimeDelta kDreamTimeout = base::Minutes(5);
  static constexpr base::TimeDelta kWeeklyTransientDefer = base::Hours(1);
  static constexpr int kMaxAttemptsPerNight = 3;  // 1 initial + 2 retries
  static constexpr double kMaxLLMConfidence = 0.8;

  raw_ptr<Profile> profile_;
  raw_ptr<DaoAgentMemoryService> memory_service_;
  raw_ptr<Runner> runner_ = nullptr;
  raw_ptr<const base::Clock> clock_ = base::DefaultClock::GetInstance();
  base::RepeatingCallback<int()> idle_time_callback_;

  State state_ = State::kIdle;
  std::optional<DreamRunRequest> active_request_;
  std::string pending_debug_material_json_;
  base::DictValue pending_material_stats_;
  bool scheduler_check_in_flight_ = false;
  uint64_t scheduler_check_epoch_ = 0;
  bool chain_weekly_after_run_ = false;
  base::Time weekly_defer_until_;
  std::optional<DreamReport> active_existing_daily_report_;
  std::optional<WeeklyDreamReport> active_existing_weekly_report_;
  std::vector<WeeklyDreamSource> pending_weekly_sources_;
  base::OnceCallback<void(bool success, const std::string& error)>
      manual_callback_;

  base::RepeatingTimer tick_timer_;
  base::OneShotTimer dream_timeout_timer_;
  std::unique_ptr<DreamMaterialCollector> daily_collector_;
  std::unique_ptr<WeeklyDreamMaterialCollector> weekly_collector_;
  base::OnceClosure weekly_collection_started_callback_for_testing_;
  base::RepeatingCallback<void(base::OnceClosure)>
      scheduler_reply_interceptor_for_testing_;

  base::WeakPtrFactory<DaoDreamService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_
