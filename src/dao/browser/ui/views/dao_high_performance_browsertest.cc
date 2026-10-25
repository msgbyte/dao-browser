// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/process/process.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "build/build_config.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_source.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/guest_view/browser/guest_view_base.h"
#include "components/guest_view/browser/guest_view_manager.h"
#include "components/guest_view/browser/guest_view_manager_delegate.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "content/public/browser/guest_page_holder.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/unowned_inner_web_contents_client.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/fenced_frame_test_util.h"
#include "dao/browser/performance/dao_high_performance_mode.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/base_event_utils.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

views::LabelButton* FindModeButton(
    views::View* view,
    int message_id = IDS_DAO_HIGH_PERFORMANCE_MODE) {
  if (auto* button = views::AsViewClass<views::LabelButton>(view);
      button && button->GetText() == l10n_util::GetStringUTF16(message_id)) {
    return button;
  }
  for (views::View* child : view->children()) {
    if (auto* button = FindModeButton(child, message_id)) {
      return button;
    }
  }
  return nullptr;
}

views::Button* FindHighPerformanceIndicator(DaoAddressBarView* address_bar) {
  for (views::View* child : address_bar->children()) {
    if (child->GetTooltipText() ==
        l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE_ENABLED)) {
      return views::AsViewClass<views::Button>(child);
    }
  }
  return nullptr;
}

views::Widget* FindIndicatorPopup(views::View* indicator) {
  for (views::Widget* widget : views::Widget::GetAllOwnedWidgets(
           indicator->GetWidget()->GetNativeView())) {
    auto* delegate = widget->widget_delegate()->AsBubbleDialogDelegate();
    if (!widget->IsClosed() && delegate &&
        delegate->GetAnchorView() == indicator) {
      return widget;
    }
  }
  return nullptr;
}

void ClickButton(views::Button* button) {
  ui::MouseEvent click(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(button).NotifyClick(click);
}

bool HasLabelText(views::View* view, std::u16string_view text) {
  if (auto* label = views::AsViewClass<views::Label>(view);
      label && label->GetText() == text) {
    return true;
  }
  for (views::View* child : view->children()) {
    if (HasLabelText(child, text)) {
      return true;
    }
  }
  return false;
}

class TestGuestView : public guest_view::GuestViewBase {
 public:
  explicit TestGuestView(content::RenderFrameHost* owner)
      : GuestViewBase(owner) {}

  std::unique_ptr<content::GuestPageHolder> CreatePage() {
    auto page = content::GuestPageHolder::Create(
        owner_web_contents(), content::SiteInstance::Create(browser_context()),
        GetGuestPageHolderDelegateWeakPtr());
    InitWithGuestPageHolder(base::DictValue(), page.get());
    return page;
  }

  std::string_view GetViewType() const override { return "test"; }
  const char* GetAPINamespace() const override { return ""; }
  int GetTaskPrefix() const override { return 0; }
  void MaybeRecreateGuestContents(content::RenderFrameHost*) override {
    ADD_FAILURE() << "Unexpected guest recreation";
  }
  void CreateInnerPage(std::unique_ptr<GuestViewBase>,
                       scoped_refptr<content::SiteInstance>,
                       const base::DictValue&,
                       GuestPageCreatedCallback) override {
    ADD_FAILURE() << "The test creates its guest page directly";
  }
  bool GuestHandleContextMenu(content::RenderFrameHost&,
                              const content::ContextMenuParams&) override {
    return false;
  }
};

}  // namespace

class DaoHighPerformanceBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    content::IsolateAllSitesForTesting(command_line);
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
  }
};

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       BackgroundPageKeepsTimersAndAnimationRunning) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  ASSERT_EQ(content::Visibility::HIDDEN, contents->GetVisibility());
  EXPECT_EQ(false, content::EvalJs(contents, "document.hidden"));
  auto page =
      performance_manager::PerformanceManager::GetPrimaryPageNodeForWebContents(
          contents);
  ASSERT_TRUE(page);
  EXPECT_TRUE(page->IsVisible());

  // A freeze request must not stop timers or animation on a protected tab.
  contents->SetPageFrozen(true);
  EXPECT_EQ(true, content::EvalJs(contents, R"(
    new Promise(resolve => setTimeout(() =>
      requestAnimationFrame(() => resolve(true)), 20))
  )"));

  DaoHighPerformanceMode::SetEnabled(contents, false);
  EXPECT_EQ(true, content::EvalJs(contents, "document.hidden"));
  EXPECT_FALSE(page->IsVisible());
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       MinimizedWindowKeepsAnimationRunning) {
#if BUILDFLAG(IS_MAC)
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  auto* widget = BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  base::ScopedClosureRunner restore_window(
      base::BindOnce(&views::Widget::Restore, base::Unretained(widget)));
  widget->Minimize();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return widget->IsMinimized() &&
           contents->GetVisibility() != content::Visibility::VISIBLE;
  }));
  EXPECT_EQ(false, content::EvalJs(contents, "document.hidden"));

  // Submit enough damaged frames to exceed Viz's undrawn-frame limit.
  const auto elapsed_ms = content::EvalJs(contents, R"(
    new Promise(resolve => {
      let frames = 0;
      let firstFrameTime;
      function animate(timestamp) {
        if (frames === 0) {
          firstFrameTime = timestamp;
        }
        document.body.style.backgroundColor = frames % 2 ? 'red' : 'blue';
        if (++frames === 12) {
          resolve(timestamp - firstFrameTime);
          return;
        }
        requestAnimationFrame(animate);
      }
      requestAnimationFrame(animate);
    })
  )");
  ASSERT_TRUE(elapsed_ms.is_ok()) << elapsed_ms;
  // Exclude first-frame startup; allow slow bots while rejecting 1 fps.
  EXPECT_LT(elapsed_ms.ExtractDouble(), 4000.0);
  EXPECT_TRUE(widget->IsMinimized());
  restore_window.RunAndReset();
  EXPECT_TRUE(base::test::RunUntil([&] { return !widget->IsMinimized(); }));
#else
  GTEST_SKIP() << "Exercises the macOS native window minimization path.";
#endif
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       RejectsDiscardUntilModeIsDisabled) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(contents, "window.savedState = 'still here'"));
  DaoHighPerformanceMode::SetEnabled(contents, true);
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  ASSERT_EQ(content::Visibility::HIDDEN, contents->GetVisibility());
  auto* lifecycle =
      resource_coordinator::TabLifecycleUnitSource::GetTabLifecycleUnitExternal(
          contents);
  ASSERT_TRUE(lifecycle);
  for (auto reason :
       {mojom::LifecycleUnitDiscardReason::EXTERNAL,
        mojom::LifecycleUnitDiscardReason::URGENT,
        mojom::LifecycleUnitDiscardReason::PROACTIVE,
        mojom::LifecycleUnitDiscardReason::SUGGESTED,
        mojom::LifecycleUnitDiscardReason::FROZEN_WITH_GROWING_MEMORY}) {
    EXPECT_FALSE(lifecycle->DiscardTab(reason, 0));
  }
  EXPECT_EQ("still here", content::EvalJs(contents, "window.savedState"));
  DaoHighPerformanceMode::SetEnabled(contents, false);
  EXPECT_TRUE(
      lifecycle->DiscardTab(mojom::LifecycleUnitDiscardReason::EXTERNAL, 0));
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       PriorityBoostDoesNotOverwriteNormalPriority) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* process = contents->GetPrimaryMainFrame()->GetProcess();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  ASSERT_EQ(content::Visibility::HIDDEN, contents->GetVisibility());
  process->SetPriorityOverride(base::Process::Priority::kBestEffort);
  EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());
  DaoHighPerformanceMode::SetEnabled(contents, false);
  EXPECT_EQ(base::Process::Priority::kBestEffort, process->GetPriority());
  process->ClearPriorityOverride();
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       ModeSurvivesNavigationAndRemainsTabScoped) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("b.test", "/title2.html")));
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(contents));
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  ASSERT_EQ(content::Visibility::HIDDEN, contents->GetVisibility());
  EXPECT_FALSE(DaoHighPerformanceMode::IsEnabled(
      browser()->tab_strip_model()->GetActiveWebContents()));
  EXPECT_EQ(false, content::EvalJs(contents, "document.hidden"));
  EXPECT_EQ(true, content::EvalJs(contents, R"(
    new Promise(resolve => requestAnimationFrame(() => resolve(true)))
  )"));
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       NewCrossSiteSubframeInheritsForegroundPriority) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("a.test", "/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  ASSERT_TRUE(content::ExecJs(
      contents, content::JsReplace(
                    R"(
    new Promise(resolve => {
      const frame = document.createElement('iframe');
      frame.src = $1;
      frame.onload = resolve;
      document.body.append(frame);
    })
  )",
                    embedded_test_server()->GetURL("b.test", "/title1.html"))));
  auto* frame = content::ChildFrameAt(contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(frame);
  auto* process = frame->GetProcess();
  ASSERT_NE(contents->GetPrimaryMainFrame()->GetProcess(), process);
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  ASSERT_EQ(content::Visibility::HIDDEN, contents->GetVisibility());
  process->SetPriorityOverride(base::Process::Priority::kBestEffort);
  EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());
  DaoHighPerformanceMode::SetEnabled(contents, false);
  EXPECT_EQ(base::Process::Priority::kBestEffort, process->GetPriority());
  process->ClearPriorityOverride();
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       MenuToggleUpdatesCheckedStateAndAddressIndicator) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* popup = browser_view->dao_control_center_popup();
  auto* address_bar = browser_view->dao_address_bar();
  ASSERT_TRUE(popup);
  ASSERT_TRUE(address_bar);
  views::View* indicator = nullptr;
  for (views::View* child : address_bar->children()) {
    if (child->GetTooltipText() ==
        l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE_ENABLED)) {
      indicator = child;
      break;
    }
  }
  ASSERT_TRUE(indicator);
  EXPECT_FALSE(indicator->GetVisible());
  popup->ShowAt(gfx::Point(100, 100));
  popup->ShowMoreMenu();
  auto* button = FindModeButton(popup);
  ASSERT_TRUE(button);
  ui::MouseEvent click(ui::EventType::kMousePressed, gfx::Point(), gfx::Point(),
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  views::test::ButtonTestApi(button).NotifyClick(click);
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(
      browser()->tab_strip_model()->GetActiveWebContents()));
  EXPECT_TRUE(indicator->GetVisible());
  ui::AXNodeData data;
  button->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(ax::mojom::CheckedState::kTrue, data.GetCheckedState());

  views::test::ButtonTestApi(button).NotifyClick(click);
  EXPECT_FALSE(DaoHighPerformanceMode::IsEnabled(
      browser()->tab_strip_model()->GetActiveWebContents()));
  EXPECT_FALSE(indicator->GetVisible());
  button->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(ax::mojom::CheckedState::kFalse, data.GetCheckedState());

  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  EXPECT_TRUE(indicator->GetVisible());
  button->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(ax::mojom::CheckedState::kTrue, data.GetCheckedState());
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  EXPECT_FALSE(indicator->GetVisible());
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(contents));
  EXPECT_TRUE(indicator->GetVisible());
  DaoHighPerformanceMode::SetEnabled(contents, false);
  EXPECT_FALSE(indicator->GetVisible());
  popup->Hide();
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       LongUrlKeepsIndicatorVisible) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL(
                     "/title1.html?query=" + std::string(2048, 'a'))));
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* address_bar = browser_view->dao_address_bar();
  ASSERT_TRUE(address_bar);
  browser_view->DeprecatedLayoutImmediately();
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* indicator = FindHighPerformanceIndicator(address_bar);
  ASSERT_TRUE(indicator);
  auto* control_center = address_bar->control_center_button();
  ASSERT_TRUE(control_center);

  for (int width : {800, 400}) {
    SCOPED_TRACE(width);
    DaoHighPerformanceMode::SetEnabled(contents, false);
    address_bar->SetSize(gfx::Size(width, DaoAddressBarView::kBarHeight));
    address_bar->DeprecatedLayoutImmediately();
    DaoHighPerformanceMode::SetEnabled(contents, true);
    address_bar->DeprecatedLayoutImmediately();

    EXPECT_TRUE(indicator->GetVisible());
    EXPECT_EQ(indicator->GetPreferredSize().width(), indicator->width());
    EXPECT_TRUE(address_bar->GetLocalBounds().Contains(indicator->bounds()));
    const gfx::Rect url_bounds = address_bar->url_container_bounds();
    EXPECT_FALSE(url_bounds.IsEmpty());
    EXPECT_LE(indicator->bounds().right(), url_bounds.x());
    EXPECT_LE(url_bounds.right(), control_center->x());
    EXPECT_TRUE(control_center->GetVisible());
    EXPECT_TRUE(
        address_bar->GetLocalBounds().Contains(control_center->bounds()));
  }
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       IndicatorPopupExplainsModeAndTurnsItOff) {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser());
  auto* address_bar = browser_view->dao_address_bar();
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  auto* indicator = FindHighPerformanceIndicator(address_bar);
  ASSERT_TRUE(indicator);
  ClickButton(indicator);
  auto* popup = FindIndicatorPopup(indicator);
  ASSERT_TRUE(popup);
  EXPECT_TRUE(popup->IsVisible());
  EXPECT_EQ(l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE_ENABLED),
            popup->widget_delegate()->GetWindowTitle());
  EXPECT_TRUE(HasLabelText(
      popup->GetContentsView(),
      l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE_DESCRIPTION)));
  EXPECT_TRUE(HasLabelText(
      popup->GetContentsView(),
      l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE_RESOURCE_USAGE)));
  auto* turn_off = FindModeButton(popup->GetContentsView(),
                                  IDS_DAO_HIGH_PERFORMANCE_MODE_TURN_OFF);
  ASSERT_TRUE(turn_off);
  ClickButton(turn_off);
  EXPECT_FALSE(DaoHighPerformanceMode::IsEnabled(contents));
  EXPECT_FALSE(indicator->GetVisible());
  EXPECT_FALSE(FindIndicatorPopup(indicator));

  auto* control_center = browser_view->dao_control_center_popup();
  control_center->ShowAt(gfx::Point(100, 100));
  control_center->ShowMoreMenu();
  auto* menu_button = FindModeButton(control_center);
  ASSERT_TRUE(menu_button);
  ui::AXNodeData data;
  menu_button->GetViewAccessibility().GetAccessibleNodeData(&data);
  EXPECT_EQ(ax::mojom::CheckedState::kFalse, data.GetCheckedState());
  control_center->Hide();
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       IndicatorPopupDoesNotReopenAfterAnchorDeactivation) {
  auto* address_bar =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_address_bar();
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(contents, true);
  auto* indicator = FindHighPerformanceIndicator(address_bar);
  ASSERT_TRUE(indicator);
  ClickButton(indicator);
  auto* popup = FindIndicatorPopup(indicator);
  ASSERT_TRUE(popup);
  views::ViewTracker popup_tracker(popup->GetContentsView());

  // Cocoa closes the bubble before dispatching the anchor's mouse press.
  popup->CloseWithReason(views::Widget::ClosedReason::kLostFocus);
  const gfx::Point point = indicator->GetLocalBounds().CenterPoint();
  ui::MouseEvent press(ui::EventType::kMousePressed, point, point,
                       ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                       ui::EF_LEFT_MOUSE_BUTTON);
  ui::MouseEvent release(ui::EventType::kMouseReleased, point, point,
                         ui::EventTimeForNow(), ui::EF_LEFT_MOUSE_BUTTON,
                         ui::EF_LEFT_MOUSE_BUTTON);
  auto* view = static_cast<views::View*>(indicator);
  view->OnMousePressed(press);
  ASSERT_TRUE(base::test::RunUntil([&] { return !popup_tracker.view(); }));
  view->OnMouseReleased(release);
  EXPECT_FALSE(FindIndicatorPopup(indicator));
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(contents));

  view->OnMousePressed(press);
  view->OnMouseReleased(release);
  EXPECT_TRUE(FindIndicatorPopup(indicator));
}

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceBrowserTest,
                       IndicatorPopupClosesWithoutAffectingOtherTabs) {
  auto* address_bar =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_address_bar();
  auto* tabs = browser()->tab_strip_model();
  auto* original = tabs->GetActiveWebContents();
  DaoHighPerformanceMode::SetEnabled(original, true);
  auto* indicator = FindHighPerformanceIndicator(address_bar);
  ASSERT_TRUE(indicator);
  ClickButton(indicator);
  ASSERT_TRUE(FindIndicatorPopup(indicator));
  ClickButton(indicator);
  EXPECT_FALSE(FindIndicatorPopup(indicator));
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(original));

  ClickButton(indicator);
  auto* popup = FindIndicatorPopup(indicator);
  ASSERT_TRUE(popup);
  ui::KeyEvent escape(ui::EventType::kKeyPressed, ui::VKEY_ESCAPE, ui::EF_NONE);
  popup->OnKeyEvent(&escape);
  EXPECT_TRUE(
      base::test::RunUntil([&] { return !FindIndicatorPopup(indicator); }));
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(original));

  ClickButton(indicator);
  ASSERT_TRUE(FindIndicatorPopup(indicator));
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  EXPECT_FALSE(FindIndicatorPopup(indicator));
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(original));
  EXPECT_FALSE(DaoHighPerformanceMode::IsEnabled(tabs->GetActiveWebContents()));

  DaoHighPerformanceMode::SetEnabled(tabs->GetActiveWebContents(), true);
  ClickButton(indicator);
  ASSERT_TRUE(FindIndicatorPopup(indicator));
  DaoHighPerformanceMode::SetEnabled(tabs->GetActiveWebContents(), false);
  EXPECT_FALSE(FindIndicatorPopup(indicator));
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(original));

  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(original));
  ClickButton(indicator);
  ASSERT_TRUE(FindIndicatorPopup(indicator));
  tabs->CloseWebContentsAt(tabs->active_index(), TabCloseTypes::CLOSE_NONE);
  EXPECT_FALSE(FindIndicatorPopup(indicator));
  EXPECT_FALSE(indicator->GetVisible());
}

class DaoHighPerformanceFencedFrameBrowserTest
    : public DaoHighPerformanceBrowserTest {
 protected:
  content::test::FencedFrameTestHelper fenced_frame_helper_;
};

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceFencedFrameBrowserTest,
                       ExistingAndNewFramesInheritPriority) {
  embedded_https_test_server().SetCertHostnames({"a.test", "b.test", "c.test"});
  ASSERT_TRUE(embedded_https_test_server().Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_https_test_server().GetURL("a.test", "/title1.html")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  auto* existing = fenced_frame_helper_.CreateFencedFrame(
      contents->GetPrimaryMainFrame(),
      embedded_https_test_server().GetURL("b.test",
                                          "/fenced_frames/title1.html"));
  ASSERT_TRUE(existing);
  DaoHighPerformanceMode::SetEnabled(contents, true);
  auto* created = fenced_frame_helper_.CreateFencedFrame(
      contents->GetPrimaryMainFrame(),
      embedded_https_test_server().GetURL("c.test",
                                          "/fenced_frames/title1.html"));
  ASSERT_TRUE(created);
  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  for (auto* frame : {existing, created}) {
    auto* process = frame->GetProcess();
    ASSERT_NE(contents->GetPrimaryMainFrame()->GetProcess(), process);
    process->SetPriorityOverride(base::Process::Priority::kBestEffort);
    EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());
  }
  DaoHighPerformanceMode::SetEnabled(contents, false);
  for (auto* frame : {existing, created}) {
    EXPECT_EQ(base::Process::Priority::kBestEffort,
              frame->GetProcess()->GetPriority());
    frame->GetProcess()->ClearPriorityOverride();
  }
}

class DaoHighPerformanceInnerContentsBrowserTest
    : public DaoHighPerformanceBrowserTest {
 private:
  base::test::ScopedFeatureList scoped_feature_list_{
      features::kAttachUnownedInnerWebContents};
};

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceInnerContentsBrowserTest,
                       InheritsModeUntilDetached) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("a.test", "/page_with_iframe.html")));
  auto* outer = browser()->tab_strip_model()->GetActiveWebContents();
  auto* container = content::ChildFrameAt(outer->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(container);
  content::WebContents::CreateParams params(outer->GetBrowserContext());
  params.initially_hidden = true;
  auto inner = content::WebContents::Create(params);
  ASSERT_TRUE(content::NavigateToURL(
      inner.get(), embedded_test_server()->GetURL("b.test", "/title1.html")));
  DaoHighPerformanceMode::SetEnabled(outer, true);
  outer->AttachUnownedInnerWebContents(
      content::UnownedInnerWebContentsClient::GetPassKeyForTesting(),
      inner.get(), container);
  EXPECT_TRUE(DaoHighPerformanceMode::IsEnabled(inner.get()));
  EXPECT_EQ(false, content::EvalJs(inner.get(), "document.hidden"));

  chrome::NewTab(browser(), NewTabTypes::kNoUserAction);
  auto* process = inner->GetPrimaryMainFrame()->GetProcess();
  process->SetPriorityOverride(base::Process::Priority::kBestEffort);
  EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());
  EXPECT_EQ(false, content::EvalJs(inner.get(), "document.hidden"));
  DaoHighPerformanceMode::SetEnabled(outer, false);
  EXPECT_EQ(true, content::EvalJs(inner.get(), "document.hidden"));
  EXPECT_EQ(base::Process::Priority::kBestEffort, process->GetPriority());
  process->ClearPriorityOverride();

  DaoHighPerformanceMode::SetEnabled(outer, true);
  ASSERT_TRUE(content::NavigateToURLFromRenderer(
      inner.get(), embedded_test_server()->GetURL("c.test", "/title1.html")));
  EXPECT_EQ(false, content::EvalJs(inner.get(), "document.hidden"));
  process = inner->GetPrimaryMainFrame()->GetProcess();
  process->SetPriorityOverride(base::Process::Priority::kBestEffort);
  EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());

  outer->DetachUnownedInnerWebContents(
      content::UnownedInnerWebContentsClient::GetPassKeyForTesting(),
      inner.get());
  EXPECT_FALSE(DaoHighPerformanceMode::IsEnabled(inner.get()));
  EXPECT_EQ(true, content::EvalJs(inner.get(), "document.hidden"));
  EXPECT_EQ(base::Process::Priority::kBestEffort, process->GetPriority());
  process->ClearPriorityOverride();
}

class DaoHighPerformanceGuestPageBrowserTest
    : public DaoHighPerformanceBrowserTest {
 private:
  base::test::ScopedFeatureList scoped_feature_list_{
      features::kGuestViewMPArch};
};

IN_PROC_BROWSER_TEST_F(DaoHighPerformanceGuestPageBrowserTest,
                       InheritsPriorityAcrossGuestNavigation) {
  ASSERT_TRUE(embedded_test_server()->Start());
  // MPArch guests are not yet supported by Chrome's page-load metrics helper.
  content::WebContents::CreateParams params(browser()
                                                ->tab_strip_model()
                                                ->GetActiveWebContents()
                                                ->GetBrowserContext());
  params.initially_hidden = true;
  auto contents = content::WebContents::Create(params);
  ASSERT_TRUE(content::NavigateToURL(
      contents.get(),
      embedded_test_server()->GetURL("a.test", "/page_with_iframe.html")));
  auto* container = content::ChildFrameAt(contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(container);
  guest_view::GuestViewManager::CreateWithDelegate(
      contents->GetBrowserContext(),
      std::make_unique<guest_view::GuestViewManagerDelegate>());
  TestGuestView guest_view(contents->GetPrimaryMainFrame());
  auto guest = guest_view.CreatePage();
  auto* guest_page = guest.get();
  contents->AttachGuestPage(std::move(guest), container);
  ASSERT_TRUE(content::NavigateToURLFromRenderer(
      guest_page->GetGuestMainFrame(),
      embedded_test_server()->GetURL("b.test", "/title1.html")));
  DaoHighPerformanceMode::SetEnabled(contents.get(), true);
  auto* process = guest_page->GetGuestMainFrame()->GetProcess();
  ASSERT_NE(contents->GetPrimaryMainFrame()->GetProcess(), process);
  process->SetPriorityOverride(base::Process::Priority::kBestEffort);
  EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());
  EXPECT_EQ(false, content::EvalJs(guest_page->GetGuestMainFrame(),
                                   "document.hidden"));
  process->ClearPriorityOverride();

  ASSERT_TRUE(content::NavigateToURLFromRenderer(
      guest_page->GetGuestMainFrame(),
      embedded_test_server()->GetURL("c.test", "/title1.html")));
  process = guest_page->GetGuestMainFrame()->GetProcess();
  process->SetPriorityOverride(base::Process::Priority::kBestEffort);
  EXPECT_EQ(base::Process::Priority::kUserBlocking, process->GetPriority());
  DaoHighPerformanceMode::SetEnabled(contents.get(), false);
  EXPECT_EQ(base::Process::Priority::kBestEffort, process->GetPriority());
  EXPECT_EQ(true, content::EvalJs(guest_page->GetGuestMainFrame(),
                                  "document.hidden"));
  process->ClearPriorityOverride();
}

}  // namespace dao
