// Copyright 2026 Dao Browser Authors. All rights reserved.
#ifndef DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_MONITOR_H_
#define DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_MONITOR_H_

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "base/feature_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/power_monitor/power_observer.h"
#include "base/scoped_observation.h"
#include "base/time/default_tick_clock.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "components/performance_manager/public/resource_attribution/page_context.h"
#include "components/performance_manager/public/resource_attribution/queries.h"
#include "components/prefs/pref_change_registrar.h"
#include "dao/browser/performance/dao_resource_usage_notification.h"

namespace base {
template <typename T>
class NoDestructor;
}
namespace content {
class WebContents;
}

class Profile;

namespace dao {
BASE_DECLARE_FEATURE(kDaoResourceUsageAlerts);

class DaoResourceUsageMonitor
    : public resource_attribution::QueryResultObserver,
      public base::PowerSuspendObserver,
      public BrowserCollectionObserver {
 public:
  class Observer : public base::CheckedObserver {
   public:
    virtual void OnResourceUsageChanged(content::WebContents* contents) = 0;
    virtual bool IsResourceAlertMediaPlaying(
        content::WebContents* contents) const;
  };
  static DaoResourceUsageMonitor* Get();
  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);
  int GetAlerts(content::WebContents* contents) const;
  bool ShouldNotify(content::WebContents* contents) const;
  void Dismiss(content::WebContents* contents);
  std::vector<content::WebContents*> GetReminderTabs(
      BrowserWindowInterface* browser) const;
  void InspectReminder(Profile* profile);
  void DismissReminder(Profile* profile);
  void RefreshReminders();
  void OnBrowserActivated(BrowserWindowInterface* browser) override;
  void OnBrowserDeactivated(BrowserWindowInterface* browser) override;
  void OnBrowserClosed(BrowserWindowInterface* browser) override;
  void OnResourceUsageUpdated(
      const resource_attribution::QueryResultMap&) override;
  void OnSuspend() override;
  void OnResume() override;

 private:
  friend class base::NoDestructor<DaoResourceUsageMonitor>;
  friend class DaoResourceUsageMonitorTestApi;
  class Page;
  DaoResourceUsageMonitor();
  ~DaoResourceUsageMonitor() override;
  void UpdateMonitoring();
  void StopMonitoring();
  void Sample();
  void Notify(content::WebContents* contents);
  Page* FindPage(content::WebContents* contents) const;
  bool IsBackground(content::WebContents* contents) const;
  bool IsExcluded(content::WebContents* contents) const;
  bool IsEligible(Page& page) const;
  void Reconcile(bool window_changed = false);
  void UpdateNotifications();
  std::vector<content::WebContents*> GetReminderTabsForProfile(
      Profile* profile) const;
  BrowserWindowInterface* GetReminderWindow(Profile* profile) const;
  struct NotificationState {
    base::WeakPtr<Profile> profile;
    bool visible = true;
  };
  void CloseNotification(NotificationState& notification);

  std::map<resource_attribution::PageContext, std::unique_ptr<Page>> pages_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_observation_{this};
  std::map<Profile*, NotificationState> notifications_;
  bool reconciling_ = false;
  std::optional<resource_attribution::ScopedResourceUsageQuery> cpu_query_;
  std::optional<resource_attribution::ScopedResourceUsageQuery> memory_query_;
  base::RepeatingTimer timer_;
  PrefChangeRegistrar pref_change_registrar_;
  raw_ptr<const base::TickClock> tick_clock_ =
      base::DefaultTickClock::GetInstance();
  base::TimeTicks monitoring_started_;
  base::TimeTicks app_background_since_;
  base::TimeTicks last_cpu_sample_;
  base::TimeTicks last_memory_sample_;
  bool memory_pending_ = false;
  base::TimeTicks memory_request_time_;
};
}  // namespace dao
#endif  // DAO_BROWSER_PERFORMANCE_DAO_RESOURCE_USAGE_MONITOR_H_
