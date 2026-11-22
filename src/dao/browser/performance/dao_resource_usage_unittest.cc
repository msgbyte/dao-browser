// Copyright 2026 Dao Browser Authors. All rights reserved.
#include "dao/browser/performance/dao_resource_usage_state.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/test/task_environment.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/notification_display_service_tester.h"
#include "chrome/browser/notifications/notification_platform_bridge.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toasts/api/toast_id.h"
#include "chrome/browser/ui/toasts/toast_controller.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/agent_space/dao_hidden_browser.h"
#include "dao/browser/performance/dao_resource_usage_monitor.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/message_center/public/cpp/notification.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/test/views_drawing_test_utils.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/test/widget_activation_waiter.h"

namespace dao {

class DaoResourceUsageMonitorTestApi {
 public:
  static void SetClock(DaoResourceUsageMonitor* monitor,
                       const base::TickClock* clock) {
    monitor->tick_clock_ = clock;
  }
  static void QueryCpu(DaoResourceUsageMonitor* monitor) {
    monitor->cpu_query_->QueryOnce();
  }
  static bool HasCpuSample(DaoResourceUsageMonitor* monitor) {
    return !monitor->last_cpu_sample_.is_null();
  }
};

namespace {

using DaoResourceUsageBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoResourceUsageBrowserTest,
                       ReenableCancelsQueuedResultsFromOldSubscription) {
  auto* prefs = g_browser_process->local_state();
  const auto* pref = performance_manager::user_tuning::prefs::
      kPerformanceInterventionNotificationEnabled;
  auto* monitor = DaoResourceUsageMonitor::Get();
  prefs->SetBoolean(pref, false);
  base::RunLoop().RunUntilIdle();
  prefs->SetBoolean(pref, true);
  DaoResourceUsageMonitorTestApi::QueryCpu(monitor);
  ASSERT_FALSE(DaoResourceUsageMonitorTestApi::HasCpuSample(monitor));

  prefs->SetBoolean(pref, false);
  prefs->SetBoolean(pref, true);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(DaoResourceUsageMonitorTestApi::HasCpuSample(monitor));

  DaoResourceUsageMonitorTestApi::QueryCpu(monitor);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(DaoResourceUsageMonitorTestApi::HasCpuSample(monitor));
  prefs->SetBoolean(pref, false);
}

IN_PROC_BROWSER_TEST_F(DaoResourceUsageBrowserTest,
                       SettingsDisableClearsAlertsAndRequiresFreshHistory) {
  auto* prefs = g_browser_process->local_state();
  const auto* pref = performance_manager::user_tuning::prefs::
      kPerformanceInterventionNotificationEnabled;
  EXPECT_TRUE(prefs->GetBoolean(pref));
  auto* monitor = DaoResourceUsageMonitor::Get();
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto context = resource_attribution::PageContext::FromWebContents(contents);
  ASSERT_TRUE(context);
  const auto start = base::TimeTicks::Now();
  auto sample = [&](int seconds) {
    resource_attribution::QueryResults result;
    result.cpu_time_result = resource_attribution::CPUTimeResult{
        .metadata = {start + base::Seconds(seconds),
                     resource_attribution::MeasurementAlgorithm::kSum},
        .start_time = start,
        .cumulative_cpu = base::Seconds(seconds),
        .cumulative_background_cpu = base::Seconds(seconds)};
    monitor->OnResourceUsageUpdated({{*context, result}});
  };
  for (int seconds : {0, 30, 60, 90}) {
    sample(seconds);
  }
  ASSERT_EQ(monitor->GetAlerts(contents), DaoResourceUsageState::kCpu);

  prefs->SetBoolean(pref, false);
  EXPECT_EQ(monitor->GetAlerts(contents), DaoResourceUsageState::kNone);
  sample(120);
  EXPECT_EQ(monitor->GetAlerts(contents), DaoResourceUsageState::kNone);

  prefs->SetBoolean(pref, true);
  sample(150);
  EXPECT_EQ(monitor->GetAlerts(contents), DaoResourceUsageState::kNone);
  for (int seconds : {180, 210, 240}) {
    sample(seconds);
  }
  EXPECT_EQ(monitor->GetAlerts(contents), DaoResourceUsageState::kCpu);
  prefs->SetBoolean(pref, false);
}

class DaoResourceBackgroundBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    SetEnabled(false);
    clock_.SetNowTicks(base::TimeTicks::Now());
    DaoResourceUsageMonitorTestApi::SetClock(monitor(), &clock_);
    notifications_ = std::make_unique<NotificationDisplayServiceTester>(
        browser()->GetProfile());
    notifications_->SetNotificationAddedClosure(base::BindRepeating(
        [](int* count) { ++*count; }, &notification_count_));
    SetEnabled(true);
    clock_.Advance(base::Seconds(1));
    start_ = clock_.NowTicks();
  }
  void TearDownOnMainThread() override {
    SetEnabled(false);
    DaoResourceUsageMonitorTestApi::SetClock(
        monitor(), base::DefaultTickClock::GetInstance());
    notifications_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }
  DaoResourceUsageMonitor* monitor() { return DaoResourceUsageMonitor::Get(); }
  void SetEnabled(bool enabled) {
    g_browser_process->local_state()->SetBoolean(
        performance_manager::user_tuning::prefs::
            kPerformanceInterventionNotificationEnabled,
        enabled);
  }
  void Sample(content::WebContents* contents,
              int seconds,
              std::optional<int> cpu_seconds = std::nullopt) {
    clock_.SetNowTicks(start_ + base::Seconds(seconds));
    auto context = resource_attribution::PageContext::FromWebContents(contents);
    ASSERT_TRUE(context);
    resource_attribution::QueryResults result;
    result.cpu_time_result = resource_attribution::CPUTimeResult{
        .metadata = {clock_.NowTicks(),
                     resource_attribution::MeasurementAlgorithm::kSum},
        .start_time = start_,
        .cumulative_cpu = base::Seconds(cpu_seconds.value_or(seconds)),
        .cumulative_background_cpu =
            base::Seconds(cpu_seconds.value_or(seconds))};
    monitor()->OnResourceUsageUpdated({{*context, result}});
  }
  void Activate(BrowserWindowInterface* browser) {
    // Headless macOS windows do not resign key when another window activates.
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* other) {
          if (other != browser)
            BrowserView::GetBrowserViewForBrowser(other)
                ->GetWidget()
                ->OnNativeWidgetActivationChanged(false);
          return true;
        });
    browser->GetWindow()->Restore();
    browser->GetWindow()->Activate();
    BrowserView::GetBrowserViewForBrowser(browser)
        ->GetWidget()
        ->OnNativeWidgetActivationChanged(true);
    views::test::WaitForWidgetActive(
        BrowserView::GetBrowserViewForBrowser(browser)->GetWidget(), true);
    ASSERT_TRUE(browser->GetWindow()->IsActive());
  }
  void RaiseAlert(content::WebContents* contents) {
    for (int seconds : {0, 30, 60, 90})
      Sample(contents, seconds);
    ASSERT_EQ(monitor()->GetAlerts(contents), DaoResourceUsageState::kCpu);
  }
  base::SimpleTestTickClock clock_;
  base::TimeTicks start_;
  std::unique_ptr<NotificationDisplayServiceTester> notifications_;
  int notification_count_ = 0;
};

IN_PROC_BROWSER_TEST_F(
    DaoResourceBackgroundBrowserTest,
    MinimizedWindowNotifiesOnceAndDisableClosesNotification) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  EXPECT_EQ(notification_count_, 1);
  Sample(contents, 120);
  EXPECT_EQ(notification_count_, 1);
  EXPECT_EQ(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .size(),
      1u);
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  Activate(browser());
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(BrowserView::GetBrowserViewForBrowser(browser())
                  ->dao_toast()
                  ->IsShowingPersistentToast());
  EXPECT_EQ(notification_count_, 1);
  SetEnabled(false);
  EXPECT_FALSE(BrowserView::GetBrowserViewForBrowser(browser())
                   ->dao_toast()
                   ->IsShowingPersistentToast());
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
}

IN_PROC_BROWSER_TEST_F(
    DaoResourceBackgroundBrowserTest,
    ForegroundOnlyMarksThenInactiveSelectedTabRetainsReminder) {
  Activate(browser());
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  RaiseAlert(contents);
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_EQ(notification_count_, 0);

  auto* other = CreateBrowser(browser()->GetProfile());
  Activate(other);
  EXPECT_FALSE(browser()->IsActive());
  Sample(contents, 120);
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  Sample(contents, 150);
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(monitor()->GetReminderTabs(browser()).empty());
  EXPECT_EQ(monitor()->GetReminderTabs(other).size(), 1u);
  EXPECT_EQ(notification_count_, 0);
  EXPECT_TRUE(BrowserView::GetBrowserViewForBrowser(other)
                  ->dao_toast()
                  ->IsShowingPersistentToast());

  Activate(browser());
  EXPECT_EQ(monitor()->GetReminderTabs(browser()).size(), 1u);
  EXPECT_TRUE(monitor()->GetReminderTabs(other).empty());
  monitor()->DismissReminder(browser()->GetProfile());
  Sample(contents, 180);
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_EQ(monitor()->GetAlerts(contents), DaoResourceUsageState::kCpu);
  EXPECT_EQ(notification_count_, 0);
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       MultipleTabsAggregateAndOsDismissalDoesNotRepeat) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  auto* first = browser()->tab_strip_model()->GetWebContentsAt(0);
  auto* second = browser()->tab_strip_model()->GetWebContentsAt(1);
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  for (int seconds : {0, 30, 60, 90}) {
    Sample(first, seconds);
    Sample(second, seconds);
  }
  EXPECT_EQ(monitor()->GetReminderTabs(browser()).size(), 2u);
  EXPECT_EQ(notification_count_, 1);
  auto notifications = notifications_->GetDisplayedNotificationsForType(
      kDaoResourceUsageNotificationType);
  ASSERT_EQ(notifications.size(), 1u);
  EXPECT_EQ(notifications.front().title(),
            l10n_util::GetStringUTF16(IDS_DAO_RESOURCE_NOTIFICATION_TITLE));
  notifications_->RemoveAllNotifications(kDaoResourceUsageNotificationType,
                                         true);
  Sample(first, 120);
  Sample(second, 120);
  EXPECT_EQ(notification_count_, 1);
  EXPECT_EQ(monitor()->GetReminderTabs(browser()).size(), 2u);
  monitor()->DismissReminder(browser()->GetProfile());
  Sample(first, 150);
  Sample(second, 150);
  EXPECT_TRUE(monitor()->GetReminderTabs(browser()).empty());
  EXPECT_EQ(notification_count_, 1);
}

IN_PROC_BROWSER_TEST_F(
    DaoResourceBackgroundBrowserTest,
    NotificationClickInspectsAndAcknowledgesCurrentIncident) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  auto notifications = notifications_->GetDisplayedNotificationsForType(
      kDaoResourceUsageNotificationType);
  ASSERT_EQ(notifications.size(), 1u);
  notifications_->SimulateClick(kDaoResourceUsageNotificationType,
                                notifications.front().id(), std::nullopt,
                                std::nullopt);
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(browser()->GetWindow()->IsMinimized());
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
  Sample(contents, 120);
  EXPECT_EQ(notification_count_, 1);
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       NavigationClosesNotificationAndRequiresFreshHistory) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  ASSERT_EQ(notification_count_, 1);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("data:text/html,new page")));
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
  Sample(contents, 120);
  EXPECT_EQ(notification_count_, 1);
  EXPECT_EQ(monitor()->GetAlerts(contents), 0);
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       ExistingMutedPlaybackStateSuppressesBothSurfaces) {
  class MediaObserver : public DaoResourceUsageMonitor::Observer {
   public:
    void OnResourceUsageChanged(content::WebContents*) override {}
    bool IsResourceAlertMediaPlaying(content::WebContents*) const override {
      return playing;
    }
    bool playing = true;
  } media;
  monitor()->AddObserver(&media);
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_EQ(notification_count_, 0);
  media.playing = false;
  monitor()->RefreshReminders();
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  EXPECT_EQ(notification_count_, 1);
  media.playing = true;
  monitor()->RefreshReminders();
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
  media.playing = false;
  monitor()->RefreshReminders();
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  EXPECT_EQ(notification_count_, 1);
  monitor()->RemoveObserver(&media);
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       ExcludedPageDoesNotSuppressNextIncident) {
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  auto* first = browser()->tab_strip_model()->GetWebContentsAt(0);
  auto* excluded = browser()->tab_strip_model()->GetWebContentsAt(1);
  class MediaObserver : public DaoResourceUsageMonitor::Observer {
   public:
    explicit MediaObserver(content::WebContents* contents)
        : contents_(contents) {}
    void OnResourceUsageChanged(content::WebContents*) override {}
    bool IsResourceAlertMediaPlaying(
        content::WebContents* contents) const override {
      return contents == contents_;
    }

   private:
    raw_ptr<content::WebContents> contents_;
  } media(excluded);
  monitor()->AddObserver(&media);
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  for (int seconds : {0, 30, 60, 90}) {
    Sample(excluded, seconds);
    Sample(first, seconds);
  }
  EXPECT_EQ(notification_count_, 1);
  EXPECT_FALSE(monitor()->ShouldNotify(excluded));
  for (int seconds : {120, 150}) {
    Sample(excluded, seconds);
    Sample(first, seconds, 90);
  }
  EXPECT_FALSE(monitor()->ShouldNotify(first));
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
  for (int seconds : {180, 210, 240}) {
    Sample(excluded, seconds);
    Sample(first, seconds, seconds - 60);
  }
  EXPECT_TRUE(monitor()->ShouldNotify(first));
  EXPECT_EQ(notification_count_, 2);
  monitor()->RemoveObserver(&media);
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       ChromiumFeedbackTemporarilyHidesPersistentReminder) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  Activate(browser());
  auto* toast = BrowserView::GetBrowserViewForBrowser(browser())->dao_toast();
  ASSERT_TRUE(toast->IsShowingPersistentToast());
  auto* controller = ToastController::From(browser());
  ASSERT_TRUE(controller->MaybeShowToast(ToastParams(ToastId::kLinkCopied)));
  ASSERT_TRUE(controller->GetToastWidgetForTesting());
  EXPECT_FALSE(toast->GetVisible());
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  // A new sample and a queued replacement must not uncover the reminder.
  Sample(contents, 120);
  EXPECT_FALSE(toast->GetVisible());
  ASSERT_TRUE(controller->MaybeShowToast(ToastParams(ToastId::kLinkCopied)));
  controller->GetToastWidgetForTesting()->CloseNow();
  ASSERT_TRUE(controller->GetToastWidgetForTesting());
  EXPECT_FALSE(toast->GetVisible());
  controller->GetToastWidgetForTesting()->CloseNow();
  EXPECT_TRUE(toast->GetVisible());
  EXPECT_TRUE(toast->IsShowingPersistentToast());
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  // Clearing the incident during Chromium feedback must not revive it later.
  ASSERT_TRUE(controller->MaybeShowToast(ToastParams(ToastId::kLinkCopied)));
  monitor()->DismissReminder(browser()->GetProfile());
  controller->GetToastWidgetForTesting()->CloseNow();
  EXPECT_FALSE(toast->GetVisible());
}

IN_PROC_BROWSER_TEST_F(DaoResourceUsageBrowserTest,
                       LongReminderFitsNormalAndMiniDaoContent) {
  auto* mini = DaoLittleDaoController::OpenInLittleDao(browser()->GetProfile(),
                                                       GURL("about:blank"));
  ASSERT_TRUE(mini);
  for (BrowserWindowInterface* window :
       {static_cast<BrowserWindowInterface*>(browser()), mini}) {
    SCOPED_TRACE(window->GetType());
    auto* view = BrowserView::GetBrowserViewForBrowser(window);
    auto* toast = view->dao_toast();
    ASSERT_TRUE(toast);
    toast->SetPersistentToast(std::u16string(100, u'W'), u"View tab",
                              base::DoNothing(), base::DoNothing());
    for (int width : {760, 1000, 760}) {
      window->GetWindow()->SetBounds(gfx::Rect(0, 0, width, 640));
      view->GetWidget()->LayoutRootViewIfNecessary();
      view->DeprecatedLayoutImmediately();
      const auto content_bounds =
          view->GetActiveContentsWebView()->GetBoundsInScreen();
      EXPECT_TRUE(content_bounds.Contains(toast->GetBoundsInScreen()))
          << "content=" << content_bounds.ToString()
          << ", toast=" << toast->GetBoundsInScreen().ToString();
      EXPECT_LE(toast->width(), content_bounds.width() - 24);
      auto* label = static_cast<views::Label*>(toast->children().front());
      EXPECT_FALSE(label->GetDisplayTextForTesting().empty());
      EXPECT_NE(label->GetDisplayTextForTesting(), label->GetText());
      for (const auto& child : toast->children()) {
        EXPECT_TRUE(toast->GetLocalBounds().Contains(child->bounds()));
        if (child != label)
          EXPECT_EQ(child->width(), child->GetPreferredSize().width());
      }
    }
    toast->ClearPersistentToast();
  }
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       IncognitoDoesNotReceiveRegularProfileReminder) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* incognito = CreateIncognitoBrowser();
  Activate(incognito);
  RaiseAlert(contents);
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(monitor()->GetReminderTabs(incognito).empty());
  EXPECT_EQ(monitor()->GetReminderTabs(browser()).size(), 1u);
  EXPECT_EQ(notification_count_, 0);
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       RecoveryClosesNotificationAndNextIncidentCanNotify) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  ASSERT_EQ(notification_count_, 1);
  Sample(contents, 120, 90);
  Sample(contents, 150, 90);
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
  Sample(contents, 180, 120);
  Sample(contents, 210, 150);
  EXPECT_EQ(notification_count_, 1);
  Sample(contents, 240, 180);
  EXPECT_TRUE(monitor()->ShouldNotify(contents));
  EXPECT_EQ(notification_count_, 2);
}

TEST(DaoResourceNotificationTest, UsesNativeSystemNotificationBridge) {
  EXPECT_TRUE(NotificationPlatformBridge::CanHandleType(
      kDaoResourceUsageNotificationType));
}

IN_PROC_BROWSER_TEST_F(DaoResourceBackgroundBrowserTest,
                       HiddenTaskWindowCannotBeInspectedThroughReminder) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  browser()->GetWindow()->Minimize();
  base::RunLoop().RunUntilIdle();
  RaiseAlert(contents);
  ASSERT_TRUE(monitor()->ShouldNotify(contents));
  auto* other = CreateBrowser(browser()->GetProfile());
  other->GetWindow()->Minimize();
  static BrowserWindowInterface* hidden_browser = nullptr;
  hidden_browser = browser();
  SetDaoHiddenBrowserPredicate([](BrowserWindowInterface* candidate) {
    return candidate == hidden_browser;
  });
  // Simulate a task becoming hidden after a reminder was already retained.
  monitor()->InspectReminder(browser()->GetProfile());
  EXPECT_TRUE(browser()->GetWindow()->IsMinimized());
  monitor()->RefreshReminders();
  EXPECT_FALSE(monitor()->ShouldNotify(contents));
  EXPECT_TRUE(
      notifications_
          ->GetDisplayedNotificationsForType(kDaoResourceUsageNotificationType)
          .empty());
  SetDaoHiddenBrowserPredicate(nullptr);
}

base::TimeTicks At(int seconds) {
  return base::TimeTicks() + base::Seconds(seconds + 1);
}

TEST(DaoResourceUsageTest, CpuRequiresSustainedLoadAndRecoversWithHysteresis) {
  DaoResourceUsageState state;
  for (int time : {0, 30, 60})
    state.UpdateCpu(At(time), base::Seconds(time));
  EXPECT_EQ(state.alerts(), 0);
  state.UpdateCpu(At(90), base::Seconds(90));
  EXPECT_EQ(state.alerts(), DaoResourceUsageState::kCpu);
  state.Dismiss();
  state.UpdateCpu(At(120), base::Seconds(105));
  EXPECT_TRUE(state.dismissed());
  EXPECT_EQ(state.alerts(), DaoResourceUsageState::kCpu);
  state.UpdateCpu(At(150), base::Seconds(105));
  EXPECT_EQ(state.alerts(), DaoResourceUsageState::kCpu);
  state.UpdateCpu(At(180), base::Seconds(105));
  EXPECT_EQ(state.alerts(), 0);
  EXPECT_FALSE(state.dismissed());
}

TEST(DaoResourceUsageTest, DuplicateShortAndMissingCpuSamplesCannotRaiseAlert) {
  DaoResourceUsageState state;
  state.UpdateCpu(At(0), base::Seconds(0));
  for (int time = 1; time < 30; ++time) {
    state.UpdateCpu(At(time), base::Seconds(time));
  }
  EXPECT_EQ(state.alerts(), 0);
  state.UpdateCpu(At(30), base::Seconds(30));
  state.UpdateCpu(At(30), base::Seconds(100));
  state.UpdateCpu(At(120), base::Seconds(120));
  EXPECT_EQ(state.alerts(), 0);
  state.UpdateCpu(At(150), base::Seconds(150));
  EXPECT_EQ(state.alerts(), 0);
}

TEST(DaoResourceUsageTest, AsynchronousSampleJitterDoesNotResetSustainedCpu) {
  DaoResourceUsageState state;
  state.UpdateCpu(At(0), base::Seconds(0));
  state.UpdateCpu(At(30) - base::Milliseconds(2), base::Seconds(30));
  state.UpdateCpu(At(60) + base::Milliseconds(2), base::Seconds(60));
  state.UpdateCpu(At(90) + base::Milliseconds(3), base::Seconds(90));
  EXPECT_EQ(state.alerts(), DaoResourceUsageState::kCpu);
  state.Expire(At(150) + base::Milliseconds(4));
  EXPECT_EQ(state.alerts(), DaoResourceUsageState::kCpu);
}

TEST(DaoResourceUsageTest, MemoryRequiresFreshSeparatedSamplesAndRecovers) {
  DaoResourceUsageState state;
  state.UpdateMemory(At(0), base::GiB(2), base::GiB(2));
  state.UpdateMemory(At(0), base::GiB(2), base::GiB(2));
  state.UpdateMemory(At(60), base::GiB(2), base::GiB(2));
  EXPECT_EQ(state.alerts(), 0);
  state.UpdateMemory(At(120), base::GiB(2), base::GiB(2));
  EXPECT_EQ(state.alerts(), DaoResourceUsageState::kMemory);
  state.Dismiss();
  state.UpdateMemory(At(240), base::GiB(1), base::GiB(2));
  EXPECT_TRUE(state.dismissed());
  state.UpdateMemory(At(360), base::GiB(1), base::GiB(2));
  EXPECT_EQ(state.alerts(), 0);
  EXPECT_FALSE(state.dismissed());
}

TEST(DaoResourceUsageTest, StaleSamplesExpireAndNewDocumentStartsClean) {
  DaoResourceUsageState state;
  state.UpdateMemory(At(0), base::GiB(1), base::GiB(1));
  state.UpdateMemory(At(120), base::GiB(1), base::GiB(1));
  state.Dismiss();
  state.Expire(At(421));
  EXPECT_EQ(state.alerts(), 0);
  EXPECT_FALSE(state.dismissed());
  state.UpdateMemory(At(422), base::GiB(1), base::GiB(1));
  EXPECT_EQ(state.alerts(), 0);
  state = {};
  state.UpdateMemory(At(600), base::GiB(1), base::GiB(1));
  EXPECT_EQ(state.alerts(), 0);
}

class DaoPersistentResourceToastTest : public views::ViewsTestBase {
 public:
  DaoPersistentResourceToastTest()
      : views::ViewsTestBase(
            base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  void SetUp() override {
    SetUpForInteractiveTests();
    views::ViewsTestBase::SetUp();
    ui::ResourceBundle::GetSharedInstance().OverrideLocaleStringResource(
        IDS_DAO_RESOURCE_DISMISS, u"Dismiss");
  }
};

TEST_F(DaoPersistentResourceToastTest, SurvivesTimeAndTransientFeedback) {
  DaoToastView toast;
  toast.SetPersistentToast(u"High usage", u"View tab", base::DoNothing(),
                           base::DoNothing());
  task_environment()->FastForwardBy(base::Minutes(5));
  EXPECT_TRUE(toast.IsShowingToast(u"High usage"));
  EXPECT_TRUE(toast.IsShowingPersistentToast());
  toast.ShowToast(u"Copied");
  EXPECT_TRUE(toast.IsShowingToast(u"Copied"));
  EXPECT_FALSE(toast.IsShowingPersistentToast());
  task_environment()->FastForwardBy(base::Seconds(3));
  EXPECT_TRUE(toast.IsShowingToast(u"High usage"));
  toast.ClearPersistentToast();
  EXPECT_FALSE(toast.GetVisible());
  toast.ShowToast(u"Copied");
  task_environment()->FastForwardBy(base::Seconds(3));
  EXPECT_FALSE(toast.GetVisible());
}
TEST_F(DaoPersistentResourceToastTest,
       PaintsPersistentControlsOnTransparentLayer) {
  auto widget = CreateTestWidget(views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  widget->SetBounds(gfx::Rect(0, 0, 600, 60));
  auto* toast = widget->SetContentsView(std::make_unique<DaoToastView>());
  toast->SetPersistentToast(u"High usage", u"View tab", base::DoNothing(),
                            base::DoNothing());
  widget->Show();
  widget->LayoutRootViewIfNecessary();
  EXPECT_FALSE(views::test::PaintViewToBitmap(toast).drawsNothing());
  widget->CloseNow();
}

TEST_F(DaoPersistentResourceToastTest,
       ExplicitDismissalDoesNotReturnAfterFeedback) {
  DaoToastView toast;
  int dismissed = 0;
  toast.SetPersistentToast(
      u"High usage", u"View tab", base::DoNothing(),
      base::BindRepeating([](int* count) { ++*count; }, &dismissed));
  auto* close = static_cast<views::LabelButton*>(toast.children().back());
  ui::MouseEvent event(ui::EventType::kMouseReleased, gfx::Point(),
                       gfx::Point(), ui::EventTimeForNow(),
                       ui::EF_LEFT_MOUSE_BUTTON, ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(close).NotifyClick(event);
  EXPECT_EQ(dismissed, 1);
  EXPECT_FALSE(toast.GetVisible());
  toast.ShowToast(u"Copied");
  task_environment()->FastForwardBy(base::Seconds(3));
  EXPECT_FALSE(toast.GetVisible());
  EXPECT_FALSE(toast.IsShowingPersistentToast());
}

}  // namespace
}  // namespace dao
