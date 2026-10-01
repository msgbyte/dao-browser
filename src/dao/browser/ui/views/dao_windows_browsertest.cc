// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/win/windows_version.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/view_ids.h"
#include "chrome/browser/ui/views/frame/browser_caption_button_container_win.h"
#include "chrome/browser/ui/views/frame/browser_frame_view_win.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/windows_caption_button.h"
#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/mcp/dao_mcp_service.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/aura/window.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/test/clipboard_test_util.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/background.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/views_test_utils.h"
#include "url/gurl.h"

using DaoWindowsBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, DaoControlShortcuts) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* sidebar = view->dao_sidebar();
  ASSERT_TRUE(sidebar);
  auto* focus_manager = view->GetFocusManager();
  const auto control_key = [](ui::KeyboardCode key, bool shift = false) {
    input::NativeWebKeyboardEvent event(
        blink::WebInputEvent::Type::kRawKeyDown,
        blink::WebInputEvent::kControlKey |
            (shift ? blink::WebInputEvent::kShiftKey : 0),
        base::TimeTicks::Now());
    event.windows_key_code = key;
    return event;
  };

  EXPECT_FALSE(focus_manager->HasPriorityHandler(
      ui::Accelerator(ui::VKEY_S, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(focus_manager->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_S, ui::EF_CONTROL_DOWN)));
  EXPECT_TRUE(sidebar->collapsed());
  EXPECT_TRUE(focus_manager->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_OEM_5, ui::EF_CONTROL_DOWN)));
  EXPECT_FALSE(sidebar->collapsed());

  const int tab_count = browser()->tab_strip_model()->count();
  EXPECT_EQ(content::KeyboardEventProcessingResult::HANDLED,
            view->PreHandleKeyboardEvent(control_key(ui::VKEY_D)));
  EXPECT_EQ(tab_count + 1, browser()->tab_strip_model()->count());

  const bool agent_visible = view->dao_agent_sidebar()->is_expanded();
  EXPECT_EQ(content::KeyboardEventProcessingResult::HANDLED,
            view->PreHandleKeyboardEvent(control_key(ui::VKEY_E)));
  EXPECT_NE(agent_visible, view->dao_agent_sidebar()->is_expanded());

  content::BrowserTestClipboardScope clipboard_scope;
  EXPECT_EQ(content::KeyboardEventProcessingResult::HANDLED,
            view->PreHandleKeyboardEvent(control_key(ui::VKEY_C, true)));
  EXPECT_EQ(base::UTF8ToUTF16(browser()->tab_strip_model()
                                ->GetActiveWebContents()->GetVisibleURL().spec()),
            ui::clipboard_test_util::ReadText(
                ui::Clipboard::GetForCurrentThread(),
                ui::ClipboardBuffer::kCopyPaste, nullptr));

  // Plain Copy and extended combinations keep their existing owners.
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            sidebar->PreHandleKeyboardEvent(nullptr, control_key(ui::VKEY_C)));
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            sidebar->PreHandleKeyboardEvent(nullptr,
                                            control_key(ui::VKEY_D, true)));
  focus_manager->set_shortcut_handling_suspended(true);
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            sidebar->PreHandleKeyboardEvent(nullptr, control_key(ui::VKEY_E)));
  focus_manager->set_shortcut_handling_suspended(false);
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest, LittleDaoControlOTransfersTab) {
  Browser* little = dao::DaoLittleDaoController::OpenInLittleDao(
      browser()->profile(), GURL("about:blank"));
  ASSERT_TRUE(little);
  auto* contents = little->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  auto* view = BrowserView::GetBrowserViewForBrowser(little);
  ASSERT_TRUE(view->dao_little_dao_view());
  EXPECT_TRUE(view->GetFocusManager()->ProcessAccelerator(
      ui::Accelerator(ui::VKEY_O, ui::EF_CONTROL_DOWN)));
  EXPECT_NE(TabStripModel::kNoTab,
            browser()->tab_strip_model()->GetIndexOfWebContents(contents));
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       ControlSInterceptAllowsSecondPressToggle) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* sidebar = view->dao_sidebar();
  ASSERT_TRUE(sidebar);
  input::NativeWebKeyboardEvent event(
      blink::WebInputEvent::Type::kRawKeyDown,
      blink::WebInputEvent::kControlKey, base::TimeTicks::Now());
  event.windows_key_code = ui::VKEY_S;
  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED_IS_SHORTCUT,
            view->PreHandleKeyboardEvent(event));
  EXPECT_FALSE(sidebar->collapsed());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar->command_s_toggle_confirmation_pending_for_testing();
  }));
  EXPECT_TRUE(view->dao_toast()->GetVisible());
  EXPECT_EQ(content::KeyboardEventProcessingResult::HANDLED,
            view->PreHandleKeyboardEvent(event));
  EXPECT_TRUE(sidebar->collapsed());
  EXPECT_FALSE(sidebar->command_s_toggle_confirmation_pending_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       CaptionButtonsBlendWithContentAndAgent) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* frame = static_cast<BrowserFrameViewWin*>(
      view->browser_widget()->GetFrameView());
  view->GetWidget()->Restore();
  views::test::RunScheduledLayout(view->GetWidget());
  const auto* controls = frame->caption_button_container_for_testing();
  EXPECT_EQ(nullptr, controls->GetBackground());
  ASSERT_TRUE(controls->layer());
  EXPECT_FALSE(controls->layer()->fills_bounds_opaquely());

  auto* button = views::AsViewClass<WindowsCaptionButton>(
      view->GetWidget()->GetRootView()->GetViewByID(VIEW_ID_MINIMIZE_BUTTON));
  ASSERT_TRUE(button);
  const auto expect_symbol_color = [&](SkColor background_color) {
    gfx::Canvas canvas(button->size(), 1.0f, false);
    canvas.DrawColor(SK_ColorTRANSPARENT, SkBlendMode::kSrc);
    button->PaintButtonContents(&canvas);
    const SkBitmap bitmap = canvas.GetBitmap();
    const SkColor expected =
        color_utils::GetColorWithMaxContrast(background_color);
    int painted_pixels = 0;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) {
        const SkColor pixel = bitmap.getColor(x, y);
        if (SkColorGetA(pixel)) {
          ++painted_pixels;
          // Inactive-window alpha can round the individual RGB channels.
          EXPECT_EQ(color_utils::IsDark(expected),
                    color_utils::IsDark(SkColorSetA(pixel, SK_AlphaOPAQUE)));
        }
      }
    }
    EXPECT_GT(painted_pixels, 0);
  };

  for (SkColor color : {SK_ColorBLACK, SK_ColorWHITE}) {
    view->dao_address_bar()->SetBackground(views::CreateSolidBackground(color));
    expect_symbol_color(color);
  }

  auto* agent = view->dao_agent_sidebar();
  agent->Toggle();
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(view->GetWidget());
    return agent->width() >= dao::DaoAgentSidebarView::kDefaultWidth;
  }));
  ASSERT_TRUE(agent->GetBoundsInScreen().Contains(
      button->GetBoundsInScreen().CenterPoint()));
  EXPECT_EQ(nullptr, controls->GetBackground());
  expect_symbol_color(agent->GetBackground()->color().ResolveToSkColor(
      agent->GetColorProvider()));
}

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

  const int maximize_component =
      base::win::GetVersion() >= base::win::Version::WIN11 ? HTMAXBUTTON
                                                         : HTCLIENT;
  for (const auto& [id, component] :
       {std::pair{VIEW_ID_MINIMIZE_BUTTON, HTCLIENT},
        std::pair{VIEW_ID_MAXIMIZE_BUTTON, maximize_component},
        std::pair{VIEW_ID_CLOSE_BUTTON, HTCLIENT}}) {
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
                       CaptionHoverStaysWithinRoundedAddressRow) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  view->GetWidget()->Restore();
  views::test::RunScheduledLayout(view->GetWidget());
  const auto* address_bar = view->dao_address_bar();
  for (int id : {VIEW_ID_MINIMIZE_BUTTON, VIEW_ID_MAXIMIZE_BUTTON,
                 VIEW_ID_CLOSE_BUTTON}) {
    auto* button = views::AsViewClass<WindowsCaptionButton>(
        view->GetWidget()->GetRootView()->GetViewByID(id));
    ASSERT_TRUE(button);
    const gfx::Rect surface = views::View::ConvertRectToTarget(
        address_bar, button, address_bar->GetLocalBounds());
    ASSERT_GT(surface.y(), 0);
    for (auto state : {views::Button::STATE_HOVERED,
                       views::Button::STATE_PRESSED}) {
      // Finish the transition immediately so the hover paint is deterministic.
      button->SetState(views::Button::STATE_PRESSED);
      button->SetState(state);
      gfx::Canvas canvas(button->size(), 1.0f, false);
      canvas.DrawColor(SK_ColorTRANSPARENT, SkBlendMode::kSrc);
      button->OnPaintBackground(&canvas);
      const SkBitmap bitmap = canvas.GetBitmap();
      EXPECT_EQ(0u, SkColorGetA(bitmap.getColor(button->width() / 2, 0)));
      EXPECT_GT(SkColorGetA(bitmap.getColor(button->width() / 2,
                                            button->height() / 2)), 0u);
      if (id == VIEW_ID_CLOSE_BUTTON) {
        // Both the outer gutter and the content card's rounded corner stay clear.
        EXPECT_EQ(0u, SkColorGetA(bitmap.getColor(button->width() - 1,
                                                 button->height() / 2)));
        EXPECT_EQ(0u, SkColorGetA(bitmap.getColor(surface.right() - 1,
                                                 surface.y())));
      }
    }
    button->SetState(views::Button::STATE_NORMAL);
  }
}

IN_PROC_BROWSER_TEST_F(DaoWindowsBrowserTest,
                       CaptionHoverTargetsNativeButtonsOverAgent) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* frame = static_cast<BrowserFrameViewWin*>(
      view->browser_widget()->GetFrameView());
  view->GetWidget()->Restore();
  auto* agent = view->dao_agent_sidebar();
  agent->Toggle();
  ASSERT_TRUE(base::test::RunUntil([&] {
    views::test::RunScheduledLayout(view->GetWidget());
    return agent->width() >= dao::DaoAgentSidebarView::kDefaultWidth;
  }));
  auto* root = view->GetWidget()->GetRootView();
  for (int id : {VIEW_ID_MINIMIZE_BUTTON, VIEW_ID_CLOSE_BUTTON}) {
    auto* button = root->GetViewByID(id);
    ASSERT_TRUE(button);
    ASSERT_TRUE(agent->GetBoundsInScreen().Contains(
        button->GetBoundsInScreen().CenterPoint()));
    for (int x : {1, button->width() / 2, button->width() - 2}) {
      for (int y : {1, button->height() / 2, button->height() - 2}) {
        const gfx::Point point(x, y);
        EXPECT_EQ(HTCLIENT, frame->NonClientHitTest(
                                views::View::ConvertPointToTarget(
                                    button, frame, point)));
        EXPECT_EQ(button, root->GetEventHandlerForPoint(
                              views::View::ConvertPointToTarget(
                                  button, root, point)));
      }
    }
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
