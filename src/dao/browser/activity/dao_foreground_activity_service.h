// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_SERVICE_H_
#define DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_SERVICE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/power_monitor/power_observer.h"
#include "base/scoped_observation.h"
#include "base/threading/sequence_bound.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/activity/dao_foreground_activity_store.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "services/device/public/mojom/time_zone_monitor.mojom.h"
#include "ui/views/widget/widget_observer.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;
class ProfileBrowserCollection;
class TabStripModel;

namespace base {
class Clock;
class TickClock;
}  // namespace base

namespace content {
class Page;
class WebContents;
}  // namespace content

namespace views {
class Widget;
}

namespace dao {

namespace internal {

bool IsEligibleForegroundActivityUrl(const GURL& url);

std::vector<DaoForegroundActivityDelta> SplitForegroundActivityInterval(
    base::Time wall_start,
    base::TimeTicks tick_start,
    base::TimeTicks tick_end,
    const std::string& host);

}  // namespace internal

class DaoForegroundActivityService final
    : public KeyedService,
      public BrowserCollectionObserver,
      public TabStripModelObserver,
      public views::WidgetObserver,
      public content::WebContentsObserver,
      public base::PowerSuspendObserver,
      public device::mojom::TimeZoneMonitorClient {
 public:
  using SnapshotCallback =
      base::OnceCallback<void(DaoForegroundActivitySnapshot)>;

  explicit DaoForegroundActivityService(
      Profile* profile,
      base::Clock* clock = nullptr,
      const base::TickClock* tick_clock = nullptr);
  ~DaoForegroundActivityService() override;

  DaoForegroundActivityService(const DaoForegroundActivityService&) = delete;
  DaoForegroundActivityService& operator=(const DaoForegroundActivityService&) =
      delete;

  void GetSnapshot(std::string start_date,
                   std::string end_date,
                   SnapshotCallback callback);
  void CheckpointForTesting();

  // KeyedService:
  void Shutdown() override;

 private:
  struct Segment {
    std::string host;
    base::Time wall_start;
    base::TimeTicks tick_start;
  };

  struct ObservedBrowser {
    raw_ptr<TabStripModel> tab_strip = nullptr;
    raw_ptr<views::Widget> widget = nullptr;
  };

  struct AggregateKey {
    std::string local_date;
    DaoForegroundActivityBucket bucket;
    std::string host;

    bool operator<(const AggregateKey& other) const;
  };

  void ObserveBrowser(BrowserWindowInterface* browser_window);
  void StopObservingBrowser(BrowserWindowInterface* browser_window);
  content::WebContents* GetSelectedWebContents() const;
  std::optional<std::string> GetEligibleHost(
      content::WebContents* contents) const;
  void Reconcile();
  void ReconcileAt(base::Time wall_now, base::TimeTicks tick_now);
  void Settle(base::TimeTicks tick_now);
  bool AddPending(const DaoForegroundActivityDelta& delta);
  std::vector<DaoForegroundActivityDelta> TakePending();
  void Checkpoint();
  void DisableStore();
  DaoForegroundActivitySnapshot UnavailableSnapshot() const;
  std::string LocalDate(base::Time time) const;

  void OnStoreSchemaInitialized(
      base::expected<void, DaoForegroundActivityStoreError> result);
  void OnStoreInitialized(
      base::expected<DaoForegroundActivitySnapshot,
                     DaoForegroundActivityStoreError> result);
  void OnCheckpointFinished(
      base::expected<DaoForegroundActivitySnapshot,
                     DaoForegroundActivityStoreError> result);
  void OnSnapshotReady(SnapshotCallback callback,
                       base::expected<DaoForegroundActivitySnapshot,
                                      DaoForegroundActivityStoreError> result);

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;
  void OnBrowserActivated(BrowserWindowInterface* browser) override;
  void OnBrowserDeactivated(BrowserWindowInterface* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabStripModelDestroyed(TabStripModel* tab_strip_model) override;

  // views::WidgetObserver:
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetShowStateChanged(views::Widget* widget) override;
  void OnWidgetDestroying(views::Widget* widget) override;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;

  // base::PowerSuspendObserver:
  void OnSuspend() override;
  void OnResume() override;

  // device::mojom::TimeZoneMonitorClient:
  void OnTimeZoneChange(const std::string& tz_info) override;

  const raw_ptr<Profile> profile_;
  const raw_ptr<base::Clock> clock_;
  const raw_ptr<const base::TickClock> tick_clock_;
  const raw_ptr<ProfileBrowserCollection> browser_collection_;

  base::SequenceBound<DaoForegroundActivityStore> store_;
  base::ScopedObservation<ProfileBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  std::map<raw_ptr<BrowserWindowInterface>, ObservedBrowser> observed_browsers_;
  std::set<raw_ptr<TabStripModel>> observed_tab_strips_;
  std::set<raw_ptr<views::Widget>> observed_widgets_;
  mojo::Remote<device::mojom::TimeZoneMonitor> time_zone_monitor_;
  mojo::Receiver<device::mojom::TimeZoneMonitorClient> time_zone_receiver_{
      this};
  base::RepeatingTimer checkpoint_timer_;

  std::optional<Segment> segment_;
  std::map<AggregateKey, int64_t> pending_;
  base::Time tracking_started_at_;
  std::string retained_from_date_;
  bool store_available_ = false;
  bool observing_power_ = false;
  bool suspended_ = false;
  bool shutting_down_ = false;

  base::WeakPtrFactory<DaoForegroundActivityService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_SERVICE_H_
