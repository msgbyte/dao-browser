// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/test/run_until.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_caption_button_container_win.h"
#include "chrome/browser/ui/views/frame/browser_frame_view_win.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "dao/browser/mcp/dao_mcp_service.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/aura/window.h"
#include "ui/base/hit_test.h"
#include "ui/views/test/views_test_utils.h"
#include "url/gurl.h"

using DaoWindowsBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, CaptionButtonsShareAddressRow) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* frame = static_cast<BrowserFrameViewWin*>(
      view->browser_widget()->GetFrameView());
  view->GetWidget()->Restore();
  views::test::RunScheduledLayout(view->GetWidget());
  const auto* controls = frame->caption_button_container_for_testing();
  ASSERT_TRUE(controls->GetVisible());
  EXPECT_LE(frame->GetTopInset(false), 1);
  EXPECT_TRUE(controls->GetBoundsInScreen().Intersects(
      view->dao_address_bar()->GetBoundsInScreen()));

  for (const auto& [id, component] :
       {std::pair{VIEW_ID_MINIMIZE_BUTTON, HTMINBUTTON},
        std::pair{VIEW_ID_MAXIMIZE_BUTTON, HTMAXBUTTON},
        std::pair{VIEW_ID_CLOSE_BUTTON, HTCLOSE}}) {
    const auto* button = controls->GetViewByID(id);
    ASSERT_TRUE(button);
    gfx::Point point = button->GetBoundsInScreen().CenterPoint();
    views::View::ConvertPointFromScreen(frame, &point);
    EXPECT_EQ(component, frame->NonClientHitTest(point));
  }
  for (auto rect : view->dao_address_bar()->interactive_rects()) {
    rect = views::View::ConvertRectToTarget(view->dao_address_bar(), view, rect);
    EXPECT_FALSE(rect.Intersects(view->GetDaoWindowControlsBounds()));
  }
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       NarrowAddressRowAvoidsWindowControls) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  view->GetWidget()->Restore();
  views::test::RunScheduledLayout(view->GetWidget());
  auto* address_bar = view->dao_address_bar();
  const gfx::Rect original_bounds = address_bar->bounds();
  const gfx::Rect controls = view->GetDaoWindowControlsBounds();
  ASSERT_FALSE(controls.IsEmpty());

  // Simulate a narrow right-hand pane with only 64 DIP beside the controls.
  address_bar->SetBounds(controls.x() - 64, original_bounds.y(),
                         controls.width() + 64, original_bounds.height());
  address_bar->DeprecatedLayoutImmediately();
  for (auto rect : address_bar->interactive_rects()) {
    rect = views::View::ConvertRectToTarget(address_bar, view, rect);
    EXPECT_FALSE(rect.Intersects(controls));
  }
  address_bar->SetBoundsRect(original_bounds);
  views::test::RunScheduledLayout(view->GetWidget());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       CaptionButtonsStayFixedAcrossSidebarChanges) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  views::test::RunScheduledLayout(view->GetWidget());
  const auto initial_bounds = view->GetDaoWindowControlsBounds();
  ASSERT_FALSE(initial_bounds.IsEmpty());
  view->dao_agent_sidebar()->Toggle();
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(view->GetWidget());
    return view->dao_agent_sidebar()->width() >=
           dao::DaoAgentSidebarView::kDefaultWidth;
  }));
  EXPECT_EQ(initial_bounds, view->GetDaoWindowControlsBounds());
  view->dao_sidebar()->ToggleCollapsed();
  views::test::RunScheduledLayout(view->GetWidget());
  EXPECT_EQ(initial_bounds, view->GetDaoWindowControlsBounds());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, TabSearchHasRegisteredWebUIConfig) {
  EXPECT_NE(nullptr, TopChromeWebUIConfig::From(
                         browser()->profile(),
                         GURL(chrome::kChromeUITabSearchURL)));
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, CommandBarRestoresEventTargeting) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* window = contents->GetNativeView();
  const auto original_policy = window->event_targeting_policy();
  auto* command_bar =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_command_bar();
  ASSERT_TRUE(command_bar);
  command_bar->Show();
  EXPECT_EQ(aura::EventTargetingPolicy::kNone, window->event_targeting_policy());
  command_bar->Hide();
  EXPECT_EQ(original_policy, window->event_targeting_policy());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       CommandBarTabSwitchRestoresOriginalTarget) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  auto* model = browser()->tab_strip_model();
  model->ActivateTabAt(0);
  auto* first_window = model->GetActiveWebContents()->GetNativeView();
  const auto original_policy = first_window->event_targeting_policy();
  auto* command_bar =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_command_bar();
  ASSERT_TRUE(command_bar);
  command_bar->Show();
  EXPECT_EQ(aura::EventTargetingPolicy::kNone,
            first_window->event_targeting_policy());
  model->ActivateTabAt(1);
  command_bar->Hide();
  EXPECT_EQ(original_policy, first_window->event_targeting_policy());
  model->ActivateTabAt(0);
  EXPECT_EQ(original_policy, first_window->event_targeting_policy());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       ControlCenterTabSwitchRestoresOnlyBlockedPane) {
  chrome::AddTabAt(browser(), GURL("about:blank"), -1, true);
  auto* model = browser()->tab_strip_model();
  auto* first = model->GetWebContentsAt(0);
  auto* second = model->GetWebContentsAt(1);
  model->ActivateTabAt(0);

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* split_view = browser_view->dao_split_view();
  ASSERT_TRUE(split_view);
  ASSERT_TRUE(split_view->SplitPane(first, dao::SplitDirection::kHorizontal,
                                    false, second));
  model->ActivateTabAt(0);

  auto* first_window = first->GetNativeView();
  auto* second_window = second->GetNativeView();
  const auto first_policy = first_window->event_targeting_policy();
  const auto second_policy = second_window->event_targeting_policy();
  auto* popup = browser_view->dao_control_center_popup();
  ASSERT_TRUE(popup);
  popup->ShowAt(gfx::Point(100, 100));
  EXPECT_EQ(aura::EventTargetingPolicy::kNone,
            first_window->event_targeting_policy());

  // Another owner can block the newly active pane independently of this popup.
  dao::BlockWebContentNativeEvents(second);
  model->ActivateTabAt(1);
  EXPECT_FALSE(popup->GetVisible());
  EXPECT_EQ(first_policy, first_window->event_targeting_policy());
  EXPECT_EQ(aura::EventTargetingPolicy::kNone,
            second_window->event_targeting_policy());
  popup->Hide();
  EXPECT_EQ(aura::EventTargetingPolicy::kNone,
            second_window->event_targeting_policy());
  dao::UnblockWebContentNativeEvents(second);
  EXPECT_EQ(second_policy, second_window->event_targeting_policy());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, CancelledDragRestoresEventTargeting) {
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* window = contents->GetNativeView();
  const auto original_policy = window->event_targeting_policy();
  auto* split_view =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_split_view();
  ASSERT_TRUE(split_view);
  split_view->SetTabDragActive(true);
  dao::EndTabDragNativeEvents();
  EXPECT_FALSE(split_view->tab_drag_active());
  EXPECT_EQ(original_policy, window->event_targeting_policy());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, McpIsUnavailable) {
  EXPECT_FALSE(dao::DaoMcpService::IsSupported());
  auto* service = dao::DaoMcpService::Get();
  service->SetEnabled(true);
  EXPECT_EQ(dao::DaoMcpStatus::kDisabled, service->GetStatus().state);
  EXPECT_TRUE(service->GetMcpConfiguration().empty());
  EXPECT_EQ(0u, service->GetControlledTargetCount());
}
