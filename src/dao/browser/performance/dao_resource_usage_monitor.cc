// Copyright 2026 Dao Browser Authors. All rights reserved.
#include "dao/browser/performance/dao_resource_usage_monitor.h"

#include <algorithm>
#include <set>

#include "base/auto_reset.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/notifications/notification_display_service.h"
#include "chrome/browser/notifications/notification_display_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/dialogs/browser_dialogs.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/media_session.h"
#include "dao/browser/agent_space/dao_hidden_browser.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/message_center/public/cpp/notification.h"

#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/power_monitor/power_monitor.h"
#include "base/system/sys_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/media/webrtc/media_capture_devices_dispatcher.h"
#include "chrome/browser/media/webrtc/media_stream_capture_indicator.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/performance/dao_high_performance_mode.h"
#include "dao/browser/performance/dao_resource_usage_state.h"

namespace dao {
BASE_FEATURE(kDaoResourceUsageAlerts, base::FEATURE_ENABLED_BY_DEFAULT);
namespace {
using resource_attribution::PageContext;
using resource_attribution::QueryBuilder;
using resource_attribution::ResourceType;
constexpr char kNotificationId[] = "dao-resource-usage";
class ResourceUsageNotificationHandler : public NotificationHandler {
 public:
  void OnClick(Profile* profile,
               const GURL& origin,
               const std::string& notification_id,
               const std::optional<int>& action_index,
               const std::optional<std::u16string>& reply,
               base::OnceClosure completed_closure) override {
    if (profile && notification_id == kNotificationId) {
      NotificationDisplayServiceFactory::GetForProfile(profile)->Close(
          kDaoResourceUsageNotificationType, notification_id);
      DaoResourceUsageMonitor::Get()->InspectReminder(profile);
    }
    std::move(completed_closure).Run();
  }
};

constexpr auto kInterval = base::Seconds(30);
constexpr const char* kEnabledPref = performance_manager::user_tuning::prefs::
    kPerformanceInterventionNotificationEnabled;
}  // namespace

std::unique_ptr<NotificationHandler> CreateResourceUsageNotificationHandler() {
  return std::make_unique<ResourceUsageNotificationHandler>();
}

class DaoResourceUsageMonitor::Page : public content::WebContentsObserver {
 public:
  Page(DaoResourceUsageMonitor* monitor, content::WebContents* contents)
      : content::WebContentsObserver(contents),
        background_since(monitor->tick_clock_->NowTicks()),
        background(monitor->IsBackground(contents)),
        monitor_(monitor) {}
  void PrimaryPageChanged(content::Page&) override {
    state = {};
    retained = false;
    background_since = monitor_->tick_clock_->NowTicks();
    document_started = monitor_->tick_clock_->NowTicks();
    monitor_->Reconcile();
  }
  void OnVisibilityChanged(content::Visibility) override {
    monitor_->Reconcile();
  }
  void WebContentsDestroyed() override {
    state = {};
    monitor_->Reconcile();
  }
  DaoResourceUsageState state;
  base::TimeTicks background_since;
  base::TimeTicks document_started;
  int published_alerts = 0;
  bool retained = false;
  bool reminder_started = false;
  bool background;

 private:
  raw_ptr<DaoResourceUsageMonitor> monitor_;
};

bool DaoResourceUsageMonitor::Observer::IsResourceAlertMediaPlaying(
    content::WebContents*) const {
  return false;
}

DaoResourceUsageMonitor::DaoResourceUsageMonitor() = default;
DaoResourceUsageMonitor::~DaoResourceUsageMonitor() = default;

DaoResourceUsageMonitor* DaoResourceUsageMonitor::Get() {
  static base::NoDestructor<DaoResourceUsageMonitor> monitor;
  return monitor.get();
}

void DaoResourceUsageMonitor::AddObserver(Observer* observer) {
  const bool first_observer = observers_.empty();
  observers_.AddObserver(observer);
  if (first_observer) {
    pref_change_registrar_.Init(g_browser_process->local_state());
    pref_change_registrar_.Add(
        kEnabledPref,
        base::BindRepeating(&DaoResourceUsageMonitor::UpdateMonitoring,
                            base::Unretained(this)));
  }
  UpdateMonitoring();
}

void DaoResourceUsageMonitor::UpdateMonitoring() {
  if (!base::FeatureList::IsEnabled(kDaoResourceUsageAlerts) ||
      !g_browser_process->local_state()->GetBoolean(kEnabledPref) ||
      !performance_manager::PerformanceManager::IsAvailable()) {
    const bool was_monitoring = cpu_query_.has_value();
    StopMonitoring();
    if (was_monitoring) {
      Notify(nullptr);
    }
    return;
  }
  if (cpu_query_ || observers_.empty()) {
    return;
  }
  monitoring_started_ = tick_clock_->NowTicks();
  cpu_query_.emplace(QueryBuilder()
                         .AddAllContextsOfType<PageContext>()
                         .AddResourceType(ResourceType::kCPUTime)
                         .CreateScopedQuery());
  memory_query_.emplace(QueryBuilder()
                            .AddAllContextsOfType<PageContext>()
                            .AddResourceType(ResourceType::kMemorySummary)
                            .CreateScopedQuery());
  cpu_query_->AddObserver(this);
  memory_query_->AddObserver(this);
  // Observe other clients without starting duplicate resource-query timers.
  cpu_query_->Start(base::TimeDelta(), true);
  memory_query_->Start(base::TimeDelta(), true);
  browser_observation_.Observe(GlobalBrowserCollection::GetInstance());
  base::PowerMonitor::GetInstance()->AddPowerSuspendObserver(this);
  timer_.Start(FROM_HERE, kInterval, this, &DaoResourceUsageMonitor::Sample);
}

void DaoResourceUsageMonitor::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
  if (!observers_.empty()) {
    return;
  }
  pref_change_registrar_.RemoveAll();
  StopMonitoring();
}

void DaoResourceUsageMonitor::StopMonitoring() {
  timer_.Stop();
  browser_observation_.Reset();
  for (auto& [profile, notification] : notifications_)
    CloseNotification(notification);
  notifications_.clear();
  if (cpu_query_) {
    base::PowerMonitor::GetInstance()->RemovePowerSuspendObserver(this);
    // In-flight queries retain their observer lists beyond query destruction.
    cpu_query_->RemoveObserver(this);
    memory_query_->RemoveObserver(this);
  }
  cpu_query_.reset();
  memory_query_.reset();
  pages_.clear();
  last_cpu_sample_ = last_memory_sample_ = {};
  monitoring_started_ = {};
  app_background_since_ = {};
  memory_pending_ = false;
}

DaoResourceUsageMonitor::Page* DaoResourceUsageMonitor::FindPage(
    content::WebContents* contents) const {
  auto context = PageContext::FromWebContents(contents);
  if (!context)
    return nullptr;
  auto it = pages_.find(*context);
  return it == pages_.end() ? nullptr : it->second.get();
}

int DaoResourceUsageMonitor::GetAlerts(content::WebContents* contents) const {
  Page* page = FindPage(contents);
  return page ? page->state.alerts() : 0;
}

bool DaoResourceUsageMonitor::IsBackground(
    content::WebContents* contents) const {
  auto* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  auto* browser = tab ? tab->GetBrowserWindowInterface() : nullptr;
  auto* window = browser ? browser->GetWindow() : nullptr;
  return !window || !browser->IsActive() || !window->IsActive() ||
         !window->IsVisible() || window->IsMinimized() ||
         contents->GetVisibility() != content::Visibility::VISIBLE;
}

bool DaoResourceUsageMonitor::IsExcluded(content::WebContents* contents) const {
  auto* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  auto* browser = tab ? tab->GetBrowserWindowInterface() : nullptr;
  if (!browser || IsDaoHiddenBrowser(browser))
    return true;
  if (contents->IsCurrentlyAudible() || contents->IsBeingCaptured() ||
      contents->GetCurrentlyPlayingVideoCount() > 0 ||
      MediaCaptureDevicesDispatcher::GetInstance()
          ->GetMediaStreamCaptureIndicator()
          ->IsCapturingUserMedia(contents) ||
      DaoHighPerformanceMode::IsEnabled(contents))
    return true;
  if (auto* session = content::MediaSession::GetIfExists(contents);
      session && session->GetMediaSessionInfoSync()->playback_state ==
                     media_session::mojom::MediaPlaybackState::kPlaying)
    return true;
  // The sidebar also tracks muted players removed from Chromium's MediaSession.
  for (const auto& observer : observers_) {
    if (observer.IsResourceAlertMediaPlaying(contents))
      return true;
  }
  return false;
}

bool DaoResourceUsageMonitor::IsEligible(Page& page) const {
  auto* contents = page.web_contents();
  return contents && page.state.alerts() && !page.state.dismissed() &&
         page.background &&
         tick_clock_->NowTicks() - page.background_since >= base::Minutes(1) &&
         !IsExcluded(contents);
}

bool DaoResourceUsageMonitor::ShouldNotify(
    content::WebContents* contents) const {
  Page* page = FindPage(contents);
  return page && page->retained;
}

void DaoResourceUsageMonitor::Dismiss(content::WebContents* contents) {
  if (Page* page = FindPage(contents)) {
    page->state.Dismiss();
    Reconcile();
  }
}

BrowserWindowInterface* DaoResourceUsageMonitor::GetReminderWindow(
    Profile* profile) const {
  BrowserWindowInterface* match = nullptr;
  ProfileBrowserCollection::GetForProfile(profile)->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetType() != BrowserWindowInterface::TYPE_NORMAL ||
            browser->IsDeleteScheduled() || IsDaoHiddenBrowser(browser))
          return true;
        match = browser;
        return false;
      },
      BrowserCollection::Order::kActivation);
  return match;
}

std::vector<content::WebContents*>
DaoResourceUsageMonitor::GetReminderTabsForProfile(Profile* profile) const {
  std::vector<content::WebContents*> tabs;
  for (const auto& [context, page] : pages_) {
    auto* contents = page->web_contents();
    if (contents && page->retained &&
        contents->GetBrowserContext() == profile &&
        tabs::TabInterface::MaybeGetFromContents(contents))
      tabs.push_back(contents);
  }
  return tabs;
}

std::vector<content::WebContents*> DaoResourceUsageMonitor::GetReminderTabs(
    BrowserWindowInterface* browser) const {
  return browser == GetReminderWindow(browser->GetProfile())
             ? GetReminderTabsForProfile(browser->GetProfile())
             : std::vector<content::WebContents*>();
}

void DaoResourceUsageMonitor::DismissReminder(Profile* profile) {
  for (auto* contents : GetReminderTabsForProfile(profile)) {
    FindPage(contents)->state.Dismiss();
  }
  Reconcile();
}

void DaoResourceUsageMonitor::InspectReminder(Profile* profile) {
  // Eligibility can change after the notification was delivered.
  Reconcile();
  auto tabs = GetReminderTabsForProfile(profile);
  if (tabs.empty())
    return;
  auto* browser = GetReminderWindow(profile);
  if (!browser)
    return;
  if (tabs.size() == 1) {
    auto* tab = tabs::TabInterface::MaybeGetFromContents(tabs.front());
    browser = tab ? tab->GetBrowserWindowInterface() : nullptr;
    if (!browser)
      return;
    const int index =
        browser->tab_strip_model()->GetIndexOfWebContents(tabs.front());
    if (index < 0)
      return;
    DismissReminder(profile);
    browser->tab_strip_model()->ActivateTabAt(index);
    if (browser->GetWindow()->IsMinimized())
      browser->GetWindow()->Restore();
    browser->GetWindow()->Show();
    browser->GetWindow()->Activate();
  } else {
    DismissReminder(profile);
    if (browser->GetWindow()->IsMinimized())
      browser->GetWindow()->Restore();
    browser->GetWindow()->Show();
    browser->GetWindow()->Activate();
    chrome::ShowTaskManager(browser);
  }
}

void DaoResourceUsageMonitor::CloseNotification(
    NotificationState& notification) {
  if (notification.profile && notification.visible) {
    NotificationDisplayServiceFactory::GetForProfile(notification.profile.get())
        ->Close(kDaoResourceUsageNotificationType, kNotificationId);
  }
  notification.visible = false;
}

void DaoResourceUsageMonitor::UpdateNotifications() {
  std::set<Profile*> ongoing;
  std::map<Profile*, size_t> counts;
  for (const auto& [context, page] : pages_) {
    auto* contents = page->web_contents();
    if (!contents || !page->reminder_started)
      continue;
    auto* profile = Profile::FromBrowserContext(contents->GetBrowserContext());
    ongoing.insert(profile);
    if (page->retained)
      ++counts[profile];
  }
  for (auto it = notifications_.begin(); it != notifications_.end();) {
    if (!it->second.profile || !ongoing.contains(it->first)) {
      CloseNotification(it->second);
      it = notifications_.erase(it);
    } else {
      if (!counts.contains(it->first))
        CloseNotification(it->second);
      ++it;
    }
  }
  bool active = false;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        auto* window = browser->GetWindow();
        active |= window && browser->IsActive() && window->IsActive() &&
                  window->IsVisible() && !window->IsMinimized();
        return !active;
      });
  if (active) {
    app_background_since_ = {};
    return;
  }
  if (app_background_since_.is_null())
    app_background_since_ = tick_clock_->NowTicks();
  // Deactivation precedes activation when switching between Dao windows.
  if (tick_clock_->NowTicks() - app_background_since_ < base::Seconds(10))
    return;
  for (const auto& [profile, count] : counts) {
    if (notifications_.contains(profile) || !GetReminderWindow(profile))
      continue;
    notifications_.emplace(profile, NotificationState{profile->GetWeakPtr()});
    message_center::Notification notification(
        message_center::NOTIFICATION_TYPE_SIMPLE, kNotificationId,
        l10n_util::GetStringUTF16(IDS_DAO_RESOURCE_NOTIFICATION_TITLE),
        l10n_util::GetStringFUTF16(IDS_DAO_RESOURCE_SUMMARY,
                                   base::NumberToString16(count)),
        ui::ImageModel(), std::u16string(), GURL(),
        message_center::NotifierId(
            message_center::NotifierType::SYSTEM_COMPONENT, kNotificationId),
        message_center::RichNotificationData(),
        /*delegate=*/nullptr);
    NotificationDisplayServiceFactory::GetForProfile(profile)->Display(
        kDaoResourceUsageNotificationType, notification, nullptr);
  }
}

void DaoResourceUsageMonitor::RefreshReminders() {
  Reconcile();
}

void DaoResourceUsageMonitor::OnBrowserActivated(BrowserWindowInterface*) {
  Reconcile(true);
}

void DaoResourceUsageMonitor::OnBrowserDeactivated(BrowserWindowInterface*) {
  Reconcile(true);
}

void DaoResourceUsageMonitor::OnBrowserClosed(BrowserWindowInterface*) {
  Reconcile(true);
}

void DaoResourceUsageMonitor::Reconcile(bool window_changed) {
  if (!cpu_query_ || reconciling_)
    return;
  base::AutoReset<bool> guard(&reconciling_, true);
  bool changed = window_changed;
  for (auto& [context, page] : pages_) {
    auto* contents = page->web_contents();
    const bool background = contents && IsBackground(contents);
    if (background != page->background) {
      page->background = background;
      page->background_since = tick_clock_->NowTicks();
    }
    const int alerts = page->state.alerts();
    const bool retained = contents && alerts && !page->state.dismissed() &&
                          !IsExcluded(contents) &&
                          (page->retained || IsEligible(*page));
    changed |= page->published_alerts != alerts || page->retained != retained;
    page->published_alerts = alerts;
    page->retained = retained;
    // Keep deduplication through temporary media suppression of this incident.
    page->reminder_started = alerts && !page->state.dismissed() &&
                             (page->reminder_started || retained);
  }
  UpdateNotifications();
  if (changed)
    Notify(nullptr);
}

void DaoResourceUsageMonitor::Notify(content::WebContents* contents) {
  for (auto& observer : observers_)
    observer.OnResourceUsageChanged(contents);
}

void DaoResourceUsageMonitor::Sample() {
  const auto now = tick_clock_->NowTicks();
  for (auto it = pages_.begin(); it != pages_.end();) {
    if (!it->first.GetWebContents()) {
      it = pages_.erase(it);
    } else {
      it->second->state.Expire(now);
      ++it;
    }
  }
  Reconcile();
  if (last_cpu_sample_.is_null() ||
      now - last_cpu_sample_ >= kInterval - base::Seconds(1)) {
    cpu_query_->QueryOnce();
  }
  if ((!memory_pending_ || now - memory_request_time_ >= base::Minutes(5)) &&
      (last_memory_sample_.is_null() ||
       now - last_memory_sample_ >= base::Minutes(2))) {
    memory_pending_ = true;
    memory_request_time_ = now;
    memory_query_->QueryOnce();
  }
}

void DaoResourceUsageMonitor::OnResourceUsageUpdated(
    const resource_attribution::QueryResultMap& results) {
  if (!cpu_query_) {
    return;
  }
  static const auto threshold =
      base::SysInfo::AmountOfTotalPhysicalMemory() <= base::GiB(8)
          ? base::GiB(1)
          : base::GiB(2);
  for (const auto& [context, result] : results) {
    auto page_context = resource_attribution::AsContext<PageContext>(context);
    auto* contents = page_context.GetWebContents();
    if (!contents || !tabs::TabInterface::MaybeGetFromContents(contents) ||
        !contents->GetPrimaryMainFrame()->IsRenderFrameLive())
      continue;
    auto& page = pages_[page_context];
    if (!page)
      page = std::make_unique<Page>(this, contents);
    if (const auto& cpu = result.cpu_time_result) {
      last_cpu_sample_ =
          std::max(last_cpu_sample_, cpu->metadata.measurement_time);
      if (cpu->metadata.measurement_time >
          std::max(page->document_started, monitoring_started_)) {
        page->state.UpdateCpu(cpu->metadata.measurement_time,
                              cpu->cumulative_cpu);
      }
    }
    if (const auto& memory = result.memory_summary_result) {
      memory_pending_ = false;
      last_memory_sample_ =
          std::max(last_memory_sample_, memory->metadata.measurement_time);
      if (memory->metadata.measurement_time >
          std::max(page->document_started, monitoring_started_)) {
        page->state.UpdateMemory(memory->metadata.measurement_time,
                                 memory->private_footprint, threshold);
      }
    }
  }
  Reconcile();
}

void DaoResourceUsageMonitor::OnSuspend() {
  timer_.Stop();
  for (auto& [context, page] : pages_) {
    page->state = {};
  }
  Reconcile();
}

void DaoResourceUsageMonitor::OnResume() {
  if (!cpu_query_) {
    return;
  }
  for (auto& [context, page] : pages_) {
    page->document_started = tick_clock_->NowTicks();
    page->background_since = tick_clock_->NowTicks();
  }
  last_cpu_sample_ = last_memory_sample_ = {};
  memory_pending_ = false;
  timer_.Start(FROM_HERE, kInterval, this, &DaoResourceUsageMonitor::Sample);
}
}  // namespace dao
