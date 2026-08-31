// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/activity/dao_foreground_activity_service.h"

#include <algorithm>
#include <array>
#include <limits>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/power_monitor/power_monitor.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "base/time/default_clock.h"
#include "base/time/default_tick_clock.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/device_service.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

constexpr base::TimeDelta kCheckpointInterval = base::Minutes(1);
constexpr std::array<int, 4> kBucketBoundaries = {6, 12, 18, 22};

DaoForegroundActivityBucket BucketForTime(base::Time time) {
  base::Time::Exploded local;
  time.LocalExplode(&local);
  if (local.hour >= 6 && local.hour < 12) {
    return DaoForegroundActivityBucket::kMorning;
  }
  if (local.hour >= 12 && local.hour < 18) {
    return DaoForegroundActivityBucket::kAfternoon;
  }
  if (local.hour >= 18 && local.hour < 22) {
    return DaoForegroundActivityBucket::kEvening;
  }
  return local.hour < 6 ? DaoForegroundActivityBucket::kNightBeforeMorning
                        : DaoForegroundActivityBucket::kNightAfterEvening;
}

std::optional<base::Time> LocalTimeForDate(base::Time::Exploded date,
                                           int hour) {
  date.hour = hour;
  date.minute = 0;
  date.second = 0;
  date.millisecond = 0;
  base::Time result;
  if (!base::Time::FromLocalExploded(date, &result)) {
    return std::nullopt;
  }
  return result;
}

std::optional<base::Time> NextLocalBoundary(base::Time time) {
  base::Time::Exploded local;
  time.LocalExplode(&local);
  for (int hour : kBucketBoundaries) {
    std::optional<base::Time> candidate = LocalTimeForDate(local, hour);
    if (candidate && *candidate > time) {
      return candidate;
    }
  }

  base::Time::Exploded calendar = local;
  calendar.hour = 0;
  calendar.minute = 0;
  calendar.second = 0;
  calendar.millisecond = 0;
  base::Time calendar_time;
  if (!base::Time::FromUTCExploded(calendar, &calendar_time)) {
    return std::nullopt;
  }
  calendar_time += base::Days(1);
  calendar_time.UTCExplode(&calendar);
  return LocalTimeForDate(calendar, 0);
}

}  // namespace

namespace internal {

bool IsEligibleForegroundActivityUrl(const GURL& url) {
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.host().empty();
}

std::vector<DaoForegroundActivityDelta> SplitForegroundActivityInterval(
    base::Time wall_start,
    base::TimeTicks tick_start,
    base::TimeTicks tick_end,
    const std::string& host) {
  std::vector<DaoForegroundActivityDelta> slices;
  int64_t remaining_ms = (tick_end - tick_start).InMilliseconds();
  if (wall_start.is_null() || remaining_ms <= 0 || host.empty()) {
    return slices;
  }

  base::Time cursor = wall_start;
  while (remaining_ms > 0) {
    int64_t slice_ms = remaining_ms;
    if (std::optional<base::Time> boundary = NextLocalBoundary(cursor)) {
      const int64_t until_boundary_ms = (*boundary - cursor).InMilliseconds();
      if (until_boundary_ms > 0) {
        slice_ms = std::min(slice_ms, until_boundary_ms);
      }
    }

    base::Time::Exploded local;
    cursor.LocalExplode(&local);
    slices.push_back({base::StringPrintf("%04d-%02d-%02d", local.year,
                                         local.month, local.day_of_month),
                      BucketForTime(cursor), host, slice_ms});
    cursor += base::Milliseconds(slice_ms);
    remaining_ms -= slice_ms;
  }
  return slices;
}

}  // namespace internal

bool DaoForegroundActivityService::AggregateKey::operator<(
    const AggregateKey& other) const {
  return std::tie(local_date, bucket, host) <
         std::tie(other.local_date, other.bucket, other.host);
}

DaoForegroundActivityService::DaoForegroundActivityService(
    Profile* profile,
    base::Clock* clock,
    const base::TickClock* tick_clock)
    : profile_(profile),
      clock_(clock ? clock : base::DefaultClock::GetInstance()),
      tick_clock_(tick_clock ? tick_clock
                             : base::DefaultTickClock::GetInstance()),
      browser_collection_(ProfileBrowserCollection::GetForProfile(profile)),
      store_(base::ThreadPool::CreateSequencedTaskRunner(
                 {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
                  base::TaskShutdownBehavior::BLOCK_SHUTDOWN}),
             profile->GetPath()) {
  if (browser_collection_) {
    browser_collection_observation_.Observe(browser_collection_);
    browser_collection_->ForEach(
        [this](BrowserWindowInterface* browser_window) {
          ObserveBrowser(browser_window);
          return true;
        });
  }

  base::PowerMonitor* power_monitor = base::PowerMonitor::GetInstance();
  suspended_ =
      power_monitor->AddPowerSuspendObserverAndReturnSuspendedState(this);
  observing_power_ = true;

  content::GetDeviceService().BindTimeZoneMonitor(
      time_zone_monitor_.BindNewPipeAndPassReceiver());
  time_zone_monitor_->AddClient(time_zone_receiver_.BindNewPipeAndPassRemote());

  store_.AsyncCall(&DaoForegroundActivityStore::Initialize)
      .Then(base::BindOnce(
          &DaoForegroundActivityService::OnStoreSchemaInitialized,
          weak_factory_.GetWeakPtr()));
}

DaoForegroundActivityService::~DaoForegroundActivityService() {
  Shutdown();
}

void DaoForegroundActivityService::GetSnapshot(std::string start_date,
                                               std::string end_date,
                                               SnapshotCallback callback) {
  if (!store_available_ || shutting_down_) {
    std::move(callback).Run(UnavailableSnapshot());
    return;
  }

  Reconcile();
  const std::string today = LocalDate(clock_->Now());
  store_.AsyncCall(&DaoForegroundActivityStore::ApplyDeltasAndQuery)
      .WithArgs(std::move(start_date), std::move(end_date), today,
                TakePending())
      .Then(base::BindOnce(
          [](base::WeakPtr<DaoForegroundActivityService> service,
             SnapshotCallback callback,
             base::expected<DaoForegroundActivitySnapshot,
                            DaoForegroundActivityStoreError> result) {
            if (!service) {
              std::move(callback).Run(DaoForegroundActivitySnapshot());
              return;
            }
            service->OnSnapshotReady(std::move(callback), std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::move(callback)));
}

void DaoForegroundActivityService::CheckpointForTesting() {
  Checkpoint();
}

void DaoForegroundActivityService::Shutdown() {
  if (shutting_down_) {
    return;
  }
  shutting_down_ = true;
  checkpoint_timer_.Stop();
  Observe(nullptr);

  browser_collection_observation_.Reset();
  for (TabStripModel* tab_strip : observed_tab_strips_) {
    tab_strip->RemoveObserver(this);
  }
  observed_tab_strips_.clear();
  for (views::Widget* widget : observed_widgets_) {
    widget->RemoveObserver(this);
  }
  observed_widgets_.clear();
  observed_browsers_.clear();

  if (observing_power_) {
    base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
    observing_power_ = false;
  }
  time_zone_receiver_.reset();
  time_zone_monitor_.reset();

  if (store_available_) {
    Settle(tick_clock_->NowTicks());
    const std::string today = LocalDate(clock_->Now());
    store_.AsyncCall(&DaoForegroundActivityStore::ApplyDeltasAndQuery)
        .WithArgs(today, today, today, TakePending())
        .Then(base::BindOnce(
            [](base::expected<DaoForegroundActivitySnapshot,
                              DaoForegroundActivityStoreError>) {}));
  }
  segment_.reset();
  store_.Reset();
  store_available_ = false;
  weak_factory_.InvalidateWeakPtrs();
}

void DaoForegroundActivityService::ObserveBrowser(
    BrowserWindowInterface* browser_window) {
  if (!browser_window || browser_window->GetProfile() != profile_) {
    return;
  }
  Browser* browser = browser_window->GetBrowserForMigrationOnly();
  if (!browser) {
    return;
  }
  TabStripModel* tab_strip = browser->tab_strip_model();
  if (tab_strip && observed_tab_strips_.insert(tab_strip).second) {
    tab_strip->AddObserver(this);
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  views::Widget* widget = browser_view ? browser_view->GetWidget() : nullptr;
  if (widget && observed_widgets_.insert(widget).second) {
    widget->AddObserver(this);
  }
  observed_browsers_.insert_or_assign(browser_window,
                                      ObservedBrowser{tab_strip, widget});
}

void DaoForegroundActivityService::StopObservingBrowser(
    BrowserWindowInterface* browser_window) {
  if (!browser_window) {
    return;
  }
  auto observed = observed_browsers_.find(browser_window);
  if (observed == observed_browsers_.end()) {
    return;
  }
  TabStripModel* tab_strip = observed->second.tab_strip;
  if (tab_strip && observed_tab_strips_.erase(tab_strip)) {
    tab_strip->RemoveObserver(this);
  }
  views::Widget* widget = observed->second.widget;
  if (widget && observed_widgets_.erase(widget)) {
    widget->RemoveObserver(this);
  }
  observed_browsers_.erase(observed);
}

content::WebContents* DaoForegroundActivityService::GetSelectedWebContents()
    const {
  BrowserWindowInterface* browser_window =
      browser_collection_ ? browser_collection_->GetLastActiveBrowser()
                          : nullptr;
  auto observed = observed_browsers_.find(browser_window);
  if (!browser_window || observed == observed_browsers_.end() ||
      !observed->second.tab_strip || !observed->second.widget) {
    return nullptr;
  }
  if (browser_window->GetProfile() != profile_) {
    return nullptr;
  }
  Browser* browser = browser_window->GetBrowserForMigrationOnly();
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view ? browser_view->GetActiveWebContents() : nullptr;
}

std::optional<std::string> DaoForegroundActivityService::GetEligibleHost(
    content::WebContents* contents) const {
  if (!contents ||
      Profile::FromBrowserContext(contents->GetBrowserContext()) != profile_ ||
      !CanAnimateAgentCursorForTarget(contents)) {
    return std::nullopt;
  }
  const GURL& url = contents->GetLastCommittedURL();
  if (!internal::IsEligibleForegroundActivityUrl(url)) {
    return std::nullopt;
  }
  return std::string(url.host());
}

void DaoForegroundActivityService::Reconcile() {
  ReconcileAt(clock_->Now(), tick_clock_->NowTicks());
}

void DaoForegroundActivityService::ReconcileAt(base::Time wall_now,
                                               base::TimeTicks tick_now) {
  if (store_available_) {
    Settle(tick_now);
  } else {
    segment_.reset();
  }

  content::WebContents* selected =
      suspended_ || shutting_down_ ? nullptr : GetSelectedWebContents();
  if (web_contents() != selected) {
    Observe(selected);
  }
  if (!store_available_ || !selected) {
    return;
  }
  if (std::optional<std::string> host = GetEligibleHost(selected)) {
    segment_ = Segment{std::move(*host), wall_now, tick_now};
  }
}

void DaoForegroundActivityService::Settle(base::TimeTicks tick_now) {
  if (!segment_) {
    return;
  }
  Segment segment = std::move(*segment_);
  segment_.reset();
  for (const DaoForegroundActivityDelta& delta :
       internal::SplitForegroundActivityInterval(
           segment.wall_start, segment.tick_start, tick_now, segment.host)) {
    if (!AddPending(delta)) {
      return;
    }
  }
}

bool DaoForegroundActivityService::AddPending(
    const DaoForegroundActivityDelta& delta) {
  if (delta.foreground_ms <= 0) {
    return true;
  }
  AggregateKey key{delta.local_date, delta.bucket, delta.host};
  int64_t& existing = pending_[std::move(key)];
  if (existing > std::numeric_limits<int64_t>::max() - delta.foreground_ms) {
    DisableStore();
    return false;
  }
  existing += delta.foreground_ms;
  return true;
}

std::vector<DaoForegroundActivityDelta>
DaoForegroundActivityService::TakePending() {
  std::vector<DaoForegroundActivityDelta> deltas;
  deltas.reserve(pending_.size());
  for (const auto& [key, foreground_ms] : pending_) {
    deltas.push_back({key.local_date, key.bucket, key.host, foreground_ms});
  }
  pending_.clear();
  return deltas;
}

void DaoForegroundActivityService::Checkpoint() {
  if (!store_available_ || shutting_down_) {
    return;
  }
  Reconcile();
  const std::string today = LocalDate(clock_->Now());
  store_.AsyncCall(&DaoForegroundActivityStore::ApplyDeltasAndQuery)
      .WithArgs(today, today, today, TakePending())
      .Then(base::BindOnce(&DaoForegroundActivityService::OnCheckpointFinished,
                           weak_factory_.GetWeakPtr()));
}

void DaoForegroundActivityService::DisableStore() {
  store_available_ = false;
  checkpoint_timer_.Stop();
  segment_.reset();
  pending_.clear();
}

DaoForegroundActivitySnapshot
DaoForegroundActivityService::UnavailableSnapshot() const {
  DaoForegroundActivitySnapshot snapshot;
  snapshot.tracking_started_at = tracking_started_at_;
  snapshot.retained_from_date = retained_from_date_;
  return snapshot;
}

std::string DaoForegroundActivityService::LocalDate(base::Time time) const {
  base::Time::Exploded local;
  time.LocalExplode(&local);
  return base::StringPrintf("%04d-%02d-%02d", local.year, local.month,
                            local.day_of_month);
}

void DaoForegroundActivityService::OnStoreInitialized(
    base::expected<DaoForegroundActivitySnapshot,
                   DaoForegroundActivityStoreError> result) {
  if (shutting_down_) {
    return;
  }
  if (!result.has_value()) {
    DisableStore();
    return;
  }
  tracking_started_at_ = result->tracking_started_at;
  retained_from_date_ = result->retained_from_date;
  store_available_ = true;
  checkpoint_timer_.Start(FROM_HERE, kCheckpointInterval, this,
                          &DaoForegroundActivityService::Checkpoint);
  Reconcile();
}

void DaoForegroundActivityService::OnStoreSchemaInitialized(
    base::expected<void, DaoForegroundActivityStoreError> result) {
  if (shutting_down_) {
    return;
  }
  if (!result.has_value()) {
    DisableStore();
    return;
  }
  const base::Time wall_now = clock_->Now();
  const base::TimeTicks tick_now = tick_clock_->NowTicks();
  store_.AsyncCall(&DaoForegroundActivityStore::StartTracking)
      .WithArgs(wall_now)
      .Then(base::BindOnce(&DaoForegroundActivityService::OnStoreInitialized,
                           weak_factory_.GetWeakPtr()));
  store_available_ = true;
  ReconcileAt(wall_now, tick_now);
}

void DaoForegroundActivityService::OnCheckpointFinished(
    base::expected<DaoForegroundActivitySnapshot,
                   DaoForegroundActivityStoreError> result) {
  if (!result.has_value()) {
    DisableStore();
    return;
  }
  tracking_started_at_ = result->tracking_started_at;
  retained_from_date_ = result->retained_from_date;
}

void DaoForegroundActivityService::OnSnapshotReady(
    SnapshotCallback callback,
    base::expected<DaoForegroundActivitySnapshot,
                   DaoForegroundActivityStoreError> result) {
  if (!result.has_value()) {
    DisableStore();
    std::move(callback).Run(UnavailableSnapshot());
    return;
  }
  tracking_started_at_ = result->tracking_started_at;
  retained_from_date_ = result->retained_from_date;
  std::move(callback).Run(std::move(result).value());
}

void DaoForegroundActivityService::OnBrowserCreated(
    BrowserWindowInterface* browser) {
  ObserveBrowser(browser);
  Reconcile();
}

void DaoForegroundActivityService::OnBrowserClosed(
    BrowserWindowInterface* browser) {
  StopObservingBrowser(browser);
  Reconcile();
}

void DaoForegroundActivityService::OnBrowserActivated(
    BrowserWindowInterface* browser) {
  Reconcile();
}

void DaoForegroundActivityService::OnBrowserDeactivated(
    BrowserWindowInterface* browser) {
  Reconcile();
}

void DaoForegroundActivityService::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  Reconcile();
}

void DaoForegroundActivityService::OnTabStripModelDestroyed(
    TabStripModel* tab_strip_model) {
  if (store_available_) {
    Settle(tick_clock_->NowTicks());
  }
  segment_.reset();
  Observe(nullptr);
  observed_tab_strips_.erase(tab_strip_model);
  for (auto& entry : observed_browsers_) {
    if (entry.second.tab_strip == tab_strip_model) {
      entry.second.tab_strip = nullptr;
    }
  }
  Reconcile();
}

void DaoForegroundActivityService::OnWidgetVisibilityChanged(
    views::Widget* widget,
    bool visible) {
  Reconcile();
}

void DaoForegroundActivityService::OnWidgetActivationChanged(
    views::Widget* widget,
    bool active) {
  Reconcile();
}

void DaoForegroundActivityService::OnWidgetShowStateChanged(
    views::Widget* widget) {
  Reconcile();
}

void DaoForegroundActivityService::OnWidgetDestroying(views::Widget* widget) {
  if (store_available_) {
    Settle(tick_clock_->NowTicks());
  }
  segment_.reset();
  Observe(nullptr);
  observed_widgets_.erase(widget);
  for (auto& entry : observed_browsers_) {
    if (entry.second.widget == widget) {
      entry.second.widget = nullptr;
    }
  }
  Reconcile();
}

void DaoForegroundActivityService::PrimaryPageChanged(content::Page& page) {
  Reconcile();
}

void DaoForegroundActivityService::OnSuspend() {
  if (suspended_ || shutting_down_) {
    return;
  }
  Settle(tick_clock_->NowTicks());
  suspended_ = true;
  segment_.reset();
}

void DaoForegroundActivityService::OnResume() {
  if (!suspended_ || shutting_down_) {
    return;
  }
  suspended_ = false;
  Reconcile();
}

void DaoForegroundActivityService::OnTimeZoneChange(
    const std::string& tz_info) {
  Reconcile();
}

}  // namespace dao
