// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

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
