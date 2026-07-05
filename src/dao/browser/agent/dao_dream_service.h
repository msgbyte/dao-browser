// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_

#include <memory>
#include <optional>
#include <string>

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

// Orchestrates the Dream Analysis pipeline: trigger decision (nightly /
// catch-up / manual), material collection, hand-off to the agent WebUI
// for LLM summarization, and persistence of results. The LLM call itself
// happens in the resident agent WebUI (see DaoAgentDreamHandler); this
// service exposes the material pack to it and receives the result.
class DaoDreamService : public KeyedService,
                        public base::PowerSuspendObserver {
 public:
  enum class State { kIdle, kCollecting, kDreaming, kSaving };
  enum class TriggerKind { kNightly, kCatchUp, kManual };

  // The WebUI side registers itself here to receive dream-run requests.
  class Runner {
   public:
    virtual ~Runner() = default;
    // Asks the WebUI to run the LLM summarization for `material`. The
    // runner replies through OnDreamResult / OnDreamFailed.
    virtual void RunDream(const std::string& dream_date,
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
  // Runs one scheduler tick immediately (test convenience).
  void TickForTesting() { OnSchedulerTick(); }

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

  // Result entry points called by the WebUI handler.
  void OnDreamResult(const std::string& dream_date, base::DictValue result);
  void OnDreamFailed(const std::string& dream_date, const std::string& error);

  // Computes the dream-day label ("YYYY-MM-DD", local) for `now`.
  // 22:00–24:00 → today; 00:00–06:00 → yesterday; daytime → today.
  static std::string DreamDateFor(base::Time now);

  // Material window [start, end) for a dream date: date 06:00 local →
  // min(date+1 06:00, now).
  static void MaterialWindowFor(const std::string& dream_date,
                                base::Time now,
                                base::Time* start,
                                base::Time* end);

  // True if local time is within the night window 22:00–06:00.
  static bool IsNightTime(base::Time now);

 private:
  void OnSchedulerTick();
  void MaybeStartNightly();
  void MaybeStartCatchUp();
  void MaybeStartCatchUpForRecentActivity();
  void StartDream(const std::string& dream_date, TriggerKind kind);
  void OnExistingReportChecked(const std::string& dream_date,
                               TriggerKind kind,
                               std::optional<DreamReport> existing);
  void OnMaterialCollected(const std::string& dream_date,
                           TriggerKind kind,
                           base::DictValue material);
  void OnDreamTimeout(const std::string& dream_date);
  void MarkFailed(const std::string& dream_date, const std::string& error);
  void PersistResult(const std::string& dream_date, base::DictValue result);
  void FinishRun(bool success, const std::string& error);

  int GetIdleSeconds() const;
  bool DreamPrefEnabled() const;

  static constexpr base::TimeDelta kTickInterval = base::Minutes(5);
  static constexpr int kNightlyIdleSeconds = 3600;
  static constexpr int kCatchUpIdleSeconds = 600;
  static constexpr base::TimeDelta kDreamTimeout = base::Minutes(5);
  static constexpr int kMaxAttemptsPerNight = 3;  // 1 initial + 2 retries
  static constexpr double kMaxLLMConfidence = 0.8;

  raw_ptr<Profile> profile_;
  raw_ptr<DaoAgentMemoryService> memory_service_;
  raw_ptr<Runner> runner_ = nullptr;
  raw_ptr<const base::Clock> clock_ = base::DefaultClock::GetInstance();
  base::RepeatingCallback<int()> idle_time_callback_;

  State state_ = State::kIdle;
  std::string active_dream_date_;
  TriggerKind active_kind_ = TriggerKind::kNightly;
  std::string pending_debug_material_json_;
  base::DictValue pending_material_stats_;
  bool preserve_completed_report_on_failure_ = false;
  base::OnceCallback<void(bool success, const std::string& error)>
      manual_callback_;

  base::RepeatingTimer tick_timer_;
  base::OneShotTimer dream_timeout_timer_;
  std::unique_ptr<DreamMaterialCollector> collector_;

  base::WeakPtrFactory<DaoDreamService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_SERVICE_H_
