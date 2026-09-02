// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_page_tools.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/timer/elapsed_timer.h"
#include "base/timer/timer.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/automation/dao_agent_lease_manager.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "net/dns/mock_host_resolver.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/views/controls/webview/webview.h"

namespace dao {
namespace {

template <typename T> T *FindDescendantViewOfClass(views::View *root) {
  if (!root) {
    return nullptr;
  }
  if (auto *view = views::AsViewClass<T>(root)) {
    return view;
  }
  for (views::View *child : root->children()) {
    if (auto *view = FindDescendantViewOfClass<T>(child)) {
      return view;
    }
  }
  return nullptr;
}

std::string CallAgentNativeField(content::WebContents *agent_contents,
                                 const std::string &method,
                                 const std::string &field,
                                 const std::string &params_json = "{}") {
  const std::string script = base::StrCat({
      R"JS(
    (async () => {
      const method = ')JS",
      method,
      R"JS(';
      const field = ')JS",
      field,
      R"JS(';
      const params = JSON.parse(')JS",
      params_json,
      R"JS(');
      const result = await new Promise(resolve => {
        const id = method + '_page_tools_browser_test_' +
            Math.random().toString(36).slice(2);
        const cr = window.cr || (window.cr = {});
        const previous = cr.webUIResponse;
        cr.webUIResponse = (callbackId, isSuccess, payload) => {
          if (callbackId !== id) {
            if (previous) {
              previous(callbackId, isSuccess, payload);
            }
            return;
          }
          cr.webUIResponse = previous;
          resolve({isSuccess, payload: payload || {}});
        };
        chrome.send(method, [id, params]);
      });
      const payload = result.payload || {};
      if (field === 'success') {
        return String(!!payload.success);
      }
      if (typeof payload[field] === 'boolean') {
        return String(payload[field]);
      }
      if (typeof payload[field] === 'number') {
        return String(payload[field]);
      }
      return String(payload[field] || payload.error || '');
    })()
  )JS"});
  return content::EvalJs(agent_contents, script).ExtractString();
}

class TestPageUiDelegate : public DaoPageTools::UiDelegate {
public:
  void MoveCursor(content::WebContents *target, double x, double y,
                  base::OnceCallback<void(bool)> callback) override {
    ++move_count_;
    if (respect_target_activation_ && !CanAnimateTarget(target)) {
      std::move(callback).Run(false);
      return;
    }
    ++animation_count_;
    if (hold_next_cursor_move_) {
      hold_next_cursor_move_ = false;
      pending_cursor_callback_ = std::move(callback);
      if (cursor_ready_) {
        std::move(cursor_ready_).Run();
      }
      return;
    }
    std::move(callback).Run(target != nullptr);
  }

  void PlayClickRipple(content::WebContents *target) override {
    if (target && (!respect_target_activation_ || CanAnimateTarget(target))) {
      ++ripple_count_;
    }
  }

  void CancelCursor(content::WebContents *target) override {
    if (target) {
      ++cancel_cursor_count_;
    }
    pending_cursor_callback_.Reset();
  }

  bool IsTargetLocked(content::WebContents *target) override {
    return DaoAgentLockTabHelper::IsLocked(target);
  }

  void LockTarget(content::WebContents *target) override {
    DaoAgentLockTabHelper::LockContents(target);
  }

  void UnlockTarget(content::WebContents *target) override {
    DaoAgentLockTabHelper::UnlockContents(target);
  }

  int move_count() const { return move_count_; }
  int animation_count() const { return animation_count_; }
  int ripple_count() const { return ripple_count_; }
  int cancel_cursor_count() const { return cancel_cursor_count_; }

  void RespectTargetActivation() { respect_target_activation_ = true; }

  void HoldNextCursorMove(base::OnceClosure ready) {
    hold_next_cursor_move_ = true;
    cursor_ready_ = std::move(ready);
  }

  void CompletePendingCursorMove(bool moved) {
    ASSERT_TRUE(pending_cursor_callback_);
    auto callback = std::move(pending_cursor_callback_);
    std::move(callback).Run(moved);
  }

private:
  bool CanAnimateTarget(content::WebContents *target) const {
    Browser *browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
    BrowserView *browser_view =
        browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
    return browser_view && browser_view->GetActiveWebContents() == target;
  }

  int move_count_ = 0;
  int animation_count_ = 0;
  int ripple_count_ = 0;
  int cancel_cursor_count_ = 0;
  bool respect_target_activation_ = false;
  bool hold_next_cursor_move_ = false;
  base::OnceClosure cursor_ready_;
  base::OnceCallback<void(bool)> pending_cursor_callback_;
};

class DaoMcpPageToolsBrowserTest : public InProcessBrowserTest {
protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    devtools_client_ = std::make_unique<DaoDevToolsClient>();
    executor_ = std::make_unique<DaoBrowserToolExecutor>(devtools_client_.get(),
                                                         &ui_delegate_);
  }

  void TearDownOnMainThread() override {
    executor_.reset();
    devtools_client_.reset();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  GURL first_url() const {
    return embedded_test_server()->GetURL("/title2.html");
  }

  GURL second_url() const {
    return embedded_test_server()->GetURL("/title1.html");
  }

  std::unique_ptr<DaoBrowserAutomationSession> MakeSessionForActiveTab() {
    content::WebContents *target =
        browser()->tab_strip_model()->GetActiveWebContents();
    CHECK(target);
    return std::make_unique<DaoBrowserAutomationSession>(browser(), target);
  }

  DaoBrowserToolResult Execute(DaoBrowserAutomationSession *session,
                               std::string name,
                               base::DictValue arguments = base::DictValue(),
                               base::TimeDelta timeout = base::Seconds(5)) {
    DaoBrowserToolCall call;
    call.request_id =
        "page-browser-test-" + base::NumberToString(++request_id_);
    call.name = std::move(name);
    call.arguments = std::move(arguments);
    call.timeout = timeout;
    base::test::TestFuture<DaoBrowserToolResult> future;
    executor_->Execute(session, DaoToolClient::kMcp, std::move(call),
                       future.GetCallback());
    return future.Take();
  }

  content::WebContents *LoadAgentWebUi() {
    auto *sidebar =
        BrowserView::GetBrowserViewForBrowser(browser())->dao_agent_sidebar();
    CHECK(sidebar);
    auto *web_view = FindDescendantViewOfClass<views::WebView>(sidebar);
    CHECK(web_view);
    content::WebContents *agent_contents = web_view->GetWebContents();
    CHECK(agent_contents);

    const GURL agent_url("chrome://agent/");
    if (agent_contents->GetLastCommittedURL() != agent_url) {
      content::TestNavigationObserver observer(agent_contents);
      sidebar->Toggle();
      observer.Wait();
    } else if (!sidebar->is_expanded()) {
      sidebar->Toggle();
    }
    CHECK(content::WaitForLoadStop(agent_contents));
    CHECK(content::EvalJs(agent_contents, R"(
      (async () => {
        await customElements.whenDefined('dao-agent-app');
        return true;
      })()
    )")
              .ExtractBool());
    return agent_contents;
  }

  TestPageUiDelegate ui_delegate_;
  std::unique_ptr<DaoDevToolsClient> devtools_client_;
  std::unique_ptr<DaoBrowserToolExecutor> executor_;
  int request_id_ = 0;
};

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       RegistersExactlyTheSixteenSharedPageTools) {
  constexpr std::array<std::string_view, 16> kSharedPageTools = {
      "get_page_info",     "get_page_html",      "get_accessibility_tree",
      "query_elements",    "capture_screenshot", "click_element",
      "agent_click",       "click_by_ref",       "move_cursor",
      "highlight_element", "scroll_down",        "scroll_up",
      "scroll_to_element", "press_key_chord",    "type_text",
      "execute_script",
  };

  for (std::string_view name : kSharedPageTools) {
    EXPECT_TRUE(DaoPageTools::Handles(name)) << name;
  }
  EXPECT_FALSE(DaoPageTools::Handles("resolve_element_context"));
  EXPECT_FALSE(DaoPageTools::Handles("list_tabs"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       QueryElementsSupportsGuardedClick) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents* target = session->ResolveTarget().value();
  ASSERT_TRUE(content::ExecJs(target, R"(
    window.__dao_guarded_clicks = 0;
    document.body.innerHTML =
        '<section aria-label="Story panel"><button>Old</button></section>' +
        '<section aria-label="Story panel"><button id="live">Open door</button></section>';
    document.getElementById('live').onclick = () => ++window.__dao_guarded_clicks;
  )"));

  DaoBrowserToolResult query = Execute(
      session.get(), "query_elements",
      base::DictValue()
          .Set("scope", base::DictValue()
                            .Set("selector", "[aria-label=\"Story panel\"]")
                            .Set("nth", "last"))
          .Set("role", "button")
          .Set("text", "Open door")
          .Set("text_match", "exact")
          .Set("visible", true)
          .Set("enabled", true)
          .Set("max_results", 3)
          .Set("require_count", 1));

  ASSERT_TRUE(query.ok) << query.error->message;
  const base::DictValue& data = query.data.GetDict();
  const base::ListValue* matches = data.FindList("matches");
  ASSERT_NE(nullptr, matches);
  ASSERT_EQ(1u, matches->size());
  const base::DictValue& match = (*matches)[0].GetDict();
  base::DictValue click_arguments;
  click_arguments.Set("ref_id", *match.FindString("ref_id"));
  click_arguments.Set("document_id", *data.FindString("document_id"));
  click_arguments.Set("snapshot_id", *data.FindString("snapshot_id"));
  click_arguments.Set(
      "preconditions",
      base::DictValue()
          .Set("url", first_url().spec())
          .Set("visible", true)
          .Set("enabled", true)
          .Set("text", "Open door")
          .Set("role", "button")
          .Set("ancestor_ref", *data.FindString("scope_ref_id")));

  DaoBrowserToolResult clicked =
      Execute(session.get(), "click_by_ref", click_arguments.Clone());
  ASSERT_TRUE(clicked.ok) << clicked.error->message;
  EXPECT_EQ(
      1, content::EvalJs(target, "window.__dao_guarded_clicks").ExtractInt());

  ASSERT_TRUE(content::ExecJs(
      target, "document.getElementById('live').textContent = 'Changed';"));
  DaoBrowserToolResult guarded =
      Execute(session.get(), "click_by_ref", std::move(click_arguments));
  ASSERT_FALSE(guarded.ok);
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, guarded.error->code);
  EXPECT_EQ(
      1, content::EvalJs(target, "window.__dao_guarded_clicks").ExtractInt());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       QueryElementsRejectsStaleRefScope) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents* target = session->ResolveTarget().value();
  ASSERT_TRUE(content::ExecJs(target, R"(
    document.body.innerHTML =
        '<section id="first"><button>First</button></section>' +
        '<section id="second"><button>Second</button></section>';
  )"));

  DaoBrowserToolResult first = Execute(
      session.get(), "query_elements",
      base::DictValue()
          .Set("scope", base::DictValue().Set("selector", "#first"))
          .Set("role", "button")
          .Set("require_count", 1));
  ASSERT_TRUE(first.ok) << first.error->message;
  const base::DictValue& first_data = first.data.GetDict();
  const std::string stale_ref = *first_data.FindString("scope_ref_id");
  const std::string stale_document = *first_data.FindString("document_id");
  const std::string stale_snapshot = *first_data.FindString("snapshot_id");

  DaoBrowserToolResult second = Execute(
      session.get(), "query_elements",
      base::DictValue()
          .Set("scope", base::DictValue().Set("selector", "#second"))
          .Set("role", "button")
          .Set("require_count", 1));
  ASSERT_TRUE(second.ok) << second.error->message;

  DaoBrowserToolResult stale = Execute(
      session.get(), "query_elements",
      base::DictValue()
          .Set("scope", base::DictValue()
                            .Set("ref_id", stale_ref)
                            .Set("document_id", stale_document)
                            .Set("snapshot_id", stale_snapshot))
          .Set("role", "button")
          .Set("require_count", 1));

  ASSERT_FALSE(stale.ok);
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, stale.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       GetPageInfoReturnsNormalizedTargetData) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();

  DaoBrowserToolResult result = Execute(session.get(), "get_page_info");

  ASSERT_TRUE(result.ok) << result.error->message;
  ASSERT_TRUE(result.data.is_dict());
  EXPECT_EQ(first_url().spec(), *result.data.GetDict().FindString("url"));
  EXPECT_EQ("Title Of Awesomeness", *result.data.GetDict().FindString("title"));
  EXPECT_FALSE(result.data.GetDict().FindString("tab_id")->empty());
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(first_url().spec(), result.target->url);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ExecuteScriptUsesPinnedTarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  chrome::AddTabAt(browser(), second_url(), 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));
  browser()->tab_strip_model()->ActivateTabAt(1);

  DaoBrowserToolResult result =
      Execute(session.get(), "execute_script",
              base::DictValue().Set("code", "document.title"));

  ASSERT_TRUE(result.ok) << result.error->message;
  ASSERT_TRUE(result.data.is_dict());
  EXPECT_EQ("Title Of Awesomeness",
            *result.data.GetDict().FindString("result"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ForegroundCursorAnimatesAndClickRipples) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::ExecJs(
      target,
      "window.__dao_clicks=0;document.body.style.minHeight='500px';"
      "document.body.addEventListener('click',()=>window.__dao_clicks++);"));
  ui_delegate_.RespectTargetActivation();
  base::RunLoop cursor_ready;
  ui_delegate_.HoldNextCursorMove(cursor_ready.QuitClosure());
  DaoBrowserToolCall call;
  call.request_id = "foreground-animated-click";
  call.name = "agent_click";
  call.arguments = base::DictValue().Set("selector", "body");
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  cursor_ready.Run();

  EXPECT_FALSE(future.IsReady());
  EXPECT_EQ(1, ui_delegate_.animation_count());
  ui_delegate_.CompletePendingCursorMove(true);
  DaoBrowserToolResult result = future.Take();

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(1, ui_delegate_.ripple_count());
  EXPECT_EQ(1, content::EvalJs(target, "window.__dao_clicks").ExtractInt());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       BackgroundCursorIsNoopAndClickStaysPinned) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::ExecJs(
      target,
      "window.__dao_clicks=0;document.body.style.minHeight='500px';"
      "document.body.addEventListener('click',()=>window.__dao_clicks++);"));
  chrome::AddTabAt(browser(), second_url(), 1, true);
  content::WebContents *foreground =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(foreground));
  ASSERT_NE(target, foreground);
  ui_delegate_.RespectTargetActivation();

  DaoBrowserToolResult moved =
      Execute(session.get(), "move_cursor",
              base::DictValue().Set("x", 10).Set("y", 20));
  ASSERT_TRUE(moved.ok) << moved.error->message;
  EXPECT_EQ(0, ui_delegate_.animation_count());

  DaoBrowserToolResult clicked =
      Execute(session.get(), "agent_click",
              base::DictValue().Set("selector", "body"));
  ASSERT_TRUE(clicked.ok) << clicked.error->message;
  EXPECT_EQ(0, ui_delegate_.animation_count());
  EXPECT_EQ(0, ui_delegate_.ripple_count());
  EXPECT_EQ(1, content::EvalJs(target, "window.__dao_clicks").ExtractInt());
  EXPECT_EQ(foreground, browser()->tab_strip_model()->GetActiveWebContents());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       BackgroundClickReportsDevToolsFailure) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  chrome::AddTabAt(browser(), second_url(), 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));
  ui_delegate_.RespectTargetActivation();

  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](DaoDevToolsClient* client, const std::string& method) {
        if (method == "Input.dispatchMouseEvent") {
          client->Detach();
        }
      },
      devtools_client_.get()));
  DaoBrowserToolResult result =
      Execute(session.get(), "agent_click",
              base::DictValue().Set("selector", "body"));
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       TabSwitchDuringCursorMoveDoesNotLeakRipple) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::ExecJs(
      target,
      "window.__dao_clicks=0;document.body.style.minHeight='500px';"
      "document.body.addEventListener('click',()=>window.__dao_clicks++);"));
  ui_delegate_.RespectTargetActivation();
  base::RunLoop cursor_ready;
  ui_delegate_.HoldNextCursorMove(cursor_ready.QuitClosure());
  DaoBrowserToolCall call;
  call.request_id = "tab-switch-during-cursor-move";
  call.name = "agent_click";
  call.arguments = base::DictValue().Set("selector", "body");
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  cursor_ready.Run();

  chrome::AddTabAt(browser(), second_url(), 1, true);
  content::WebContents *foreground =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::WaitForLoadStop(foreground));
  ui_delegate_.CompletePendingCursorMove(false);
  DaoBrowserToolResult result = future.Take();

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(1, ui_delegate_.animation_count());
  EXPECT_EQ(0, ui_delegate_.ripple_count());
  EXPECT_EQ(1, content::EvalJs(target, "window.__dao_clicks").ExtractInt());
  EXPECT_EQ(foreground, browser()->tab_strip_model()->GetActiveWebContents());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       CaptureScreenshotReturnsNativeMedia) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();

  DaoBrowserToolResult result = Execute(session.get(), "capture_screenshot");

  ASSERT_TRUE(result.ok) << result.error->message;
  ASSERT_TRUE(result.media.has_value());
  EXPECT_EQ("image/jpeg", result.media->mime_type);
  EXPECT_FALSE(result.media->data.empty());
  ASSERT_TRUE(result.data.is_dict());
  EXPECT_EQ("jpeg", *result.data.GetDict().FindString("format"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ValidationPrecedesTargetEligibility) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));
  auto session = MakeSessionForActiveTab();

  DaoBrowserToolResult result =
      Execute(session.get(), "click_element", base::DictValue());

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       IneligiblePinnedTargetDoesNotFallback) {
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("chrome://version")));
  auto session = MakeSessionForActiveTab();
  chrome::AddTabAt(browser(), first_url(), 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));

  DaoBrowserToolResult result = Execute(session.get(), "get_page_info");

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, result.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       CancelAllCompletesPendingCdpExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(devtools_client_->AttachTo(
      browser()->tab_strip_model()->GetActiveWebContents()));

  int callback_count = 0;
  std::optional<DaoToolError> callback_error;
  base::DictValue params;
  params.Set("expression", "new Promise(() => {})");
  params.Set("awaitPromise", true);
  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](int *count, std::optional<DaoToolError> *error,
             DaoDevToolsClient::CommandResult result) {
            ++*count;
            if (!result.has_value()) {
              *error = std::move(result).error();
            }
          },
          &callback_count, &callback_error));

  devtools_client_->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                               "Cancelled by browser test."));
  devtools_client_->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                               "Second cancellation."));

  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(callback_error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, callback_error->code);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       CancelCallbackMayReenterDetachExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(devtools_client_->AttachTo(
      browser()->tab_strip_model()->GetActiveWebContents()));

  int callback_count = 0;
  base::DictValue params;
  params.Set("expression", "new Promise(() => {})");
  params.Set("awaitPromise", true);
  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](DaoDevToolsClient *client, int *count,
             DaoDevToolsClient::CommandResult result) {
            ++*count;
            EXPECT_FALSE(result.has_value());
            client->Detach();
          },
          devtools_client_.get(), &callback_count));

  devtools_client_->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                               "Cancelled by browser test."));

  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       CancelCallbackReentrantSendIsRejected) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(devtools_client_->AttachTo(
      browser()->tab_strip_model()->GetActiveWebContents()));

  int outer_count = 0;
  int reentrant_count = 0;
  int reentrant_command_id = -1;
  devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", "new Promise(() => {})")
          .Set("awaitPromise", true),
      base::BindOnce(
          [](DaoDevToolsClient *client, int *outer_count, int *reentrant_count,
             int *reentrant_command_id,
             DaoDevToolsClient::CommandResult result) {
            ++*outer_count;
            EXPECT_FALSE(result.has_value());
            *reentrant_command_id = client->SendCommand(
                "Runtime.evaluate", base::DictValue().Set("expression", "1"),
                base::BindOnce(
                    [](int *count,
                       DaoDevToolsClient::CommandResult nested_result) {
                      ++*count;
                      EXPECT_FALSE(nested_result.has_value());
                      EXPECT_EQ(DaoToolErrorCode::kToolCancelled,
                                nested_result.error().code);
                    },
                    reentrant_count));
          },
          devtools_client_.get(), &outer_count, &reentrant_count,
          &reentrant_command_id));

  devtools_client_->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                               "Cancelled by browser test."));

  EXPECT_EQ(1, outer_count);
  EXPECT_EQ(1, reentrant_count);
  EXPECT_EQ(0, reentrant_command_id);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       CancelDrainMayDestroyClientWithMultiplePending) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto client = std::make_unique<DaoDevToolsClient>();
  ASSERT_TRUE(
      client->AttachTo(browser()->tab_strip_model()->GetActiveWebContents()));

  int first_count = 0;
  int second_count = 0;
  DaoDevToolsClient *raw_client = client.get();
  raw_client->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", "new Promise(() => {})")
          .Set("awaitPromise", true),
      base::BindOnce(
          [](std::unique_ptr<DaoDevToolsClient> *owner, int *count,
             DaoDevToolsClient::CommandResult result) {
            ++*count;
            EXPECT_FALSE(result.has_value());
            owner->reset();
          },
          &client, &first_count));
  raw_client->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", "new Promise(() => {})")
          .Set("awaitPromise", true),
      base::BindOnce(
          [](int *count, DaoDevToolsClient::CommandResult result) {
            ++*count;
            EXPECT_FALSE(result.has_value());
          },
          &second_count));

  raw_client->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                         "Cancelled by browser test."));

  EXPECT_FALSE(client);
  EXPECT_EQ(1, first_count);
  EXPECT_EQ(1, second_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       DetachDrainMayDestroyClientWithMultiplePending) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto client = std::make_unique<DaoDevToolsClient>();
  ASSERT_TRUE(
      client->AttachTo(browser()->tab_strip_model()->GetActiveWebContents()));

  int first_count = 0;
  int second_count = 0;
  DaoDevToolsClient *raw_client = client.get();
  for (const auto &[count, destroy] : std::array<std::pair<int *, bool>, 2>{
           std::pair{&first_count, true}, std::pair{&second_count, false}}) {
    raw_client->SendCommand(
        "Runtime.evaluate",
        base::DictValue()
            .Set("expression", "new Promise(() => {})")
            .Set("awaitPromise", true),
        base::BindOnce(
            [](std::unique_ptr<DaoDevToolsClient> *owner, int *count,
               bool destroy, DaoDevToolsClient::CommandResult result) {
              ++*count;
              EXPECT_FALSE(result.has_value());
              if (destroy) {
                owner->reset();
              }
            },
            &client, count, destroy));
  }

  raw_client->Detach();

  EXPECT_FALSE(client);
  EXPECT_EQ(1, first_count);
  EXPECT_EQ(1, second_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AttachDrainMayDestroyClientWithMultiplePending) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *first_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome::AddTabAt(browser(), second_url(), 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));
  content::WebContents *second_target =
      browser()->tab_strip_model()->GetActiveWebContents();

  auto client = std::make_unique<DaoDevToolsClient>();
  ASSERT_TRUE(client->AttachTo(first_target));
  int first_count = 0;
  int second_count = 0;
  DaoDevToolsClient *raw_client = client.get();
  for (const auto &[count, destroy] : std::array<std::pair<int *, bool>, 2>{
           std::pair{&first_count, true}, std::pair{&second_count, false}}) {
    raw_client->SendCommand(
        "Runtime.evaluate",
        base::DictValue()
            .Set("expression", "new Promise(() => {})")
            .Set("awaitPromise", true),
        base::BindOnce(
            [](std::unique_ptr<DaoDevToolsClient> *owner, int *count,
               bool destroy, DaoDevToolsClient::CommandResult result) {
              ++*count;
              EXPECT_FALSE(result.has_value());
              if (destroy) {
                owner->reset();
              }
            },
            &client, count, destroy));
  }

  EXPECT_FALSE(raw_client->AttachTo(second_target));

  EXPECT_FALSE(client);
  EXPECT_EQ(1, first_count);
  EXPECT_EQ(1, second_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       PageAttachDrainMayDestroyExecutor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *first_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome::AddTabAt(browser(), second_url(), 1, true);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetActiveWebContents()));
  content::WebContents *second_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(devtools_client_->AttachTo(first_target));

  auto session =
      std::make_unique<DaoBrowserAutomationSession>(browser(), second_target);
  auto local_executor = std::make_unique<DaoBrowserToolExecutor>(
      devtools_client_.get(), &ui_delegate_);
  int drain_count = 0;
  devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", "new Promise(() => {})")
          .Set("awaitPromise", true),
      base::BindOnce(
          [](std::unique_ptr<DaoBrowserToolExecutor> *owner, int *count,
             DaoDevToolsClient::CommandResult result) {
            ++*count;
            EXPECT_FALSE(result.has_value());
            owner->reset();
          },
          &local_executor, &drain_count));

  int tool_callback_count = 0;
  std::optional<DaoToolError> tool_error;
  DaoBrowserToolCall call;
  call.request_id = "page-attach-destroys-executor";
  call.name = "get_page_info";
  DaoBrowserToolExecutor *raw_executor = local_executor.get();
  raw_executor->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                        base::BindOnce(
                            [](int *count, std::optional<DaoToolError> *error,
                               DaoBrowserToolResult result) {
                              ++*count;
                              if (result.error.has_value()) {
                                *error = std::move(result.error);
                              }
                            },
                            &tool_callback_count, &tool_error));

  EXPECT_FALSE(local_executor);
  EXPECT_EQ(1, drain_count);
  EXPECT_EQ(1, tool_callback_count);
  ASSERT_TRUE(tool_error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, tool_error->code);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       PageResolverMayDestroyOwnerExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto session = MakeSessionForActiveTab();
  auto page_tools =
      std::make_unique<DaoPageTools>(devtools_client_.get(), &ui_delegate_);
  DaoPageTools *raw_page_tools = page_tools.get();
  int resolver_count = 0;
  int callback_count = 0;
  std::optional<DaoToolError> callback_error;
  auto resolver = base::BindRepeating(
      [](std::unique_ptr<DaoPageTools> *owner, int *count,
         content::WebContents *target)
          -> base::expected<content::WebContents *, DaoToolError> {
        ++*count;
        owner->reset();
        return target;
      },
      &page_tools, &resolver_count, target);

  raw_page_tools->Execute("page-resolver-destroys-owner", "get_page_info",
                          target, session->committed_origin(),
                          session->document_sequence_number(), resolver,
                          base::DictValue(),
                          base::BindOnce(
                              [](int *count, std::optional<DaoToolError> *error,
                                 DaoBrowserToolResult result) {
                                ++*count;
                                if (result.error.has_value()) {
                                  *error = std::move(result.error);
                                }
                              },
                              &callback_count, &callback_error));

  EXPECT_FALSE(page_tools);
  EXPECT_EQ(1, resolver_count);
  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(callback_error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, callback_error->code);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ValidationResolverMayDestroyOwnerExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto session = MakeSessionForActiveTab();
  auto page_tools =
      std::make_unique<DaoPageTools>(devtools_client_.get(), &ui_delegate_);
  DaoPageTools *raw_page_tools = page_tools.get();
  int resolver_count = 0;
  int callback_count = 0;
  auto resolver = base::BindRepeating(
      [](std::unique_ptr<DaoPageTools> *owner, int *count,
         content::WebContents *target)
          -> base::expected<content::WebContents *, DaoToolError> {
        ++*count;
        if (*count == 2) {
          owner->reset();
        }
        return target;
      },
      &page_tools, &resolver_count, target);

  raw_page_tools->Execute(
      "validation-resolver-destroys-owner", "get_page_info", target,
      session->committed_origin(), session->document_sequence_number(),
      resolver, base::DictValue(),
      base::BindOnce(
          [](int *count, DaoBrowserToolResult result) {
            ++*count;
            EXPECT_FALSE(result.ok);
            ASSERT_TRUE(result.error.has_value());
            EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
          },
          &callback_count));

  EXPECT_FALSE(page_tools);
  EXPECT_EQ(2, resolver_count);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       SynchronousSendCommandMayDestroyOwnerExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto session = MakeSessionForActiveTab();
  auto page_tools =
      std::make_unique<DaoPageTools>(devtools_client_.get(), &ui_delegate_);
  DaoPageTools *raw_page_tools = page_tools.get();
  int command_count = 0;
  int callback_count = 0;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](std::unique_ptr<DaoPageTools> *owner, int *count,
         const std::string &method) {
        if (method == "Runtime.evaluate") {
          ++*count;
          owner->reset();
        }
      },
      &page_tools, &command_count));
  auto resolver = base::BindRepeating(
      [](content::WebContents *target)
          -> base::expected<content::WebContents *, DaoToolError> {
        return target;
      },
      target);

  raw_page_tools->Execute(
      "send-command-destroys-owner", "get_page_info", target,
      session->committed_origin(), session->document_sequence_number(),
      resolver, base::DictValue(),
      base::BindOnce(
          [](int *count, DaoBrowserToolResult result) {
            ++*count;
            EXPECT_FALSE(result.ok);
            ASSERT_TRUE(result.error.has_value());
            EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
          },
          &callback_count));
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());

  EXPECT_FALSE(page_tools);
  EXPECT_EQ(1, command_count);
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       PageToolsCancelReentrantExecuteIsRejected) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(
      target,
      "const input=document.body.appendChild(document.createElement('input'));"
      "input.focus(); input.select=()=>new Promise(()=>{});"));
  auto session = MakeSessionForActiveTab();
  auto page_tools =
      std::make_unique<DaoPageTools>(devtools_client_.get(), &ui_delegate_);

  int outer_count = 0;
  int reentrant_count = 0;
  auto resolver = base::BindRepeating(
      [](base::WeakPtr<DaoBrowserAutomationSession> session)
          -> base::expected<content::WebContents *, DaoToolError> {
        if (!session) {
          return base::unexpected(MakeDaoToolError(
              DaoToolErrorCode::kTargetGone, "Session was destroyed."));
        }
        return session->ResolveEligibleTarget();
      },
      session->GetWeakPtr());
  page_tools->Execute(
      "page-tools-cancel-outer", "type_text", target,
      session->committed_origin(), session->document_sequence_number(),
      resolver, base::DictValue().Set("text", "outer").Set("clear", true),
      base::BindOnce(
          [](DaoPageTools *page_tools, content::WebContents *target,
             DaoBrowserAutomationSession *session,
             DaoPageTools::TargetResolver resolver, int *outer_count,
             int *reentrant_count, DaoBrowserToolResult result) {
            ++*outer_count;
            ASSERT_TRUE(result.error.has_value());
            page_tools->Execute(
                "page-tools-cancel-reentrant", "get_page_info", target,
                session->committed_origin(),
                session->document_sequence_number(), std::move(resolver),
                base::DictValue(),
                base::BindOnce(
                    [](int *count, DaoBrowserToolResult nested_result) {
                      ++*count;
                      ASSERT_TRUE(nested_result.error.has_value());
                      EXPECT_EQ(DaoToolErrorCode::kToolCancelled,
                                nested_result.error->code);
                    },
                    reentrant_count));
          },
          page_tools.get(), target, session.get(), resolver, &outer_count,
          &reentrant_count));

  page_tools->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                         "Cancelled by browser test."));

  EXPECT_EQ(1, outer_count);
  EXPECT_EQ(1, reentrant_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       PageToolsCancelDrainMayDestroyOwner) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(
      target,
      "const input=document.body.appendChild(document.createElement('input'));"
      "input.focus(); input.select=()=>new Promise(()=>{});"));
  auto session = MakeSessionForActiveTab();
  auto page_tools =
      std::make_unique<DaoPageTools>(devtools_client_.get(), &ui_delegate_);
  DaoPageTools *raw_page_tools = page_tools.get();
  auto resolver = base::BindRepeating(
      [](base::WeakPtr<DaoBrowserAutomationSession> session)
          -> base::expected<content::WebContents *, DaoToolError> {
        if (!session) {
          return base::unexpected(MakeDaoToolError(
              DaoToolErrorCode::kTargetGone, "Session was destroyed."));
        }
        return session->ResolveEligibleTarget();
      },
      session->GetWeakPtr());

  int first_count = 0;
  int second_count = 0;
  for (const auto &[request_id, count, destroy] :
       std::array<std::tuple<const char *, int *, bool>, 2>{
           std::tuple{"page-tools-destroy-a", &first_count, true},
           std::tuple{"page-tools-destroy-b", &second_count, false}}) {
    raw_page_tools->Execute(
        request_id, "type_text", target, session->committed_origin(),
        session->document_sequence_number(), resolver,
        base::DictValue().Set("text", request_id).Set("clear", true),
        base::BindOnce(
            [](std::unique_ptr<DaoPageTools> *owner, int *count, bool destroy,
               DaoBrowserToolResult result) {
              ++*count;
              ASSERT_TRUE(result.error.has_value());
              if (destroy) {
                owner->reset();
              }
            },
            &page_tools, count, destroy));
  }

  raw_page_tools->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                             "Cancelled by browser test."));

  EXPECT_FALSE(page_tools);
  EXPECT_EQ(1, first_count);
  EXPECT_EQ(1, second_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ExecutorCancelReentrantExecuteIsRejected) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(content::ExecJs(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "const input=document.body.appendChild(document.createElement('input'));"
      "input.focus(); input.select=()=>new Promise(()=>{});"));
  auto session = MakeSessionForActiveTab();
  auto local_executor = std::make_unique<DaoBrowserToolExecutor>(
      devtools_client_.get(), &ui_delegate_);

  int outer_count = 0;
  int reentrant_count = 0;
  DaoBrowserToolCall call;
  call.request_id = "executor-cancel-outer";
  call.name = "type_text";
  call.arguments = base::DictValue().Set("text", "outer").Set("clear", true);
  local_executor->Execute(
      session.get(), DaoToolClient::kMcp, std::move(call),
      base::BindOnce(
          [](DaoBrowserToolExecutor *executor,
             DaoBrowserAutomationSession *session, int *outer_count,
             int *reentrant_count, DaoBrowserToolResult result) {
            ++*outer_count;
            EXPECT_TRUE(result.error.has_value());
            DaoBrowserToolCall nested;
            nested.request_id = "executor-cancel-reentrant";
            nested.name = "get_page_info";
            executor->Execute(
                session, DaoToolClient::kMcp, std::move(nested),
                base::BindOnce(
                    [](int *count, DaoBrowserToolResult nested_result) {
                      ++*count;
                      ASSERT_TRUE(nested_result.error.has_value());
                      EXPECT_EQ(DaoToolErrorCode::kToolCancelled,
                                nested_result.error->code);
                    },
                    reentrant_count));
          },
          local_executor.get(), session.get(), &outer_count, &reentrant_count));

  local_executor->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                             "Cancelled by browser test."));

  EXPECT_EQ(1, outer_count);
  EXPECT_EQ(1, reentrant_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ExecutorCancelDrainMayDestroyOwnerWithMultiplePending) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(content::ExecJs(
      browser()->tab_strip_model()->GetActiveWebContents(),
      "const input=document.body.appendChild(document.createElement('input'));"
      "input.focus(); input.select=()=>new Promise(()=>{});"));
  auto session = MakeSessionForActiveTab();
  auto local_executor = std::make_unique<DaoBrowserToolExecutor>(
      devtools_client_.get(), &ui_delegate_);
  DaoBrowserToolExecutor *raw_executor = local_executor.get();

  int first_count = 0;
  int second_count = 0;
  for (const auto &[request_id, count, destroy] :
       std::array<std::tuple<const char *, int *, bool>, 2>{
           std::tuple{"executor-destroy-a", &first_count, true},
           std::tuple{"executor-destroy-b", &second_count, false}}) {
    DaoBrowserToolCall call;
    call.request_id = request_id;
    call.name = "type_text";
    call.arguments =
        base::DictValue().Set("text", request_id).Set("clear", true);
    raw_executor->Execute(
        session.get(), DaoToolClient::kMcp, std::move(call),
        base::BindOnce(
            [](std::unique_ptr<DaoBrowserToolExecutor> *owner, int *count,
               bool destroy, DaoBrowserToolResult result) {
              ++*count;
              EXPECT_TRUE(result.error.has_value());
              if (destroy) {
                owner->reset();
              }
            },
            &local_executor, count, destroy));
  }

  raw_executor->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                           "Cancelled by browser test."));

  EXPECT_FALSE(local_executor);
  EXPECT_EQ(1, first_count);
  EXPECT_EQ(1, second_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       DevToolsProtocolErrorIsTypedFailure) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(devtools_client_->AttachTo(
      browser()->tab_strip_model()->GetActiveWebContents()));

  base::test::TestFuture<DaoDevToolsClient::CommandResult> future;
  devtools_client_->SendCommand("Dao.invalidMethod", base::DictValue(),
                                future.GetCallback());
  DaoDevToolsClient::CommandResult result = future.Take();

  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, result.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AnimatedClickRevalidatesDomainAfterCursorMove) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();

  base::RunLoop cursor_ready;
  ui_delegate_.HoldNextCursorMove(cursor_ready.QuitClosure());
  DaoBrowserToolCall call;
  call.request_id = "animated-click-domain-revalidation";
  call.name = "agent_click";
  call.arguments = base::DictValue().Set("selector", "body");
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  cursor_ready.Run();

  const GURL cross_domain_url =
      embedded_test_server()->GetURL("other.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), cross_domain_url));

  DaoBrowserToolResult result = future.Take();
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, result.error->code);
  EXPECT_EQ(1, ui_delegate_.cancel_cursor_count());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       NewOperationSnapshotsCompletedReplacementDocument) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url()));

  DaoBrowserToolResult script_result = Execute(
      session.get(), "execute_script",
      base::DictValue().Set(
          "code", "document.body.dataset.daoOperationSnapshot='replacement';"
                  "'replacement'"));

  ASSERT_TRUE(script_result.ok);
  EXPECT_EQ(
      "replacement",
      content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "document.body.dataset.daoOperationSnapshot")
          .ExtractString());

  DaoBrowserToolResult highlight_result =
      Execute(session.get(), "highlight_element",
              base::DictValue().Set("selector", "body"));
  EXPECT_TRUE(highlight_result.ok);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       NewOperationSnapshotsCompletedCrossOriginDocument) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  const GURL replacement =
      embedded_test_server()->GetURL("other.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), replacement));

  DaoBrowserToolResult result = Execute(
      session.get(), "execute_script",
      base::DictValue().Set(
          "code", "document.body.dataset.daoOperationSnapshot='cross-origin';"
                  "'cross-origin'"));

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(
      "cross-origin",
      content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "document.body.dataset.daoOperationSnapshot")
          .ExtractString());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AnimatedClickRejectsReplacementDocument) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();

  base::RunLoop cursor_ready;
  ui_delegate_.HoldNextCursorMove(cursor_ready.QuitClosure());
  DaoBrowserToolCall call;
  call.request_id = "animated-click-document-revalidation";
  call.name = "agent_click";
  call.arguments = base::DictValue().Set("selector", "body");
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  cursor_ready.Run();

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url()));

  DaoBrowserToolResult result = future.Take();
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, result.error->code);
  EXPECT_EQ(1, ui_delegate_.cancel_cursor_count());
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpPageToolsBrowserTest,
    AnimatedClickRevalidatesAuthorizedWindowAfterCursorMove) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  TabListInterface *source_tabs = TabListInterface::From(browser());
  ASSERT_NE(nullptr, source_tabs);
  tabs::TabInterface *target_tab = source_tabs->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  const tabs::TabHandle target_handle = target_tab->GetHandle();
  chrome::AddTabAt(browser(), GURL("about:blank"), 1, false);

  base::RunLoop cursor_ready;
  ui_delegate_.HoldNextCursorMove(cursor_ready.QuitClosure());
  DaoBrowserToolCall call;
  call.request_id = "animated-click-window-revalidation";
  call.name = "agent_click";
  call.arguments = base::DictValue().Set("selector", "body");
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  cursor_ready.Run();

  Browser *destination = CreateBrowser(browser()->profile());
  source_tabs->MoveTabToWindow(target_handle, destination->GetSessionID(), 0);
  ui_delegate_.CompletePendingCursorMove(true);

  DaoBrowserToolResult result = future.Take();
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetGone, result.error->code);
  EXPECT_EQ(1, ui_delegate_.cancel_cursor_count());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       TypeTextRevalidatesAuthorizedWindowBeforeInsert) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      document.body.innerHTML = '<input id="target" value="original">';
      const input = document.querySelector('#target');
      input.focus();
      input.select = () => new Promise(resolve => {
        window.__dao_select_started__ = true;
        window.__dao_complete_select__ = () => {
          HTMLInputElement.prototype.select.call(input);
          resolve();
        };
      });
      return true;
    })()
  )")
                  .ExtractBool());

  TabListInterface *source_tabs = TabListInterface::From(browser());
  ASSERT_NE(nullptr, source_tabs);
  tabs::TabInterface *target_tab = source_tabs->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  const tabs::TabHandle target_handle = target_tab->GetHandle();
  chrome::AddTabAt(browser(), GURL("about:blank"), 1, false);

  DaoBrowserToolCall call;
  call.request_id = "type-text-window-revalidation";
  call.name = "type_text";
  call.arguments =
      base::DictValue().Set("text", "replacement").Set("clear", true);
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(target, "!!window.__dao_select_started__")
        .ExtractBool();
  }));

  Browser *destination = CreateBrowser(browser()->profile());
  source_tabs->MoveTabToWindow(target_handle, destination->GetSessionID(), 0);
  ASSERT_TRUE(content::EvalJs(target, "window.__dao_complete_select__(); true")
                  .ExtractBool());

  DaoBrowserToolResult result = future.Take();
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetGone, result.error->code);
  EXPECT_EQ("original",
            content::EvalJs(target, "document.querySelector('#target').value")
                .ExtractString());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       TypeTextRejectsReplacementDocumentBeforeInsert) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      document.body.innerHTML = '<input id="target" value="original">';
      const input = document.querySelector('#target');
      input.focus();
      input.select = () => new Promise(() => {
        window.__dao_select_started__ = true;
      });
      return true;
    })()
  )")
                  .ExtractBool());

  DaoBrowserToolCall call;
  call.request_id = "type-text-document-revalidation";
  call.name = "type_text";
  call.arguments =
      base::DictValue().Set("text", "replacement").Set("clear", true);
  call.timeout = base::Seconds(5);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(target, "!!window.__dao_select_started__")
        .ExtractBool();
  }));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(),
      embedded_test_server()->GetURL("/textinput/focus_input_on_load.html")));

  DaoBrowserToolResult result = future.Take();
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(
      "",
      content::EvalJs(target, "document.activeElement.value").ExtractString());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       TypeTextRuntimeExceptionStopsInsert) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      document.body.innerHTML = '<input id="target" value="original">';
      const input = document.querySelector('#target');
      input.focus();
      input.select = () => {
        throw new Error('selection failed');
      };
      return true;
    })()
  )")
                  .ExtractBool());

  DaoBrowserToolResult result =
      Execute(session.get(), "type_text",
              base::DictValue().Set("text", "replacement").Set("clear", true));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, result.error->code);
  EXPECT_EQ("original",
            content::EvalJs(target, "document.querySelector('#target').value")
                .ExtractString());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       HighlightRuntimeExceptionIsTypedFailure) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      window.__dao_agent__ = {
        cleanupAttempts: 0,
        showHighlight() {
          throw new Error('highlight failed');
        },
        clearHighlight() {
          this.cleanupAttempts++;
          return true;
        },
        hasHighlight() {
          return false;
        },
      };
      return true;
    })()
  )")
                  .ExtractBool());

  DaoBrowserToolResult result =
      Execute(session.get(), "highlight_element",
              base::DictValue().Set("selector", "body"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, result.error->code);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(target, "window.__dao_agent__.cleanupAttempts >= 1")
        .ExtractBool();
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       KeyRuntimeExceptionIsTypedFailure) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      document.body.dispatchEvent = () => {
        throw new Error('keyboard dispatch failed');
      };
      return true;
    })()
  )")
                  .ExtractBool());

  DaoBrowserToolResult result = Execute(session.get(), "press_key_chord",
                                        base::DictValue().Set("keys", "Enter"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, result.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ScriptRuntimeExceptionIsTypedFailureAndUnlocks) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();

  DaoBrowserToolResult result =
      Execute(session.get(), "execute_script",
              base::DictValue()
                  .Set("code", "throw new Error('script failed')")
                  .Set("lock_tab", true));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInternalError, result.error->code);
  EXPECT_FALSE(DaoAgentLockTabHelper::IsLocked(target));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       CursorTimeoutCancelsVisualState) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();

  base::RunLoop cursor_ready;
  ui_delegate_.HoldNextCursorMove(cursor_ready.QuitClosure());
  DaoBrowserToolCall call;
  call.request_id = "cursor-timeout-cleanup";
  call.name = "move_cursor";
  call.arguments = base::DictValue().Set("x", 10).Set("y", 20);
  call.timeout = base::Milliseconds(1);
  base::test::TestFuture<DaoBrowserToolResult> future;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     future.GetCallback());
  cursor_ready.Run();

  DaoBrowserToolResult result = future.Take();
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolTimeout, result.error->code);
  EXPECT_EQ(1, ui_delegate_.cancel_cursor_count());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       SessionTeardownClearsSuccessfulCursor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();

  DaoBrowserToolResult result =
      Execute(session.get(), "move_cursor",
              base::DictValue().Set("x", 10).Set("y", 20));

  ASSERT_TRUE(result.ok);
  EXPECT_EQ(0, ui_delegate_.cancel_cursor_count());
  executor_->ClearSessionState(nullptr);
  EXPECT_EQ(1, ui_delegate_.cancel_cursor_count());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       NullSessionTeardownClearsPersistentHighlight) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();

  DaoBrowserToolResult result =
      Execute(session.get(), "highlight_element",
              base::DictValue().Set("selector", "body"));

  ASSERT_TRUE(result.ok);
  EXPECT_TRUE(content::EvalJs(target, "window.__dao_agent__ && "
                                      "window.__dao_agent__.hasHighlight()")
                  .ExtractBool());
  executor_->ClearSessionState(nullptr);
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return !content::EvalJs(target, "window.__dao_agent__ && "
                                    "window.__dao_agent__.hasHighlight()")
                .ExtractBool();
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       OldCleanupDoesNotClearNewHighlight) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(Execute(session.get(), "highlight_element",
                      base::DictValue().Set("selector", "body"))
                  .ok);

  executor_->ClearSessionState(nullptr);
  ASSERT_TRUE(Execute(session.get(), "highlight_element",
                      base::DictValue().Set("selector", "body"))
                  .ok);
  base::RunLoop settle;
  base::OneShotTimer timer;
  timer.Start(FROM_HERE, base::Milliseconds(250), settle.QuitClosure());
  settle.Run();

  EXPECT_TRUE(content::EvalJs(target, "window.__dao_agent__ && "
                                      "window.__dao_agent__.hasHighlight()")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       HighlightCleanupRetriesRuntimeException) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(Execute(session.get(), "highlight_element",
                      base::DictValue().Set("selector", "body"))
                  .ok);
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      const original = window.__dao_agent__.clearHighlight.bind(
          window.__dao_agent__);
      window.__dao_cleanup_attempts__ = 0;
      window.__dao_cleanup_first_attempt__ = 0;
      window.__dao_agent__.clearHighlight = generation => {
        window.__dao_cleanup_attempts__++;
        if (window.__dao_cleanup_attempts__ === 1) {
          window.__dao_cleanup_first_attempt__ = performance.now();
          throw new Error('fail the first cleanup attempt');
        }
        if (performance.now() - window.__dao_cleanup_first_attempt__ < 25) {
          throw new Error('retry happened before the cleanup queue delay');
        }
        return original(generation);
      };
      return true;
    })()
  )")
                  .ExtractBool());

  executor_->ClearSessionState(nullptr);

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(target, "window.__dao_cleanup_attempts__ >= 2 && "
                                   "!window.__dao_agent__.hasHighlight()")
        .ExtractBool();
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AnimatedClickQueuesRuntimeExceptionHighlightCleanup) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      window.__dao_agent__ = {
        visible: false,
        generation: '',
        clearAttempts: 0,
        showHighlight(selector, generation) {
          if (!document.querySelector(selector)) {
            return false;
          }
          this.visible = true;
          this.generation = generation;
          return true;
        },
        clearHighlight(generation) {
          this.clearAttempts++;
          if (this.clearAttempts === 1) {
            throw new Error('fail the inline cleanup attempt');
          }
          if (!generation || generation === this.generation) {
            this.visible = false;
            this.generation = '';
          }
          return true;
        },
        hasHighlight() {
          return this.visible;
        },
      };
      return true;
    })()
  )")
                  .ExtractBool());

  DaoBrowserToolCall call;
  call.request_id = "animated-click-runtime-cleanup";
  call.name = "agent_click";
  call.arguments = base::DictValue().Set("selector", "body");
  call.timeout = base::Seconds(5);
  int callback_count = 0;
  std::optional<DaoBrowserToolResult> callback_result;
  base::RunLoop result_loop;
  executor_->Execute(
      session.get(), DaoToolClient::kMcp, std::move(call),
      base::BindOnce(
          [](int *count, std::optional<DaoBrowserToolResult> *result,
             base::OnceClosure quit, DaoBrowserToolResult value) {
            ++*count;
            *result = std::move(value);
            std::move(quit).Run();
          },
          &callback_count, &callback_result, result_loop.QuitClosure()));
  result_loop.Run();

  ASSERT_TRUE(callback_result.has_value());
  ASSERT_TRUE(callback_result->ok);
  ASSERT_TRUE(callback_result->data.is_dict());
  EXPECT_TRUE(
      callback_result->data.GetDict().FindBool("success").value_or(false));

  base::RunLoop cleanup_loop;
  base::OneShotTimer timer;
  timer.Start(FROM_HERE, base::Milliseconds(250), cleanup_loop.QuitClosure());
  cleanup_loop.Run();

  EXPECT_EQ(1, callback_count);
  EXPECT_GE(content::EvalJs(target, "window.__dao_agent__.clearAttempts")
                .ExtractInt(),
            2);
  EXPECT_FALSE(content::EvalJs(target, "window.__dao_agent__.hasHighlight()")
                   .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ExecutorDestructionFinishesHighlightCleanup) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(Execute(session.get(), "highlight_element",
                      base::DictValue().Set("selector", "body"))
                  .ok);
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      const original = window.__dao_agent__.clearHighlight.bind(
          window.__dao_agent__);
      window.__dao_cleanup_attempts__ = 0;
      window.__dao_cleanup_first_attempt__ = 0;
      window.__dao_agent__.clearHighlight = generation => {
        window.__dao_cleanup_attempts__++;
        if (window.__dao_cleanup_attempts__ === 1) {
          window.__dao_cleanup_first_attempt__ = performance.now();
          throw new Error('fail the first cleanup attempt');
        }
        if (performance.now() - window.__dao_cleanup_first_attempt__ < 25) {
          throw new Error('retry happened before the cleanup queue delay');
        }
        return original(generation);
      };
      return true;
    })()
  )")
                  .ExtractBool());

  executor_.reset();

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(target, "window.__dao_cleanup_attempts__ >= 2 && "
                                   "!window.__dao_agent__.hasHighlight()")
        .ExtractBool();
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       HighlightGenerationIsUniqueAcrossExecutors) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();
  ASSERT_TRUE(content::EvalJs(target, R"(
    (() => {
      window.__dao_agent__ = {
        visible: false,
        generations: [],
        showHighlight(selector, generation) {
          this.visible = true;
          this.generations.push(generation);
          return true;
        },
        clearHighlight(generation) {
          if (!generation ||
              generation === this.generations[this.generations.length - 1]) {
            this.visible = false;
          }
          return true;
        },
        hasHighlight() {
          return this.visible;
        },
      };
      return true;
    })()
  )")
                  .ExtractBool());
  ASSERT_TRUE(Execute(session.get(), "highlight_element",
                      base::DictValue().Set("selector", "body"))
                  .ok);

  executor_.reset();
  executor_ = std::make_unique<DaoBrowserToolExecutor>(devtools_client_.get(),
                                                       &ui_delegate_);
  ASSERT_TRUE(Execute(session.get(), "highlight_element",
                      base::DictValue().Set("selector", "body"))
                  .ok);

  EXPECT_TRUE(content::EvalJs(target,
                              "window.__dao_agent__.generations.length >= 2 && "
                              "window.__dao_agent__.generations[0] !== "
                              "window.__dao_agent__.generations[1]")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       DeadlineUnlocksAndDiscardsLateCdpCompletion) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();

  DaoBrowserToolCall call;
  call.request_id = "deadline-lock-cleanup";
  call.name = "execute_script";
  call.arguments =
      base::DictValue()
          .Set("code", "(() => { const end = Date.now() + 250; "
                       "while (Date.now() < end) {} return 'late'; })()")
          .Set("lock_tab", true);
  call.timeout = base::Milliseconds(1);
  int callback_count = 0;
  std::optional<DaoBrowserToolResult> callback_result;
  base::RunLoop result_loop;
  executor_->Execute(
      session.get(), DaoToolClient::kMcp, std::move(call),
      base::BindOnce(
          [](int *count, std::optional<DaoBrowserToolResult> *result,
             base::OnceClosure quit, DaoBrowserToolResult value) {
            ++*count;
            *result = std::move(value);
            std::move(quit).Run();
          },
          &callback_count, &callback_result, result_loop.QuitClosure()));
  EXPECT_TRUE(DaoAgentLockTabHelper::IsLocked(target));
  result_loop.Run();

  ASSERT_TRUE(callback_result.has_value());
  ASSERT_FALSE(callback_result->ok);
  ASSERT_TRUE(callback_result->error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolTimeout, callback_result->error->code);
  EXPECT_FALSE(DaoAgentLockTabHelper::IsLocked(target));

  base::RunLoop late_response_loop;
  base::OneShotTimer timer;
  timer.Start(FROM_HERE, base::Milliseconds(400),
              late_response_loop.QuitClosure());
  late_response_loop.Run();
  EXPECT_EQ(1, callback_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ExecutorDestructionCancelsAndUnlocksExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSessionForActiveTab();
  content::WebContents *target = session->ResolveTarget().value();

  DaoBrowserToolCall call;
  call.request_id = "executor-destruction-cleanup";
  call.name = "execute_script";
  call.arguments =
      base::DictValue()
          .Set("code", "(() => { const end = Date.now() + 250; "
                       "while (Date.now() < end) {} return 'late'; })()")
          .Set("lock_tab", true);
  call.timeout = base::Seconds(5);
  int callback_count = 0;
  std::optional<DaoToolErrorCode> error_code;
  executor_->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                     base::BindOnce(
                         [](int *count, std::optional<DaoToolErrorCode> *code,
                            DaoBrowserToolResult result) {
                           ++*count;
                           if (result.error) {
                             *code = result.error->code;
                           }
                         },
                         &callback_count, &error_code));
  EXPECT_TRUE(DaoAgentLockTabHelper::IsLocked(target));

  executor_.reset();

  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(error_code.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, *error_code);
  EXPECT_FALSE(DaoAgentLockTabHelper::IsLocked(target));
}

using DaoMcpPeerLeaseTest = DaoMcpPageToolsBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoMcpPeerLeaseTest,
                       ExternalLeaseBlocksDaoAgentUntilReleased) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  DaoAgentLeaseManager *leases =
      DaoAgentLeaseManager::GetForProfile(browser()->profile());
  ASSERT_NE(nullptr, leases);
  auto external =
      leases->TryAcquire({DaoToolClient::kMcp, "external-test", "Codex"});
  ASSERT_TRUE(external.has_value());

  const std::string blocked_turn_id =
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId");
  ASSERT_FALSE(blocked_turn_id.empty());
  EXPECT_EQ("AGENT_CONTROL_BUSY",
            CallAgentNativeField(agent_contents, "getPageInfo", "code"));
  EXPECT_EQ("true",
            CallAgentNativeField(agent_contents, "getPageInfo", "retryable"));
  EXPECT_EQ(l10n_util::GetStringUTF8(IDS_DAO_MCP_CONTROL_BUSY),
            CallAgentNativeField(agent_contents, "getPageInfo", "error"));
  EXPECT_EQ("AGENT_CONTROL_BUSY",
            CallAgentNativeField(
                agent_contents, "executeScript", "code",
                R"({"code":"document.title","lockTab":false})"));
  EXPECT_EQ("true",
            CallAgentNativeField(
                agent_contents, "executeScript", "retryable",
                R"({"code":"document.title","lockTab":false})"));
  EXPECT_EQ("AGENT_CONTROL_BUSY",
            CallAgentNativeField(agent_contents, "captureScreenshot", "code"));
  EXPECT_EQ(
      "true",
      CallAgentNativeField(agent_contents, "captureScreenshot", "retryable"));
  EXPECT_EQ("true",
            CallAgentNativeField(
                agent_contents, "endAgentTurn", "success",
                R"({"turnId":")" + blocked_turn_id + R"("})"));

  external->Reset();
  const std::string turn_id =
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId");
  EXPECT_FALSE(turn_id.empty());
  auto blocked =
      leases->TryAcquire({DaoToolClient::kMcp, "second-external", "Claude"});
  ASSERT_FALSE(blocked.has_value());
  EXPECT_EQ(DaoToolErrorCode::kLeaseBusy, blocked.error().code);
  EXPECT_EQ("true",
            CallAgentNativeField(agent_contents, "endAgentTurn", "success",
                                 R"({"turnId":")" + turn_id + R"("})"));
  EXPECT_TRUE(
      leases->TryAcquire({DaoToolClient::kMcp, "second-external", "Claude"})
          .has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       LegacyAgentUiPageCallsUseScopedOneShotLease) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  DaoAgentLeaseManager *leases =
      DaoAgentLeaseManager::GetForProfile(browser()->profile());
  ASSERT_NE(nullptr, leases);

  ASSERT_TRUE(content::ExecJs(agent_contents, R"JS(
    (async () => {
      const {executeTool} = await import('./agent_bridge.js');
      const legacy = {context: 'legacy_ui_one_shot'};
      window.__legacyOneShot = {};
      window.__legacyOneShot.unmarked =
          await executeTool('get_page_info', {});
      window.__legacyOneShot.info =
          await executeTool('get_page_info', {}, legacy);
      window.__legacyOneShot.script = await executeTool(
          'execute_script', {code: 'document.title', lock_tab: false}, legacy);
      window.__legacyOneShot.screenshot = await executeTool(
          'capture_screenshot',
          {clip: {x: 0, y: 0, width: 40, height: 30, scale: 1}}, legacy);
      return true;
    })()
  )JS"));

  EXPECT_EQ(
      "LEASE_BUSY",
      content::EvalJs(agent_contents,
                      "String(window.__legacyOneShot.unmarked.code || '')")
          .ExtractString());
  EXPECT_EQ("INVALID_ARGUMENT",
            CallAgentNativeField(agent_contents, "getPageInfo", "code",
                                 R"({"__daoAgentExecutionContext":42})"));
  EXPECT_EQ("INVALID_ARGUMENT",
            CallAgentNativeField(
                agent_contents, "getPageInfo", "code",
                R"({"__daoAgentExecutionContext":"legacy-ui-one-shot"})"));
  EXPECT_EQ(first_url().spec(),
            content::EvalJs(agent_contents,
                            "String(window.__legacyOneShot.info.url || '')")
                .ExtractString());
  EXPECT_EQ(
      "Title Of Awesomeness",
      content::EvalJs(agent_contents,
                      "String(window.__legacyOneShot.script.result || '')")
          .ExtractString());
  EXPECT_TRUE(content::EvalJs(agent_contents,
                              "!!window.__legacyOneShot.screenshot.media?.data")
                  .ExtractBool());

  auto after_success =
      leases->TryAcquire({DaoToolClient::kMcp, "legacy-after", "Codex"});
  ASSERT_TRUE(after_success.has_value());
  EXPECT_TRUE(content::EvalJs(agent_contents, R"JS(
    (async () => {
      const {executeTool} = await import('./agent_bridge.js');
      window.__legacyOneShot.blocked = await executeTool(
          'get_page_info', {}, {context: 'legacy_ui_one_shot'});
      return true;
    })()
  )JS")
                  .ExtractBool());
  EXPECT_EQ("AGENT_CONTROL_BUSY",
            content::EvalJs(agent_contents,
                            "String(window.__legacyOneShot.blocked.code || '')")
                .ExtractString());
  after_success->Reset();
  EXPECT_TRUE(
      leases->TryAcquire({DaoToolClient::kMcp, "legacy-final", "Claude"})
          .has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AgentScreenshotCallbackPreservesLegacyDataWithMedia) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  const std::string turn_id =
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId");
  ASSERT_FALSE(turn_id.empty());

  EXPECT_TRUE(content::EvalJs(agent_contents, R"JS(
    (async () => {
      const {callNative} = await import('./agent_bridge.js');
      window.__legacyScreenshot = await callNative(
          'captureScreenshot',
          {clip: {x: 0, y: 0, width: 40, height: 30, scale: 1}});
      return true;
    })()
  )JS")
                  .ExtractBool());
  EXPECT_FALSE(
      content::EvalJs(agent_contents, "!window.__legacyScreenshot.data")
          .ExtractBool());
  EXPECT_EQ("jpeg",
            content::EvalJs(agent_contents,
                            "String(window.__legacyScreenshot.format || '')")
                .ExtractString());
  EXPECT_EQ(
      "image/jpeg",
      content::EvalJs(agent_contents,
                      "String(window.__legacyScreenshot.media?.mimeType || '')")
          .ExtractString());
  EXPECT_TRUE(content::EvalJs(agent_contents,
                              "window.__legacyScreenshot.data === "
                              "window.__legacyScreenshot.media?.data")
                  .ExtractBool());

  EXPECT_EQ("true",
            CallAgentNativeField(agent_contents, "endAgentTurn", "success",
                                 R"({"turnId":")" + turn_id + R"("})"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AgentAbortCancelsNativePageToolEndToEnd) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(target, R"JS(
    (() => {
      const input = document.createElement('input');
      input.id = 'abort-e2e-input';
      input.addEventListener('input', () => ++window.__abortE2EInputEvents);
      document.body.appendChild(input);
      input.focus();
      window.__abortE2EInputEvents = 0;
      window.__abortE2ESelectStarted = false;
      window.__abortE2EReleaseSelect = null;
      input.select = () => {
        window.__abortE2ESelectStarted = true;
        return new Promise(resolve => {
          window.__abortE2EReleaseSelect = resolve;
        });
      };
      return true;
    })()
  )JS"));
  content::WebContents *agent_contents = LoadAgentWebUi();

  ASSERT_TRUE(content::ExecJs(agent_contents, R"JS(
    (() => {
      window.__abortE2E = {
        abortSettled: false,
        errorName: '',
        promiseSettlements: 0,
        nativeResponses: 0,
        typeRequestId: '',
        cancelRequestId: '',
        typeSends: 0,
        cancelSends: 0,
        setupError: '',
      };
      window.__abortE2ERun = (async () => {
        try {
          const [{buildAgentTools}, {callNative}] = await Promise.all([
            import('./pi_tool_adapter.js'),
            import('./agent_bridge.js'),
          ]);
          const begin = await callNative('beginAgentTurn');
          const state = window.__abortE2E;
          state.turnId = begin.turnId || '';
          const originalSend = chrome.send.bind(chrome);
          const previousResponse = window.cr.webUIResponse;
          chrome.send = (method, args) => {
            if (method === 'typeText') {
              ++state.typeSends;
              state.typeRequestId = String(args[0] || '');
            } else if (method === 'cancelBrowserTool') {
              ++state.cancelSends;
              state.cancelRequestId = String(args[0] || '');
            }
            originalSend(method, args);
          };
          window.cr.webUIResponse = (id, success, payload) => {
            if (id === state.typeRequestId) {
              ++state.nativeResponses;
            }
            previousResponse(id, success, payload);
          };
          const typeText =
              buildAgentTools().find(tool => tool.name === 'type_text');
          if (!typeText) throw new Error('type_text tool unavailable');
          const controller = new AbortController();
          window.__abortE2EController = controller;
          try {
            await typeText.execute(
                'abort-e2e-tool-call', {text: 'must-not-appear', clear: true},
                controller.signal);
            ++state.promiseSettlements;
          } catch (error) {
            ++state.promiseSettlements;
            state.errorName = error && error.name || '';
          }
        } catch (error) {
          window.__abortE2E.setupError =
              error && (error.stack || error.message) || String(error);
        } finally {
          window.__abortE2E.abortSettled = true;
        }
      })();
      return true;
    })()
  )JS"));

  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(target, "!!window.__abortE2ESelectStarted")
        .ExtractBool();
  }));
  auto blocked_lease =
      DaoAgentLeaseManager::GetForProfile(browser()->profile())
          ->TryAcquire({DaoToolClient::kMcp, "abort-e2e-blocked", "Codex"});
  EXPECT_FALSE(blocked_lease.has_value());
  EXPECT_FALSE(DaoAgentLockTabHelper::IsLocked(target));

  ASSERT_TRUE(
      content::ExecJs(agent_contents, "window.__abortE2EController.abort();"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(agent_contents, "!!window.__abortE2E.abortSettled")
        .ExtractBool();
  }));

  EXPECT_EQ("", content::EvalJs(agent_contents, "window.__abortE2E.setupError")
                    .ExtractString());
  EXPECT_EQ("AbortError",
            content::EvalJs(agent_contents, "window.__abortE2E.errorName")
                .ExtractString());
  EXPECT_EQ(
      1, content::EvalJs(agent_contents, "window.__abortE2E.promiseSettlements")
             .ExtractInt());
  EXPECT_EQ(1, content::EvalJs(agent_contents, "window.__abortE2E.typeSends")
                   .ExtractInt());
  EXPECT_EQ(1, content::EvalJs(agent_contents, "window.__abortE2E.cancelSends")
                   .ExtractInt());
  EXPECT_EQ(content::EvalJs(agent_contents, "window.__abortE2E.typeRequestId")
                .ExtractString(),
            content::EvalJs(agent_contents, "window.__abortE2E.cancelRequestId")
                .ExtractString());

  // The turn remains active here. A native response for the held type_text
  // request can therefore only come from request-scoped cancelBrowserTool,
  // not from endAgentTurn's executor-wide cancellation.
  base::ElapsedTimer native_cancel_timer;
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(agent_contents,
                           "window.__abortE2E.nativeResponses === 1")
        .ExtractBool();
  }));
  EXPECT_LT(native_cancel_timer.Elapsed(), base::Seconds(2));
  auto still_blocked = DaoAgentLeaseManager::GetForProfile(browser()->profile())
                           ->TryAcquire({DaoToolClient::kMcp,
                                         "abort-e2e-still-blocked", "Codex"});
  EXPECT_FALSE(still_blocked.has_value());

  ASSERT_TRUE(content::ExecJs(
      target, "window.__abortE2EReleaseSelect();"
              "new Promise(resolve => setTimeout(resolve, 100));"));
  EXPECT_EQ("", content::EvalJs(
                    target, "document.getElementById('abort-e2e-input').value")
                    .ExtractString());
  EXPECT_EQ(
      0, content::EvalJs(target, "window.__abortE2EInputEvents").ExtractInt());
  EXPECT_EQ(
      1, content::EvalJs(agent_contents, "window.__abortE2E.promiseSettlements")
             .ExtractInt());
  EXPECT_EQ(1,
            content::EvalJs(agent_contents, "window.__abortE2E.nativeResponses")
                .ExtractInt());
  EXPECT_TRUE(content::EvalJs(agent_contents, R"JS(
    (async () => {
      const {buildAgentTools} = await import('./pi_tool_adapter.js');
      const getPageInfo =
          buildAgentTools().find(tool => tool.name === 'get_page_info');
      if (!getPageInfo) return false;
      const result = await getPageInfo.execute(
          'abort-e2e-follow-up', {}, new AbortController().signal);
      window.__abortE2E.followUpContentNonempty =
          !!result && Array.isArray(result.content) &&
          result.content.length > 0;
      window.__abortE2E.followUpDetails = result.details || {};
      return true;
    })()
  )JS")
                  .ExtractBool());
  EXPECT_TRUE(
      content::EvalJs(agent_contents,
                      "window.__abortE2E.followUpContentNonempty === true")
          .ExtractBool());
  EXPECT_FALSE(content::EvalJs(agent_contents,
                               "!!window.__abortE2E.followUpDetails.error")
                   .ExtractBool());
  EXPECT_EQ(
      first_url().spec(),
      content::EvalJs(agent_contents,
                      "String(window.__abortE2E.followUpDetails.url || '')")
          .ExtractString());
  EXPECT_EQ(
      "Title Of Awesomeness",
      content::EvalJs(agent_contents,
                      "String(window.__abortE2E.followUpDetails.title || '')")
          .ExtractString());
  auto blocked_after_follow_up =
      DaoAgentLeaseManager::GetForProfile(browser()->profile())
          ->TryAcquire(
              {DaoToolClient::kMcp, "abort-e2e-follow-up-blocked", "Codex"});
  EXPECT_FALSE(blocked_after_follow_up.has_value());

  EXPECT_TRUE(content::EvalJs(agent_contents, R"JS(
    (async () => {
      const {callNative} = await import('./agent_bridge.js');
      const result = await callNative(
          'endAgentTurn', {turnId: window.__abortE2E.turnId});
      return !!result.success;
    })()
  )JS")
                  .ExtractBool());
  EXPECT_TRUE(
      DaoAgentLeaseManager::GetForProfile(browser()->profile())
          ->TryAcquire({DaoToolClient::kMcp, "abort-e2e-external", "Codex"})
          .has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       NonPageAgentHandlerRequiresTurnLease) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  DaoAgentLeaseManager *leases =
      DaoAgentLeaseManager::GetForProfile(browser()->profile());
  ASSERT_NE(nullptr, leases);
  auto external =
      leases->TryAcquire({DaoToolClient::kMcp, "external-test", "Codex"});
  ASSERT_TRUE(external.has_value());

  EXPECT_EQ("LEASE_BUSY", CallAgentNativeField(
                              agent_contents, "enableNetworkTracking", "code"));
  EXPECT_EQ("LEASE_BUSY",
            CallAgentNativeField(agent_contents, "searchInResources", "code",
                                 R"({"pattern":"Title","types":"Document"})"));
  auto second =
      leases->TryAcquire({DaoToolClient::kMcp, "second-external", "Claude"});
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(DaoToolErrorCode::kLeaseBusy, second.error().code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       AgentSearchHandlerDelegatesToSharedDevToolsTool) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  EXPECT_FALSE(
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId").empty());

  EXPECT_EQ(
      "i", CallAgentNativeField(
               agent_contents, "searchInResources", "flags",
               R"({"pattern":"Title","flags":"","types":"","max_matches":1})"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       StaleTurnCompletionDoesNotReleaseCurrentLease) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  DaoAgentLeaseManager *leases =
      DaoAgentLeaseManager::GetForProfile(browser()->profile());
  ASSERT_NE(nullptr, leases);

  const std::string first_turn =
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId");
  EXPECT_FALSE(first_turn.empty());
  const std::string second_turn =
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId");
  EXPECT_FALSE(second_turn.empty());
  EXPECT_NE(first_turn, second_turn);

  EXPECT_EQ("false",
            CallAgentNativeField(agent_contents, "endAgentTurn", "success",
                                 R"({"turnId":")" + first_turn + R"("})"));
  auto blocked =
      leases->TryAcquire({DaoToolClient::kMcp, "external-test", "Codex"});
  ASSERT_FALSE(blocked.has_value());
  EXPECT_EQ(DaoToolErrorCode::kLeaseBusy, blocked.error().code);

  EXPECT_EQ("true",
            CallAgentNativeField(agent_contents, "endAgentTurn", "success",
                                 R"({"turnId":")" + second_turn + R"("})"));
  EXPECT_TRUE(
      leases->TryAcquire({DaoToolClient::kMcp, "external-test", "Codex"})
          .has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ResourceChainDetachCancelsExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *agent_contents = LoadAgentWebUi();
  const std::string turn_id =
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId");
  ASSERT_FALSE(turn_id.empty());

  const std::string script = base::StrCat({
      R"JS(
    (async () => {
      const callbacks = new Map();
      const cr = window.cr || (window.cr = {});
      const previous = cr.webUIResponse;
      cr.webUIResponse = (id, ok, payload) => {
        const callback = callbacks.get(id);
        if (callback) {
          callbacks.delete(id);
          callback(payload || {});
        } else if (previous) {
          previous(id, ok, payload);
        }
      };
      const call = (method, params) => new Promise(resolve => {
        const id = method + '_resource_detach_' +
            Math.random().toString(36).slice(2);
        callbacks.set(id, resolve);
        chrome.send(method, [id, params || {}]);
      });
      let listCount = 0;
      const listPromise = call('listPageResources', {}).then(result => {
        ++listCount;
        return result;
      });
      const endResult = await call('endAgentTurn', {turnId: ')JS",
      turn_id,
      R"JS('});
      const listResult = await listPromise;
      cr.webUIResponse = previous;
      return JSON.stringify({
        endSuccess: !!endResult.success,
        listCount,
        code: listResult.code || '',
      });
    })()
  )JS"});
  const std::string result =
      content::EvalJs(agent_contents, script).ExtractString();

  EXPECT_NE(std::string::npos, result.find("\"endSuccess\":true"));
  EXPECT_NE(std::string::npos, result.find("\"listCount\":1"));
  EXPECT_NE(std::string::npos, result.find("\"code\":\"TOOL_CANCELLED\""));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       TrackingStateClearsWhenTurnSwitchesTargets) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *first_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome::AddTabAt(browser(), second_url(), 1, false);
  ASSERT_TRUE(content::WaitForLoadStop(
      browser()->tab_strip_model()->GetWebContentsAt(1)));
  content::WebContents *second_contents =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  content::WebContents *agent_contents = LoadAgentWebUi();

  EXPECT_FALSE(
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId").empty());
  EXPECT_EQ("true", CallAgentNativeField(agent_contents,
                                         "enableNetworkTracking", "success"));
  EXPECT_EQ("true", CallAgentNativeField(agent_contents, "getNetworkRequests",
                                         "enabled"));
  ASSERT_TRUE(content::ExecJs(
      first_contents,
      "fetch('/title2.html').then(() => true).catch(() => false)"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return CallAgentNativeField(agent_contents, "getNetworkRequests",
                                "count") != "0";
  }));
  EXPECT_EQ("true", CallAgentNativeField(agent_contents, "switchTab", "success",
                                         R"({"index":1})"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return CallAgentNativeField(agent_contents, "getNetworkRequests",
                                "enabled") == "true";
  }));
  EXPECT_EQ(
      "0", CallAgentNativeField(agent_contents, "getNetworkRequests", "count"));
  ASSERT_TRUE(content::ExecJs(
      first_contents,
      "fetch('/title2.html').then(() => true).catch(() => false)"));
  EXPECT_EQ(
      "0", CallAgentNativeField(agent_contents, "getNetworkRequests", "count"));
  ASSERT_TRUE(content::ExecJs(
      second_contents,
      "fetch('/title1.html').then(() => true).catch(() => false)"));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return CallAgentNativeField(agent_contents, "getNetworkRequests",
                                "count") != "0";
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ClearHighlightPreservesDevToolsTrackingState) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  content::WebContents *target =
      browser()->tab_strip_model()->GetActiveWebContents();
  content::WebContents *agent_contents = LoadAgentWebUi();

  EXPECT_FALSE(
      CallAgentNativeField(agent_contents, "beginAgentTurn", "turnId").empty());
  EXPECT_EQ("true", CallAgentNativeField(agent_contents,
                                         "enableNetworkTracking", "success"));
  ASSERT_TRUE(content::ExecJs(
      target, "fetch('/title2.html').then(() => true).catch(() => false)"));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return CallAgentNativeField(agent_contents, "getNetworkRequests",
                                "count") != "0";
  }));
  const std::string count_before =
      CallAgentNativeField(agent_contents, "getNetworkRequests", "count");

  EXPECT_TRUE(content::EvalJs(agent_contents, R"JS(
    new Promise(resolve => {
      const id = 'clear_highlight_preserves_tracking_' +
          Math.random().toString(36).slice(2);
      const cr = window.cr || (window.cr = {});
      const previous = cr.webUIResponse;
      cr.webUIResponse = (callbackId, isSuccess, payload) => {
        if (callbackId !== id) {
          if (previous) {
            previous(callbackId, isSuccess, payload);
          }
          return;
        }
        cr.webUIResponse = previous;
        resolve(isSuccess && payload === true);
      };
      chrome.send('clearHighlight', [id, {}]);
    })
  )JS")
                  .ExtractBool());
  EXPECT_EQ("true", CallAgentNativeField(agent_contents, "getNetworkRequests",
                                         "enabled"));
  EXPECT_EQ(count_before, CallAgentNativeField(agent_contents,
                                               "getNetworkRequests", "count"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpPageToolsBrowserTest,
                       ProtocolFailureDoesNotCommitTrackingState) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  ASSERT_TRUE(devtools_client_->AttachTo(
      browser()->tab_strip_model()->GetActiveWebContents()));

  bool tracking_enabled = false;
  int callback_count = 0;
  base::test::TestFuture<void> done;
  devtools_client_->SendCommand(
      "Network.enableInvalidForTest", base::DictValue(),
      base::BindOnce(
          [](bool *enabled, int *count, base::OnceClosure done,
             DaoDevToolsClient::CommandResult result) {
            ++*count;
            if (result.has_value()) {
              *enabled = true;
            }
            EXPECT_FALSE(result.has_value());
            EXPECT_EQ(DaoToolErrorCode::kInternalError, result.error().code);
            std::move(done).Run();
          },
          &tracking_enabled, &callback_count, done.GetCallback()));
  EXPECT_TRUE(done.Wait());

  EXPECT_FALSE(tracking_enabled);
  EXPECT_EQ(1, callback_count);
}

} // namespace
} // namespace dao
