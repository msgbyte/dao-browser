// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>

#include "base/auto_reset.h"
#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/browser_shutdown.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/agent_space/dao_agent_space.h"
#include "dao/browser/agent_space/dao_agent_space_service.h"
#include "dao/browser/agent_space/dao_hidden_browser.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "net/dns/mock_host_resolver.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/base_window.h"

namespace dao {
namespace {

class DaoAgentSpaceBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    service_ = DaoAgentSpaceService::GetForProfile(browser()->GetProfile());
    ASSERT_TRUE(service_);
  }

  void TearDownOnMainThread() override {
    for (DaoAgentSpace* space : service_->GetSpaces()) {
      service_->Stop(space->id());
    }
    service_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }

  DaoBrowserToolResult Call(std::string name,
                            base::DictValue arguments,
                            DaoToolClient client = DaoToolClient::kDaoAgent) {
    base::test::TestFuture<DaoBrowserToolResult> future;
    DaoBrowserToolCall call;
    call.request_id = base::NumberToString(++next_request_id_);
    call.name = std::move(name);
    call.arguments = std::move(arguments);
    service_->ExecuteTool({client, "test-client", "Test agent"},
                          std::move(call), future.GetCallback());
    return future.Take();
  }

  std::string CreateSpace(const GURL& url) {
    DaoBrowserToolResult result =
        Call("create_space",
             base::DictValue().Set("name", "Test task").Set("url", url.spec()));
    EXPECT_FALSE(result.error) << result.error->message;
    const std::string* space_id =
        result.data.is_dict() ? result.data.GetDict().FindString("space_id")
                              : nullptr;
    EXPECT_TRUE(space_id);
    return space_id ? *space_id : std::string();
  }

  DaoAgentSpace* Space(const std::string& space_id) {
    return service_->FindSpace(space_id);
  }

  base::Value Eval(const std::string& space_id, const std::string& code) {
    DaoBrowserToolResult result =
        Call("execute_script",
             base::DictValue().Set("space_id", space_id).Set("code", code));
    EXPECT_FALSE(result.error) << result.error->message;
    const base::Value* value =
        result.data.is_dict() ? result.data.GetDict().Find("result") : nullptr;
    return value ? value->Clone() : base::Value();
  }

  std::string EvalText(const std::string& space_id, const std::string& code) {
    base::Value value = Eval(space_id, code);
    return value.is_string() ? value.GetString() : value.DebugString();
  }

  bool WaitForLoad(const std::string& space_id) {
    return base::test::RunUntil([&] {
      DaoAgentSpace* space = Space(space_id);
      content::WebContents* contents =
          space ? space->GetCurrentContents() : nullptr;
      return contents && !contents->IsLoading() &&
             !contents->GetLastCommittedURL().is_empty();
    });
  }

  std::string ErrorCode(const DaoBrowserToolResult& result) {
    return result.error
               ? std::string(DaoToolErrorCodeToString(result.error->code))
               : std::string();
  }

  raw_ptr<DaoAgentSpaceService> service_ = nullptr;
  int next_request_id_ = 0;
};

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       RejectsExpiredConsentBeforeExecutingSpaceTools) {
  DaoAgentSpaceClient client{DaoToolClient::kMcp, "expiring-client", "Test"};
  client.authorization_deadline = base::TimeTicks::Now();
  DaoBrowserToolCall call;
  call.request_id = "expired-create";
  call.name = "create_space";
  call.arguments.Set("name", "Must not open");
  base::test::TestFuture<DaoBrowserToolResult> future;
  service_->ExecuteTool(client, std::move(call), future.GetCallback());
  EXPECT_EQ("AUTHORIZATION_DENIED", ErrorCode(future.Take()));
  EXPECT_EQ(0u, service_->GetActiveSpaceCount());
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       CreatesHiddenWindowThatFindersSkip) {
  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  DaoAgentSpace* space = Space(space_id);
  ASSERT_TRUE(space);
  BrowserWindowInterface* space_browser = space->browser();
  ASSERT_TRUE(space_browser);

  EXPECT_EQ(browsers_before + 1,
            GlobalBrowserCollection::GetInstance()->GetSize());
  EXPECT_FALSE(space_browser->GetWindow()->IsVisible());
  EXPECT_TRUE(IsDaoHiddenBrowser(space_browser));
  EXPECT_NE(space_browser,
            GlobalBrowserCollection::GetInstance()->GetLastActiveBrowser());
  EXPECT_NE(space_browser,
            ProfileBrowserCollection::GetForProfile(browser()->GetProfile())
                ->FindTabbedBrowser(false));
  EXPECT_EQ(1, space->GetTabCount());
  EXPECT_EQ(1, TabListInterface::From(browser())->GetTabCount());
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       ClosingTaskWindowKeepsSpaceRunning) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  DaoAgentSpace* space = Space(space_id);
  BrowserWindowInterface* space_browser = space->browser();
  Eval(space_id, "window.savedTaskState = 'still running'");

  for (int attempt = 0; attempt < 2; ++attempt) {
    ASSERT_TRUE(service_->ShowWindow(space_id));
    EXPECT_TRUE(space_browser->GetWindow()->IsVisible());
    space_browser->GetWindow()->Close();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !space->browser() || !space->browser()->GetWindow()->IsVisible();
    }));
    ASSERT_EQ(space_browser, space->browser());
    EXPECT_EQ(DaoAgentSpaceState::kRunning, space->state());
    EXPECT_TRUE(IsDaoHiddenBrowser(space_browser));
    EXPECT_EQ(base::Value("still running"),
              Eval(space_id, "window.savedTaskState"));
    EXPECT_EQ(base::Value("visible"), Eval(space_id, "document.visibilityState"));
  }

  ASSERT_TRUE(service_->Stop(space_id));
  EXPECT_EQ(DaoAgentSpaceState::kStopped, space->state());
  ASSERT_TRUE(base::test::RunUntil([&] { return !space->browser(); }));
  EXPECT_EQ("SPACE_CLOSED",
            ErrorCode(Call("get_page_info",
                           base::DictValue().Set("space_id", space_id))));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       ClosingProfileWindowsStopsSpacesWithoutBeforeUnload) {
  auto user_browser = browser()->GetWeakPtr();
  for (bool with_callback : {false, true}) {
    SCOPED_TRACE(with_callback);
    ProfileManager* manager = g_browser_process->profile_manager();
    Profile& profile = profiles::testing::CreateProfileSync(
        manager, manager->GenerateNextProfileDirectoryPath());
    auto keep_alive = ScopedProfileKeepAlive::TryAcquire(
        &profile, ProfileKeepAliveOrigin::kBrowserWindow);
    ASSERT_TRUE(keep_alive);
    auto profile_browser = CreateBrowser(&profile)->GetWeakPtr();
    base::AutoReset<raw_ptr<DaoAgentSpaceService>> use_profile(
        &service_, DaoAgentSpaceService::GetForProfile(&profile));
    base::ScopedClosureRunner stop_spaces(base::BindOnce(
        [](DaoAgentSpaceService* service) {
          for (DaoAgentSpace* space : service->GetSpaces()) {
            service->Stop(space->id());
          }
        },
        base::Unretained(service_.get())));
    const std::string space_id =
        CreateSpace(embedded_test_server()->GetURL("/title1.html"));
    ASSERT_TRUE(WaitForLoad(space_id));
    DaoAgentSpace* space = Space(space_id);
    auto space_browser = space->browser()->GetWeakPtr();
    if (with_callback) {
      ASSERT_TRUE(service_->ShowWindow(space_id));
    }

    ASSERT_FALSE(browser_shutdown::IsTryingToQuit());
    if (with_callback) {
      base::test::TestFuture<const base::FilePath&> closed;
      chrome::CloseAllBrowsersWithProfile(
          &profile, /*skip_beforeunload=*/false,
          closed.GetRepeatingCallback(), base::NullCallback());
      EXPECT_EQ(profile.GetPath(), closed.Get());
    } else {
      chrome::CloseAllBrowsersWithProfile(&profile);
    }

    ASSERT_TRUE(base::test::RunUntil(
        [&] { return !space_browser && !profile_browser; }));
    EXPECT_EQ(DaoAgentSpaceState::kStopped, space->state());
    EXPECT_EQ("SPACE_CLOSED",
              ErrorCode(Call("get_page_info",
                             base::DictValue().Set("space_id", space_id))));
    EXPECT_TRUE(user_browser);
  }
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       ClosingIncognitoProfileStopsCleanSpace) {
  DaoBrowserToolResult result =
      Call("create_space", base::DictValue()
                               .Set("name", "Clean task")
                               .Set("url", embedded_test_server()
                                               ->GetURL("/title1.html")
                                               .spec())
                               .Set("logins", false));
  ASSERT_FALSE(result.error) << result.error->message;
  const std::string space_id = *result.data.GetDict().FindString("space_id");
  ASSERT_TRUE(WaitForLoad(space_id));
  DaoAgentSpace* space = Space(space_id);
  auto space_browser = space->browser()->GetWeakPtr();
  Profile* profile = space_browser->GetProfile();
  ASSERT_TRUE(profile->IsOffTheRecord());

  chrome::CloseAllBrowsersWithIncognitoProfile(profile,
                                              /*skip_beforeunload=*/false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !space_browser; }));
  EXPECT_EQ(DaoAgentSpaceState::kStopped, space->state());
  EXPECT_EQ("SPACE_CLOSED",
            ErrorCode(Call("get_page_info",
                           base::DictValue().Set("space_id", space_id))));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       ClosingAllWindowsWithoutQuittingStopsSpaces) {
  ScopedKeepAlive keep_alive(KeepAliveOrigin::PANEL_VIEW,
                            KeepAliveRestartOption::DISABLED);
  Profile* profile = browser()->GetProfile();
  auto profile_keep_alive = ScopedProfileKeepAlive::TryAcquire(
      profile, ProfileKeepAliveOrigin::kBrowserWindow);
  ASSERT_TRUE(profile_keep_alive);
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  DaoAgentSpace* space = Space(space_id);
  auto space_browser = space->browser()->GetWeakPtr();

  ASSERT_FALSE(browser_shutdown::IsTryingToQuit());
  chrome::CloseAllBrowsers();
  EXPECT_TRUE(base::test::RunUntil(
      [] { return GlobalBrowserCollection::GetInstance()->IsEmpty(); }));
  EXPECT_FALSE(browser_shutdown::IsTryingToQuit());
  EXPECT_FALSE(space_browser);
  EXPECT_EQ(DaoAgentSpaceState::kStopped, space->state());
  EXPECT_EQ("SPACE_CLOSED",
            ErrorCode(Call("get_page_info",
                           base::DictValue().Set("space_id", space_id))));

  // Keep the fixture's profile and services alive after closing every window.
  SetBrowser(CreateBrowser(profile));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       CancellingCloseAllWindowsKeepsSpacesRunning) {
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::NavigateToURL(
      contents, embedded_test_server()->GetURL("/beforeunload.html")));
  content::PrepContentsForBeforeUnloadTest(contents);
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  DaoAgentSpace* space = Space(space_id);
  auto space_browser = space->browser()->GetWeakPtr();
  auto user_browser = browser()->GetWeakPtr();

  ASSERT_FALSE(browser_shutdown::IsTryingToQuit());
  chrome::CloseAllBrowsers();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  EXPECT_EQ(DaoAgentSpaceState::kRunning, space->state());
  dialog->view()->CancelAppModalDialog();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null;"));

  EXPECT_TRUE(user_browser);
  EXPECT_TRUE(space_browser);
  EXPECT_EQ(DaoAgentSpaceState::kRunning, space->state());
  EXPECT_EQ(base::Value("visible"), Eval(space_id, "document.visibilityState"));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       ClosingTaskWindowWhileQuittingStopsSpace) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  DaoAgentSpace* space = Space(space_id);
  ASSERT_TRUE(service_->ShowWindow(space_id));

  const bool was_quitting = browser_shutdown::IsTryingToQuit();
  base::ScopedClosureRunner restore_quit_state(
      base::BindOnce(&browser_shutdown::SetTryingToQuit, was_quitting));
  browser_shutdown::SetTryingToQuit(true);
  space->browser()->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !space->browser(); }));
  EXPECT_EQ(DaoAgentSpaceState::kStopped, space->state());
}

// The hidden window must behave like a foreground one: visible to the page,
// rendering frames and running timers at full speed.
IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, HiddenPagesRenderNormally) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  EXPECT_EQ(base::Value("visible"), Eval(space_id, "document.visibilityState"));
  Eval(space_id,
       "window.__frames = 0; (function tick() { window.__frames++; "
       "requestAnimationFrame(tick); })(); true");
  int frames = 0;
  for (int attempt = 0; attempt < 50 && frames <= 10; ++attempt) {
    base::RunLoop wait;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), base::Milliseconds(100));
    wait.Run();
    // execute_script reports values as text.
    base::StringToInt(EvalText(space_id, "window.__frames"), &frames);
  }
  EXPECT_GT(frames, 10);
}

// Sign-ins carry over: a cookie the user's window set is sent from a space,
// while a clean space starts without it.
IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       SpacesShareSignInsUnlessClean) {
  ASSERT_TRUE(content::NavigateToURL(
      browser()->tab_strip_model()->GetActiveWebContents(),
      embedded_test_server()->GetURL("a.test", "/set-cookie?dao_login=1")));
  const GURL echo =
      embedded_test_server()->GetURL("a.test", "/echoheader?Cookie");

  const std::string signed_in = CreateSpace(echo);
  ASSERT_TRUE(WaitForLoad(signed_in));
  EXPECT_EQ(base::Value("dao_login=1"),
            Eval(signed_in, "document.body.innerText.trim()"));

  DaoBrowserToolResult clean =
      Call("create_space", base::DictValue()
                               .Set("name", "Clean task")
                               .Set("url", echo.spec())
                               .Set("logins", false));
  ASSERT_FALSE(clean.error) << clean.error->message;
  const std::string clean_id = *clean.data.GetDict().FindString("space_id");
  ASSERT_TRUE(WaitForLoad(clean_id));
  EXPECT_EQ(base::Value("None"),
            Eval(clean_id, "document.body.innerText.trim()"));
  EXPECT_TRUE(Space(clean_id)->browser()->GetProfile()->IsOffTheRecord());
}

// Single-page apps keep working: client-side routes are what tools report.
IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, SinglePageRoutesWork) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  EXPECT_EQ(base::Value("/app/orders"),
            Eval(space_id,
                 "history.pushState({}, '', '/app/orders'); "
                 "location.pathname"));
  DaoBrowserToolResult info =
      Call("get_page_info", base::DictValue().Set("space_id", space_id));
  ASSERT_FALSE(info.error) << info.error->message;
  EXPECT_NE(std::string::npos, info.data.DebugString().find("/app/orders"))
      << info.data.DebugString();
}

// Media plays in a hidden window as it would on screen.
IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, HiddenVideosPlay) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  Eval(space_id,
       "window.__video = document.createElement('video');"
       "__video.muted = true; __video.src = '/media/bigbuck.webm';"
       "document.body.appendChild(__video); __video.play(); true");
  double played = 0;
  for (int attempt = 0; attempt < 100 && played < 0.5; ++attempt) {
    base::RunLoop wait;
    base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE, wait.QuitClosure(), base::Milliseconds(100));
    wait.Run();
    base::StringToDouble(EvalText(space_id, "__video.currentTime"), &played);
  }
  EXPECT_GE(played, 0.5);
}

// Nobody can see dialogs in a hidden window. Alerts let the page continue and
// are reported to the agent; confirm and prompt wait for the agent's answer
// instead of being cancelled, and block other tools until then.
IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, AgentAnswersHiddenDialogs) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));

  DaoBrowserToolResult alert =
      Call("execute_script", base::DictValue()
                                 .Set("space_id", space_id)
                                 .Set("code", "alert('Saved'); 'after alert'"));
  ASSERT_FALSE(alert.error) << alert.error->message;
  EXPECT_EQ("after alert", *alert.data.GetDict().FindString("result"));
  const base::ListValue* dialogs = alert.data.GetDict().FindList("dialogs");
  ASSERT_TRUE(dialogs);
  ASSERT_EQ(1u, dialogs->size());
  EXPECT_EQ("Saved", *(*dialogs)[0].GetDict().FindString("message"));

  // A confirm stops the call that triggered it and every later page tool.
  DaoBrowserToolResult blocked =
      Call("execute_script",
           base::DictValue()
               .Set("space_id", space_id)
               .Set("code", "window.__answer = confirm('Delete it?'); true"));
  EXPECT_EQ("PAGE_DIALOG_OPEN", ErrorCode(blocked));
  ASSERT_TRUE(Space(space_id)->pending_dialog());
  EXPECT_EQ("confirm", Space(space_id)->pending_dialog()->type);
  EXPECT_EQ("PAGE_DIALOG_OPEN",
            ErrorCode(Call("get_page_info",
                           base::DictValue().Set("space_id", space_id))));

  DaoBrowserToolResult answer =
      Call("handle_dialog",
           base::DictValue().Set("space_id", space_id).Set("accept", true));
  ASSERT_FALSE(answer.error) << answer.error->message;
  EXPECT_FALSE(Space(space_id)->pending_dialog());
  EXPECT_EQ("true", EvalText(space_id, "window.__answer"));

  Call("execute_script",
       base::DictValue()
           .Set("space_id", space_id)
           .Set("code", "window.__name = prompt('Name?', 'Guest'); true"));
  ASSERT_TRUE(Space(space_id)->pending_dialog());
  ASSERT_FALSE(Call("handle_dialog", base::DictValue()
                                         .Set("space_id", space_id)
                                         .Set("accept", true)
                                         .Set("prompt_text", "Ada"))
                   .error);
  EXPECT_EQ("Ada", EvalText(space_id, "window.__name"));

  EXPECT_EQ(
      "INVALID_ARGUMENT",
      ErrorCode(Call(
          "handle_dialog",
          base::DictValue().Set("space_id", space_id).Set("accept", false))));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       BrowserToolsStayInsideTheSpace) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  const int user_tabs = TabListInterface::From(browser())->GetTabCount();

  DaoBrowserToolResult opened = Call(
      "open_tab",
      base::DictValue()
          .Set("space_id", space_id)
          .Set("url", embedded_test_server()->GetURL("/title2.html").spec())
          .Set("label", "Open second page"));
  ASSERT_FALSE(opened.error) << opened.error->message;
  EXPECT_EQ(2, Space(space_id)->GetTabCount());
  EXPECT_EQ(user_tabs, TabListInterface::From(browser())->GetTabCount());
  ASSERT_TRUE(WaitForLoad(space_id));

  // A popup from a page in the space opens as a tab of the same hidden window.
  // Pages open popups from a click, which carries a user gesture.
  const size_t browsers = GlobalBrowserCollection::GetInstance()->GetSize();
  Eval(space_id,
       content::JsReplace(
           "document.body.innerHTML = $1; true",
           base::StrCat(
               {"<button onclick=\"window.open('",
                embedded_test_server()->GetURL("/title3.html").spec(),
                "', '_blank', 'popup,width=300')\">Open popup</button>"})));
  DaoBrowserToolResult found =
      Call("query_elements", base::DictValue()
                                 .Set("space_id", space_id)
                                 .Set("role", "button")
                                 .Set("name", "Open popup"));
  ASSERT_FALSE(found.error) << found.error->message;
  const base::DictValue& query = found.data.GetDict();
  const base::ListValue* matches = query.FindList("matches");
  ASSERT_TRUE(matches && !matches->empty()) << query.DebugString();
  base::DictValue click = base::DictValue().Set("space_id", space_id);
  click.Set("ref_id", *(*matches)[0].GetDict().FindString("ref_id"));
  for (const char* key : {"document_id", "snapshot_id"}) {
    if (const base::Value* value = query.Find(key)) {
      click.Set(key, value->Clone());
    }
  }
  DaoBrowserToolResult clicked = Call("click_by_ref", std::move(click));
  ASSERT_FALSE(clicked.error) << clicked.error->message;
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Space(space_id)->GetTabCount() == 3; }));
  EXPECT_EQ(browsers, GlobalBrowserCollection::GetInstance()->GetSize());
  EXPECT_FALSE(Space(space_id)->browser()->GetWindow()->IsVisible());

  const auto& events = Space(space_id)->events();
  ASSERT_FALSE(events.empty());
  bool saw_label = false;
  for (const DaoAgentSpaceEvent& event : events) {
    saw_label |= event.label == "Open second page";
  }
  EXPECT_TRUE(saw_label);
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       TakeOverBlocksAgentUntilHandBack) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));

  ASSERT_TRUE(service_->TakeOver(space_id));
  EXPECT_EQ(DaoAgentSpaceState::kUserInControl, Space(space_id)->state());
  DaoBrowserToolResult blocked =
      Call("get_page_info", base::DictValue().Set("space_id", space_id));
  EXPECT_EQ("SPACE_USER_IN_CONTROL", ErrorCode(blocked));

  ASSERT_TRUE(service_->HandBack(space_id));
  DaoBrowserToolResult allowed =
      Call("get_page_info", base::DictValue().Set("space_id", space_id));
  EXPECT_FALSE(allowed.error);
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       RequestUserHelpWaitsForHandBack) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));

  base::test::TestFuture<DaoBrowserToolResult> help;
  DaoBrowserToolCall call;
  call.request_id = "help";
  call.name = "request_user_help";
  call.arguments = base::DictValue()
                       .Set("space_id", space_id)
                       .Set("reason", "Sign in to continue");
  service_->ExecuteTool({DaoToolClient::kDaoAgent, "test-client", "Test"},
                        std::move(call), help.GetCallback());
  EXPECT_FALSE(help.IsReady());
  EXPECT_EQ(DaoAgentSpaceState::kUserInControl, Space(space_id)->state());
  EXPECT_EQ("Sign in to continue", Space(space_id)->handoff_message());

  ASSERT_TRUE(service_->ShowWindow(space_id));
  Space(space_id)->browser()->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !Space(space_id)->browser() ||
           !Space(space_id)->browser()->GetWindow()->IsVisible();
  }));
  ASSERT_TRUE(Space(space_id)->browser());
  EXPECT_FALSE(help.IsReady());
  EXPECT_EQ(DaoAgentSpaceState::kUserInControl, Space(space_id)->state());
  EXPECT_EQ("SPACE_USER_IN_CONTROL",
            ErrorCode(Call("get_page_info",
                           base::DictValue().Set("space_id", space_id))));

  ASSERT_TRUE(service_->HandBack(space_id));
  DaoBrowserToolResult result = help.Take();
  ASSERT_FALSE(result.error);
  EXPECT_TRUE(result.data.GetDict().FindDict("user_changes"));
  EXPECT_EQ(DaoAgentSpaceState::kRunning, Space(space_id)->state());
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, FinishKeepsChosenTabs) {
  const GURL url = embedded_test_server()->GetURL("/title2.html");
  const std::string space_id = CreateSpace(url);
  ASSERT_TRUE(WaitForLoad(space_id));
  DaoAgentSpace* space = Space(space_id);
  const base::DictValue info = space->ToDict(false);
  const std::string tab_id = *info.FindString("tab_id");
  const int user_tabs = TabListInterface::From(browser())->GetTabCount();

  DaoBrowserToolResult finished =
      Call("finish_space", base::DictValue()
                               .Set("space_id", space_id)
                               .Set("keep", base::ListValue().Append(tab_id))
                               .Set("summary", "Found it"));
  ASSERT_FALSE(finished.error) << finished.error->message;
  EXPECT_EQ(DaoAgentSpaceState::kFinished, space->state());
  EXPECT_EQ(user_tabs + 1, TabListInterface::From(browser())->GetTabCount());
  ASSERT_TRUE(base::test::RunUntil([&] { return !space->browser(); }));

  DaoBrowserToolResult after =
      Call("get_page_info", base::DictValue().Set("space_id", space_id));
  EXPECT_EQ("SPACE_CLOSED", ErrorCode(after));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, SpacesAreScopedToClient) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  DaoBrowserToolResult other =
      Call("get_page_info", base::DictValue().Set("space_id", space_id),
           DaoToolClient::kMcp);
  EXPECT_EQ("SPACE_NOT_FOUND", ErrorCode(other));
  DaoBrowserToolResult listed =
      Call("list_spaces", base::DictValue(), DaoToolClient::kMcp);
  ASSERT_FALSE(listed.error);
  EXPECT_EQ(0, *listed.data.GetDict().FindInt("count"));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, ScriptsDriveSpaces) {
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  DaoBrowserToolResult result = Call(
      "run_browser_script",
      base::DictValue().Set(
          "code",
          base::StrCat({"const s = await dao.createSpace('Script task', '",
                        url.spec(),
                        "');\n"
                        "console.log('created', s.id);\n"
                        "const title = await s.eval('document.title');\n"
                        "return {id: s.id, title, sandboxed: "
                        "typeof chrome === 'undefined' || !chrome.send};"})));
  ASSERT_FALSE(result.error) << result.error->message;
  const base::DictValue& data = result.data.GetDict();
  ASSERT_TRUE(data.FindBool("success").value_or(false)) << data.DebugString();
  const base::DictValue* value = data.FindDict("result");
  ASSERT_TRUE(value);
  EXPECT_TRUE(value->FindBool("sandboxed").value_or(false));
  EXPECT_TRUE(Space(*value->FindString("id")));
  const base::ListValue* logs = data.FindList("logs");
  ASSERT_TRUE(logs && !logs->empty());
  EXPECT_TRUE(base::StartsWith((*logs)[0].GetString(), "created"));
}

// Scripts reach pages only through space tools: the sandbox page itself has
// no network, so it cannot use the user's cookies directly.
IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, ScriptSandboxIsOffline) {
  const GURL url = embedded_test_server()->GetURL("/title1.html");
  DaoBrowserToolResult result =
      Call("run_browser_script",
           base::DictValue().Set("code", base::StrCat({"try {\n"
                                                       "  await fetch('",
                                                       url.spec(),
                                                       "');\n"
                                                       "  return 'online';\n"
                                                       "} catch (e) {\n"
                                                       "  return 'offline';\n"
                                                       "}"})));
  ASSERT_FALSE(result.error) << result.error->message;
  EXPECT_EQ("offline", *result.data.GetDict().FindString("result"));
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest, ScriptSyntaxErrorsHaveLines) {
  DaoBrowserToolResult result =
      Call("run_browser_script",
           base::DictValue().Set("code", "const a = 1;\nconst = ;\nreturn a;"));
  ASSERT_TRUE(result.error);
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_NE(std::string::npos, result.error->message.find("line 2"))
      << result.error->message;
}

IN_PROC_BROWSER_TEST_F(DaoAgentSpaceBrowserTest,
                       ScriptsHardStopWhenUserTakesOver) {
  const std::string space_id =
      CreateSpace(embedded_test_server()->GetURL("/title1.html"));
  ASSERT_TRUE(WaitForLoad(space_id));
  ASSERT_TRUE(service_->TakeOver(space_id));
  DaoBrowserToolResult result =
      Call("run_browser_script",
           base::DictValue()
               .Set("space_id", space_id)
               .Set("code",
                    "console.log('secret partial output');\n"
                    "await space.info();\nreturn 'unreachable';"));
  ASSERT_TRUE(result.error);
  EXPECT_EQ(DaoToolErrorCode::kSpaceUserInControl, result.error->code);
  EXPECT_TRUE(result.data.is_none());
}

}  // namespace
}  // namespace dao
