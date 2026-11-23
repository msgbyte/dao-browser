// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_home_ui.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/process/launch.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/test/gtest_tags.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/content_switches.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/home/dao_home_connector_executor.h"
#include "dao/browser/home/dao_home_external_service.h"
#include "dao/browser/home/dao_home_file_snapshot.h"
#include "dao/browser/home/dao_home_project_service.h"
#include "dao/browser/home/dao_home_project_service_factory.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view_utils.h"

namespace dao {
namespace {

HomeFileSnapshot BasicProject() {
  return {
      {"manifest.json",
       R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[],"limits":{"max_result_bytes":1048576,"max_items_per_connector":100}})"},
      {"index.html",
       "<!doctype html><html><body><main>External "
       "fixture</main></body></html>"},
  };
}

class DaoHomeExternalUIBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
#if BUILDFLAG(IS_MAC)
    // The real Home listener must fit macOS's Unix socket address limit.
    ASSERT_TRUE(user_data_dir_.CreateUniqueTempDirUnderPath(
        base::FilePath("/tmp"), "dao-home-"));
#else
    ASSERT_TRUE(user_data_dir_.CreateUniqueTempDir());
#endif
    command_line->AppendSwitchPath(switches::kUserDataDir,
                                   user_data_dir_.GetPath());
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    DaoHomeExternalService::Get()->SetApprovalForTesting(
        base::BindRepeating([](Profile*) { return true; }));
  }

  void TearDownOnMainThread() override {
    if (!cli_workspace_dir_.empty()) {
      const auto cleaned =
          RunCli("clean", {{"discard", ""}, {"close-session", ""}});
      EXPECT_EQ(true, cleaned.FindBool("ok"));
    }
    if (!token_.empty()) {
      Call("session.close");
    }
    DaoHomeExternalService::Get()->SetApprovalForTesting({});
    InProcessBrowserTest::TearDownOnMainThread();
  }

  base::DictValue RunCli(
      const std::string& command,
      const std::vector<std::pair<std::string, std::string>>& switches = {},
      bool expected_success = true) {
    SCOPED_TRACE(command);
    const auto executable = base::PathService::CheckedGet(base::DIR_EXE)
#if BUILDFLAG(IS_WIN)
                                .AppendASCII("dao-browser.exe");
#else
                                .AppendASCII("dao-browser");
#endif
    base::CommandLine command_line(executable);
    command_line.AppendArg("home");
    command_line.AppendArg(command);
    command_line.AppendSwitch("no-launch");
    command_line.AppendSwitchPath(
        "user-data-dir", base::PathService::CheckedGet(chrome::DIR_USER_DATA));
    if (!cli_workspace_dir_.empty()) {
      command_line.AppendSwitchPath("workspace", cli_workspace_dir_);
    }
    for (const auto& [name, value] : switches) {
      command_line.AppendSwitchASCII(name, value);
    }
    base::test::TestFuture<std::pair<bool, std::string>> result;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
        base::BindOnce(
            [](base::CommandLine command_line) {
              std::string output;
              const bool success = base::GetAppOutput(command_line, &output);
              return std::make_pair(success, std::move(output));
            },
            std::move(command_line)),
        result.GetCallback());
    auto [success, output] = result.Take();
    EXPECT_EQ(expected_success, success) << output;
    auto response = base::JSONReader::ReadDict(output, base::JSON_PARSE_RFC);
    EXPECT_TRUE(response.has_value()) << output;
    return response ? std::move(*response) : base::DictValue();
  }

  base::DictValue Call(std::string method, base::DictValue params = {}) {
    if (!token_.empty()) {
      params.Set("token", token_);
    }
    base::test::TestFuture<base::DictValue> result;
    DaoHomeExternalService::Get()->ExecuteForTesting(
        std::move(method), std::move(params), result.GetCallback());
    return result.Take();
  }

  void OpenAndStage(HomeFileSnapshot files = BasicProject()) {
    auto opened =
        Call("session.open", base::DictValue().Set("reason", "Test preview"));
    ASSERT_EQ(true, opened.FindBool("ok")) << opened.DebugString();
    ASSERT_TRUE(opened.FindString("token")) << opened.DebugString();
    token_ = *opened.FindString("token");
    digest_ = HomeFileSnapshotDigest(files);
    auto staged =
        Call("draft.stage", base::DictValue()
                                .Set("base_revision", "")
                                .Set("files", EncodeHomeFileSnapshot(files))
                                .Set("digest", digest_));
    ASSERT_EQ(true, staged.FindBool("ok")) << staged.DebugString();
    ASSERT_TRUE(staged.FindString("draft_id")) << staged.DebugString();
    draft_id_ = *staged.FindString("draft_id");
  }

  content::WebContents* PreviewContents() {
    content::WebContents* result = nullptr;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* window) {
          for (int index = 0; index < window->GetTabStripModel()->count();
               ++index) {
            auto* contents =
                window->GetTabStripModel()->GetWebContentsAt(index);
            if (DaoHomeExternalService::Get()->GetContext(contents).is_dict()) {
              result = contents;
              return false;
            }
          }
          return true;
        });
    return result;
  }

  std::string token_;
  std::string draft_id_;
  std::string digest_;
  base::ScopedTempDir user_data_dir_;
  base::FilePath cli_workspace_dir_;
};

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       EditPromptCannotFollowATabSwitchOrBeReplayed) {
  auto* service =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
  service->StageSnapshot("", BasicProject(), "Existing Home",
                         staged.GetCallback());
  ASSERT_TRUE(staged.Get().has_value());
  base::test::TestFuture<base::expected<HomeVersion, HomeError>> published;
  service->Publish(staged.Get()->id, HomeRevisionKind::kInitial,
                   published.GetCallback(), nullptr);
  ASSERT_TRUE(published.Get().has_value());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* home = browser()->tab_strip_model()->GetActiveWebContents();
  auto* sidebar =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_agent_sidebar();
  ASSERT_TRUE(sidebar);
  if (!sidebar->is_expanded()) {
    sidebar->Toggle();
  }
  content::WebContents* agent = nullptr;
  for (views::View* child : sidebar->children()) {
    if (auto* web_view = views::AsViewClass<views::WebView>(child)) {
      agent = web_view->GetWebContents();
      break;
    }
  }
  ASSERT_TRUE(agent);
  ASSERT_TRUE(content::WaitForLoadStop(agent));
  ASSERT_EQ(true, content::EvalJs(agent, R"js(
    customElements.whenDefined('dao-agent-app').then(() => true)
  )js"));
  auto capture_click = [&] {
    ASSERT_TRUE(content::ExecJs(agent, R"js(
      window.editRequest = new Promise(resolve => {
        window.__daoExternalSubmit = (_, options) => resolve(options.homeEdit);
      });
      true;
    )js"));
    ASSERT_TRUE(content::ExecJs(home, content::JsReplace(
        "chrome.send('openHomeAgent', ['edit', $1])", published.Get()->id)));
    ASSERT_EQ(true, content::EvalJs(
                        agent, "editRequest.then(edit => !!edit.token)"));
  };
  ASSERT_TRUE(content::ExecJs(agent, R"js(
    window.beginEdit = async (retry = false) => {
      const {callNative} = await import('./agent_bridge.js');
      const {token, ...origin} = await editRequest;
      const homeEdit = retry ? {...origin, resumeToken: window.resumeToken}
                             : {...origin, token};
      const result = await callNative('beginAgentTurn', {homeEdit});
      if (result.turnId) {
        window.turnId = result.turnId;
        window.resumeToken = result.homeEditResumeToken;
      }
      return result.homeContext?.revision ?? result.code;
    };
    window.validateEdit = async () => {
      const {callNative} = await import('./agent_bridge.js');
      return callNative('validateHomeEdit', {turnId, resumeToken});
    };
    true;
  )js"));
  ASSERT_NO_FATAL_FAILURE(capture_click());
  EXPECT_EQ(published.Get()->id, content::EvalJs(agent, "beginEdit()"));
  EXPECT_EQ(true, content::EvalJs(agent, "validateEdit()"));
  EXPECT_EQ("TOOL_CANCELLED", content::EvalJs(agent, "beginEdit()"));
  ASSERT_TRUE(content::ExecJs(agent, R"js(
    (async () => {
      const {callNative} = await import('./agent_bridge.js');
      return (await callNative('endAgentTurn', {turnId})).success;
    })()
  )js"));
  EXPECT_EQ(false, content::EvalJs(agent, "validateEdit()"));
  EXPECT_EQ(published.Get()->id, content::EvalJs(agent, "beginEdit(true)"));
  EXPECT_EQ(true, content::EvalJs(agent, "validateEdit()"));
  ASSERT_NO_FATAL_FAILURE(capture_click());
  ASSERT_TRUE(AddTabAtIndex(-1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  EXPECT_EQ(false, content::EvalJs(agent, "validateEdit()"));
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(home));
  EXPECT_EQ(false, content::EvalJs(agent, "validateEdit()"));
  EXPECT_EQ("TOOL_CANCELLED", content::EvalJs(agent, "beginEdit(true)"));
  EXPECT_EQ("TOOL_CANCELLED", content::EvalJs(agent, "beginEdit()"));
  ASSERT_NO_FATAL_FAILURE(capture_click());
  EXPECT_EQ(published.Get()->id, content::EvalJs(agent, "beginEdit()"));
  base::test::TestFuture<base::expected<void, HomeError>> reset;
  service->Reset(published.Get()->id, reset.GetCallback(), nullptr);
  ASSERT_TRUE(reset.Get().has_value());
  EXPECT_EQ(false, content::EvalJs(agent, "validateEdit()"));
  EXPECT_EQ("TOOL_CANCELLED", content::EvalJs(agent, "beginEdit(true)"));
}

class DaoHomeExternalUnavailableSourceUIBrowserTest
    : public DaoHomeExternalUIBrowserTest,
      public testing::WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(DaoHomeSourceFailures,
                         DaoHomeExternalUnavailableSourceUIBrowserTest,
                         testing::Bool());

class DaoHomeExternalOcclusionUIBrowserTest
    : public DaoHomeExternalUIBrowserTest {
 protected:
  void SetUpInProcessBrowserTestFixture() override {
    DaoHomeExternalUIBrowserTest::SetUpInProcessBrowserTestFixture();
    // BrowserTestBase adds this after SetUpCommandLine().
    base::CommandLine::ForCurrentProcess()->RemoveSwitch(
        switches::kDisableBackgroundingOccludedWindowsForTesting);
  }
};

IN_PROC_BROWSER_TEST_F(DaoHomeExternalOcclusionUIBrowserTest,
                       OccludedOwnerKeepsInFlightSourceCollection) {
  ASSERT_FALSE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableBackgroundingOccludedWindowsForTesting));
  net::test_server::ControllableHttpResponse source_response(
      embedded_test_server(), "/slow-feed");
  ASSERT_TRUE(embedded_test_server()->Start());
  std::string manifest =
      R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[{"id":"feed","module":"connectors/feed.js","schema":"schemas/feed.json","permissions":{"origins":["FIXTURE_ORIGIN"],"paths":["/slow-feed"],"capabilities":["read_dom"],"mode":"read"}}]})";
  base::ReplaceSubstringsAfterOffset(
      &manifest, 0, "FIXTURE_ORIGIN",
      embedded_test_server()->GetURL("/").spec());
  HomeFileSnapshot files = {
      {"manifest.json", std::move(manifest)},
      {"index.html",
       "<!doctype html><html><body><main>Loading</main><script src='app.js' "
       "defer></script></body></html>"},
      {"app.js",
       "dao.sources.collect('feed').then(items => {"
       "document.querySelector('main').textContent = items[0].title;});"},
      {"connectors/feed.js",
       "export default {async collect(page) {"
       "return [{title: await page.getText('main')}];}};"},
      {"schemas/feed.json",
       R"({"type":"array","items":{"type":"object","properties":{"title":{"type":"string"}},"required":["title"]}})"}};
  ASSERT_NO_FATAL_FAILURE(OpenAndStage(std::move(files)));
  base::test::TestFuture<base::DictValue> preview_result;
  DaoHomeExternalService::Get()->ExecuteForTesting(
      "draft.preview",
      base::DictValue().Set("token", token_).Set("draft_id", draft_id_),
      preview_result.GetCallback());
  auto* project =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return project->GetPendingPermission().has_value() ||
           preview_result.IsReady();
  }));
  ASSERT_TRUE(project->GetPendingPermission());
  ASSERT_TRUE(
      project->ApprovePermission(project->GetPendingPermission()->id, {"feed"})
          .has_value());
  source_response.WaitForRequest();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  ASSERT_TRUE(ui->HasPendingAgentConnectorForTesting());
  preview->WasOccluded();
  EXPECT_TRUE(content::EvalJs(preview, R"(
    new Promise(resolve => {
      if (document.hidden) resolve(true);
      else document.addEventListener('visibilitychange',
          () => resolve(document.hidden), {once: true});
    })
  )")
                  .ExtractBool());
  ASSERT_EQ(content::Visibility::OCCLUDED, preview->GetVisibility());
  EXPECT_TRUE(ui->HasPendingAgentConnectorForTesting());
  source_response.Send(net::HTTP_OK, "text/html",
                       "<!doctype html><main>Occluded source content</main>");
  source_response.Done();
  const auto result = preview_result.Take();
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  EXPECT_TRUE(ui->IsExternalPreviewValid(draft_id_));
  ASSERT_TRUE(result.FindList("sources"));
  ASSERT_EQ(1u, result.FindList("sources")->size());
  const auto* status =
      result.FindList("sources")->front().GetDict().FindString("status");
  ASSERT_TRUE(status);
  EXPECT_EQ("passed", *status);
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalOcclusionUIBrowserTest,
                       OccludedRendererKeepsReceiptUntilNativeOwnerIsHidden) {
  ASSERT_FALSE(base::CommandLine::ForCurrentProcess()->HasSwitch(
      switches::kDisableBackgroundingOccludedWindowsForTesting));
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  const auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  auto publication_lease = ui->CreateMutationLease();
  ASSERT_TRUE(publication_lease->IsValid());
  ASSERT_TRUE(content::ExecJs(preview, R"(
    window.occlusionTestFrame = document.querySelector('dao-home-app')
        .shadowRoot.querySelector('[data-test="preview-frame"]');
  )"));

  preview->WasOccluded();
  ASSERT_EQ(content::Visibility::OCCLUDED, preview->GetVisibility());
  EXPECT_TRUE(content::EvalJs(preview, R"(
    (async () => {
      if (!document.hidden) {
        await new Promise(resolve => document.addEventListener(
            'visibilitychange', resolve, {once: true}));
      }
      // Wait behind the visibility handler's WebUI request, including its reply.
      await new Promise(resolve => {
        const original = window.cr.webUIListenerCallback;
        window.cr.webUIListenerCallback = (event, ...args) => {
          if (event === 'occlusion-test-barrier') {
            window.cr.webUIListenerCallback = original;
            resolve();
          } else original(event, ...args);
        };
        chrome.send('getHomeSessionActive', ['occlusion-test-barrier']);
      });
      const app = document.querySelector('dao-home-app');
      await app.updateComplete;
      return document.hidden && window.occlusionTestFrame ===
          app.shadowRoot.querySelector('[data-test="preview-frame"]');
    })()
  )")
                  .ExtractBool());
  EXPECT_TRUE(ui->IsExternalPreviewValid(draft_id_));
  EXPECT_TRUE(publication_lease->IsValid());

  auto* window =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(preview);
  ASSERT_TRUE(window);
  window->GetWindow()->Hide();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return preview->GetVisibility() == content::Visibility::HIDDEN; }));
  EXPECT_FALSE(ui->IsExternalPreviewValid(draft_id_));
  EXPECT_FALSE(publication_lease->IsValid());
  EXPECT_TRUE(content::EvalJs(preview, R"(
    new Promise(resolve => {
      const root = document.querySelector('dao-home-app').shadowRoot;
      const check = () => {
        if (root.querySelector('iframe')) return;
        observer.disconnect();
        resolve(document.hidden);
      };
      const observer = new MutationObserver(check);
      observer.observe(root, {childList: true, subtree: true});
      check();
    })
  )")
                  .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       SameDocumentPreviewNavigationKeepsReceipt) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  const auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  auto publication_lease = ui->CreateMutationLease();
  ASSERT_TRUE(publication_lease->IsValid());
  content::RenderFrameHost* frame = nullptr;
  preview->ForEachRenderFrameHost([&](content::RenderFrameHost* candidate) {
    if (candidate->GetLastCommittedURL().path().find(draft_id_) !=
        std::string::npos) {
      frame = candidate;
    }
  });
  ASSERT_TRUE(frame);
  for (const auto* script : {"location.hash = 'route-one';",
                             "history.pushState({}, '', '#route-two');",
                             "history.replaceState({}, '', '#route-three');"}) {
    content::TestNavigationObserver navigation(preview);
    navigation.set_wait_event(
        content::TestNavigationObserver::WaitEvent::kNavigationFinished);
    ASSERT_TRUE(content::ExecJs(frame, script));
    navigation.Wait();
    EXPECT_TRUE(ui->IsExternalPreviewValid(draft_id_)) << script;
    EXPECT_TRUE(publication_lease->IsValid()) << script;
  }

  content::TestNavigationObserver navigation(preview);
  navigation.set_wait_event(
      content::TestNavigationObserver::WaitEvent::kNavigationFinished);
  ASSERT_TRUE(content::ExecJs(frame, "location.reload();"));
  navigation.Wait();
  EXPECT_FALSE(ui->IsExternalPreviewValid(draft_id_));
  EXPECT_FALSE(publication_lease->IsValid());
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       SwitchingPreviewTabRevokesReceipt) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  const auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  auto publication_lease = ui->CreateMutationLease();
  ASSERT_TRUE(publication_lease->IsValid());
  auto* window =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(preview);
  ASSERT_TRUE(window);
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      window, GURL("about:blank"), WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  EXPECT_EQ(content::Visibility::HIDDEN, preview->GetVisibility());
  EXPECT_FALSE(ui->IsExternalPreviewValid(draft_id_));
  EXPECT_FALSE(publication_lease->IsValid());
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       RemovingPreviewFrameRevokesReceipt) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  const auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  auto publication_lease = ui->CreateMutationLease();
  ASSERT_TRUE(publication_lease->IsValid());
  ASSERT_TRUE(content::ExecJs(preview, R"(
    document.querySelector('dao-home-app').shadowRoot.querySelector(
        '[data-test="preview-frame"]').remove();
  )"));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !publication_lease->IsValid(); }));
  EXPECT_FALSE(ui->IsExternalPreviewValid(draft_id_));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       SettingsExplainsHomeSkillWithMcpDisabled) {
  g_browser_process->local_state()->SetBoolean(prefs::kDaoMcpServerEnabled,
                                               false);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL("dao://settings/localAgents")));
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  const auto help = content::EvalJs(contents, R"(
    new Promise((resolve, reject) => {
      const deadline = performance.now() + 10000;
      function find(root) {
        const help = root.querySelector('#daoHomeSkillHelp');
        if (help) return help;
        for (const element of root.querySelectorAll('*')) {
          if (element.shadowRoot) {
            const result = find(element.shadowRoot);
            if (result) return result;
          }
        }
        return null;
      }
      function tick() {
        const help = find(document);
        if (help) {
          help.closest('details').open = true;
          help.scrollIntoView();
          if (help.getBoundingClientRect().height > 0) {
            resolve(help.innerText);
            return;
          }
        }
        if (performance.now() > deadline) reject('Home help is not visible');
        else setTimeout(tick, 20);
      }
      tick();
    });
  )");
  ASSERT_TRUE(help.is_ok()) << help;
  const auto text = help.ExtractString();
  for (int message : {IDS_DAO_LOCAL_AGENT_HELP_HOME_TITLE,
                      IDS_DAO_LOCAL_AGENT_HELP_HOME_DESCRIPTION,
                      IDS_DAO_LOCAL_AGENT_HELP_HOME_SETUP,
                      IDS_DAO_LOCAL_AGENT_HELP_HOME_EXAMPLE}) {
    EXPECT_NE(std::string::npos, text.find(l10n_util::GetStringUTF8(message)));
  }
  EXPECT_NE(std::string::npos, text.find("$dao-home"));
  EXPECT_NE(std::string::npos, text.find("/dao-home"));
  EXPECT_FALSE(g_browser_process->local_state()->GetBoolean(
      prefs::kDaoMcpServerEnabled));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       CliPublishesThroughHomeSocketWhileMcpIsDisabled) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  g_browser_process->local_state()->SetBoolean(prefs::kDaoMcpServerEnabled,
                                               false);
  const auto runtime_path = base::PathService::CheckedGet(chrome::DIR_USER_DATA)
                                .AppendASCII("HomeCLI")
                                .AppendASCII("runtime.json");
  ASSERT_TRUE(
      base::test::RunUntil([&] { return base::PathExists(runtime_path); }));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* ordinary = browser()->tab_strip_model()->GetActiveWebContents();
  const std::string profile =
      browser()->GetProfile()->GetBaseName().AsUTF8Unsafe();
  const auto pulled = RunCli(
      "pull",
      {{"reason", "Exercise the native Home CLI"}, {"profile", profile}});
  if (const auto* path = pulled.FindString("workspace_dir")) {
    cli_workspace_dir_ = base::FilePath::FromUTF8Unsafe(*path);
  }
  ASSERT_EQ(true, pulled.FindBool("ok")) << pulled.DebugString();
  ASSERT_FALSE(cli_workspace_dir_.empty());
  ASSERT_TRUE(cli_workspace_dir_.IsAbsolute());
  ASSERT_TRUE(pulled.FindString("profile_id")) << pulled.DebugString();
  EXPECT_EQ(profile, *pulled.FindString("profile_id"));
  ASSERT_TRUE(pulled.FindString("project_dir")) << pulled.DebugString();
  const auto project_dir =
      base::FilePath::FromUTF8Unsafe(*pulled.FindString("project_dir"));
  ASSERT_EQ(cli_workspace_dir_.AppendASCII("project"), project_dir);
  ASSERT_TRUE(base::WriteFile(
      project_dir.AppendASCII("index.html"),
      "<!doctype html><html><body><main data-dao-node-id='main'>"
      "CLI transport fixture</main></body></html>"));

  const auto previewed = RunCli("preview");
  ASSERT_EQ(true, previewed.FindBool("ok")) << previewed.DebugString();
  ASSERT_EQ(true, previewed.FindBool("publishable")) << previewed.DebugString();
  ASSERT_TRUE(previewed.FindString("content_digest"))
      << previewed.DebugString();
  ASSERT_TRUE(previewed.FindString("draft_id")) << previewed.DebugString();
  ASSERT_TRUE(previewed.FindString("preview_window_id"))
      << previewed.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  EXPECT_NE(ordinary, preview);
  auto* window =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(preview);
  ASSERT_TRUE(window);
  EXPECT_NE(browser(), window);
  EXPECT_EQ(*previewed.FindString("preview_window_id"),
            base::NumberToString(window->GetSessionID().id()));
  EXPECT_TRUE(window->GetWindow()->IsVisible());
  auto preview_window = window->GetWeakPtr();
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  EXPECT_TRUE(ui->IsExternalPreviewValid(*previewed.FindString("draft_id")));
  content::RenderFrameHost* frame = nullptr;
  preview->ForEachRenderFrameHost([&](content::RenderFrameHost* candidate) {
    if (candidate->GetLastCommittedURL().path().find(
            *previewed.FindString("draft_id")) != std::string::npos) {
      frame = candidate;
    }
  });
  ASSERT_TRUE(frame);
  EXPECT_EQ(
      "CLI transport fixture",
      content::EvalJs(frame, "document.querySelector('main').textContent"));

  const auto screenshot = RunCli("screenshot");
  ASSERT_EQ(true, screenshot.FindBool("ok")) << screenshot.DebugString();
  ASSERT_TRUE(screenshot.FindString("artifact")) << screenshot.DebugString();
  const auto artifact =
      base::FilePath::FromUTF8Unsafe(*screenshot.FindString("artifact"));
  EXPECT_TRUE(cli_workspace_dir_.IsParent(artifact));
  std::string png;
  ASSERT_TRUE(base::ReadFileToString(artifact, &png));
  EXPECT_TRUE(png.starts_with(std::string_view("\x89PNG\r\n\x1a\n", 8)));

  const auto resumed = RunCli("resume", {{"reason", "Continue editing"}});
  ASSERT_EQ(true, resumed.FindBool("ok")) << resumed.DebugString();
  EXPECT_EQ(pulled.FindDouble("expires_at_ms"),
            resumed.FindDouble("expires_at_ms"));
  EXPECT_EQ(true, resumed.FindBool("preview_valid"));
  ASSERT_TRUE(resumed.FindString("preview_window_id"));
  EXPECT_EQ(*previewed.FindString("preview_window_id"),
            *resumed.FindString("preview_window_id"));
  EXPECT_TRUE(ui->IsExternalPreviewValid(*previewed.FindString("draft_id")));

  const auto published =
      RunCli("publish", {{"summary", "Publish the native CLI fixture"}});
  ASSERT_EQ(true, published.FindBool("ok")) << published.DebugString();
  ASSERT_TRUE(published.FindString("revision")) << published.DebugString();
  ASSERT_TRUE(published.FindString("digest")) << published.DebugString();
  const std::string revision = *published.FindString("revision");
  ASSERT_FALSE(revision.empty());
  EXPECT_EQ(*previewed.FindString("content_digest"),
            *published.FindString("digest"));
  const auto status = RunCli("status");
  ASSERT_EQ(true, status.FindBool("ok")) << status.DebugString();
  ASSERT_TRUE(status.FindString("current_revision")) << status.DebugString();
  EXPECT_EQ(revision, *status.FindString("current_revision"));
  const auto context = DaoHomeExternalService::Get()->GetContext(preview);
  ASSERT_TRUE(context.is_dict());
  EXPECT_EQ(true, context.GetDict().FindBool("published"));
  const std::string wait_for_project = content::JsReplace(R"(
    new Promise(resolve => {
      const tick = () => {
        const frame = document.querySelector('dao-home-app').shadowRoot
            .querySelector('[data-test="project-frame"]');
        if (frame?.src.includes('/' + $1 + '/')) resolve(true);
        else setTimeout(tick, 10);
      };
      tick();
    });
  )",
                                                          revision);
  EXPECT_TRUE(content::EvalJs(ordinary, wait_for_project).ExtractBool());
  EXPECT_TRUE(content::EvalJs(preview, wait_for_project).ExtractBool());

  // Model a lost publication reply while retaining the browser's real commit.
  const auto state_path =
      cli_workspace_dir_.AppendASCII(".dao-home/state.json");
  std::string state_json;
  ASSERT_TRUE(base::ReadFileToString(state_path, &state_json));
  auto state = base::JSONReader::ReadDict(state_json, base::JSON_PARSE_RFC);
  ASSERT_TRUE(state);
  ASSERT_TRUE(published.FindString("operation_id"));
  state->Set("pending_operation",
             base::DictValue()
                 .Set("operation_id", *published.FindString("operation_id"))
                 .Set("kind", "publish")
                 .Set("base_revision", *pulled.FindString("base_revision"))
                 .Set("draft_id", *previewed.FindString("draft_id"))
                 .Set("digest", *published.FindString("digest"))
                 .Set("summary", "Publish the native CLI fixture"));
  auto pending_json = base::WriteJson(*state);
  ASSERT_TRUE(pending_json);
  ASSERT_TRUE(base::WriteFile(state_path, *pending_json));
  const auto recovered = RunCli("resume", {{"reason", "Recover publication"}});
  ASSERT_EQ(true, recovered.FindBool("ok")) << recovered.DebugString();
  EXPECT_EQ(true, recovered.FindBool("recovered"));
  ASSERT_TRUE(recovered.FindString("revision"));
  EXPECT_EQ(revision, *recovered.FindString("revision"));
  const auto versions = RunCli("versions");
  ASSERT_TRUE(versions.FindList("versions")) << versions.DebugString();
  EXPECT_EQ(1u, versions.FindList("versions")->size());

  const auto generated = project_dir.AppendASCII("generated");
  ASSERT_TRUE(base::CreateDirectory(generated));
  for (int i = 0; i <= 2000; ++i)
    ASSERT_TRUE(base::WriteFile(generated.AppendASCII(std::to_string(i)), ""));
  const auto failed_cleanup = RunCli("clean", {{"discard", ""}}, false);
  ASSERT_TRUE(failed_cleanup.FindStringByDottedPath("error.code"));
  EXPECT_EQ("QUOTA_EXCEEDED",
            *failed_cleanup.FindStringByDottedPath("error.code"));
  EXPECT_TRUE(base::PathExists(state_path));
  EXPECT_EQ(true, RunCli("status").FindBool("ok"));
  ASSERT_TRUE(base::DeleteFile(generated.AppendASCII("2000")));

  const auto workspace = cli_workspace_dir_;
  const auto cleaned =
      RunCli("clean", {{"discard", ""}, {"close-session", ""}});
  ASSERT_EQ(true, cleaned.FindBool("ok")) << cleaned.DebugString();
  EXPECT_EQ(true, cleaned.FindBool("session_closed"));
  EXPECT_FALSE(base::PathExists(workspace));
  cli_workspace_dir_.clear();
  ASSERT_TRUE(base::test::RunUntil([&] { return !preview_window; }));
  EXPECT_EQ(ordinary, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_FALSE(g_browser_process->local_state()->GetBoolean(
      prefs::kDaoMcpServerEnabled));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       BusyProfileDoesNotPromptAndStopRevokesToken) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  int approvals = 0;
  DaoHomeExternalService::Get()->SetApprovalForTesting(base::BindRepeating(
      [](int* approvals, Profile*) {
        ++*approvals;
        return true;
      },
      &approvals));
  const auto busy =
      Call("session.open",
           base::DictValue()
               .Set("reason", "Duplicate editing request")
               .Set("profile",
                    browser()->GetProfile()->GetBaseName().AsUTF8Unsafe()));
  ASSERT_TRUE(busy.FindStringByDottedPath("error.code"));
  EXPECT_EQ("HOME_BUSY", *busy.FindStringByDottedPath("error.code"));
  EXPECT_EQ(0, approvals);
  DaoHomeExternalService::Get()->StopForProfile(browser()->GetProfile());
  const auto revoked = Call("session.status");
  ASSERT_TRUE(revoked.FindStringByDottedPath("error.code"));
  EXPECT_EQ("SESSION_REVOKED", *revoked.FindStringByDottedPath("error.code"));
  token_ = "missing-session";
  const auto missing = Call("session.status");
  ASSERT_TRUE(missing.FindStringByDottedPath("error.code"));
  EXPECT_EQ("SESSION_EXPIRED", *missing.FindStringByDottedPath("error.code"));
  token_.clear();
  DaoHomeExternalService::Get()->SetApprovalForTesting(
      base::BindRepeating([](Profile*) { return true; }));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       RejectsLongSummaryWithoutInvalidatingPreview) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  ASSERT_EQ(true,
            Call("draft.preview", base::DictValue().Set("draft_id", draft_id_))
                .FindBool("publishable"));
  const auto rejected =
      Call("draft.publish", base::DictValue()
                                .Set("draft_id", draft_id_)
                                .Set("base_revision", "")
                                .Set("digest", digest_)
                                .Set("operation_id", "long-summary")
                                .Set("summary", std::string(501, 'x')));
  ASSERT_TRUE(rejected.FindStringByDottedPath("error.code"));
  EXPECT_EQ("INVALID_ARGUMENT", *rejected.FindStringByDottedPath("error.code"));
  EXPECT_EQ(true, Call("session.status").FindBool("preview_valid"));
  const auto accepted =
      Call("draft.publish", base::DictValue()
                                .Set("draft_id", draft_id_)
                                .Set("base_revision", "")
                                .Set("digest", digest_)
                                .Set("operation_id", "bounded-summary")
                                .Set("summary", std::string(500, 'x')));
  EXPECT_EQ(true, accepted.FindBool("ok")) << accepted.DebugString();
}

IN_PROC_BROWSER_TEST_P(DaoHomeExternalUnavailableSourceUIBrowserTest,
                       UnchangedFailingSourceWarnsButChangedSourceBlocks) {
  base::ScopedAllowBlockingForTesting allow_blocking;
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](bool navigation_fails, const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/feed")
          return nullptr;
        if (navigation_fails)
          return std::make_unique<net::test_server::RawHttpResponse>("", "");
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        response->set_content_type("text/html");
        response->set_content("<main>Feed fixture</main>");
        return response;
      },
      GetParam()));
  ASSERT_TRUE(embedded_test_server()->Start());
  HomeFileSnapshot files = {
      {"manifest.json",
       R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[{"id":"feed","module":"connectors/feed.js","schema":"schemas/feed.json","permissions":{"origins":["https://fixture.test"],"paths":["/feed"],"capabilities":["read_dom"],"mode":"read"}}]})"},
      {"index.html",
       "<!doctype html><html><body><main>Home</main><script src='app.js' "
       "defer></script></body></html>"},
      {"app.js",
       "dao.sources.collect('feed').catch(() => "
       "{document.querySelector('main').textContent = 'Source "
       "unavailable';});"},
      {"connectors/feed.js",
       "export default {async collect() {return ['invalid'];}};"},
      {"schemas/feed.json", R"({"type":"array","items":{"type":"object"}})"}};
  base::ReplaceSubstringsAfterOffset(&files["manifest.json"], 0,
                                     "https://fixture.test",
                                     embedded_test_server()
                                         ->GetURL("/feed")
                                         .DeprecatedGetOriginAsURL()
                                         .spec());
  std::string revision;
  {
    DaoHomeProjectStore store(browser()->GetProfile()->GetPath());
    ASSERT_TRUE(store.Initialize().has_value());
    auto draft = store.StageSnapshot("", files, "Existing unavailable source");
    ASSERT_TRUE(draft.has_value());
    auto version = store.PublishWithGrant(draft->id, "feed",
                                          HomeRevisionKind::kInitial, nullptr);
    ASSERT_TRUE(version.has_value());
    revision = version->id;
  }
  const auto opened =
      Call("session.open", base::DictValue().Set("reason", "Edit layout"));
  ASSERT_TRUE(opened.FindString("token")) << opened.DebugString();
  token_ = *opened.FindString("token");
  for (const bool changed_source : {false, true}) {
    files["index.html"] += "\n";
    if (changed_source)
      files["connectors/feed.js"] += "\n";
    digest_ = HomeFileSnapshotDigest(files);
    const auto staged =
        Call("draft.stage", base::DictValue()
                                .Set("base_revision", revision)
                                .Set("files", EncodeHomeFileSnapshot(files))
                                .Set("digest", digest_));
    ASSERT_TRUE(staged.FindString("draft_id")) << staged.DebugString();
    draft_id_ = *staged.FindString("draft_id");
    const auto previewed =
        Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
    EXPECT_EQ(!changed_source, previewed.FindBool("publishable"))
        << previewed.DebugString();
    ASSERT_TRUE(previewed.FindStringByDottedPath("render.status"))
        << previewed.DebugString();
    EXPECT_EQ("passed", *previewed.FindStringByDottedPath("render.status"));
    const auto* sources = previewed.FindList("sources");
    ASSERT_TRUE(sources);
    ASSERT_EQ(1u, sources->size());
    EXPECT_EQ(!changed_source, (*sources)[0].GetDict().FindBool("warning"));
    EXPECT_EQ(changed_source, (*sources)[0].GetDict().FindBool("required"));
    const auto published =
        Call("draft.publish",
             base::DictValue()
                 .Set("draft_id", draft_id_)
                 .Set("base_revision", revision)
                 .Set("digest", digest_)
                 .Set("operation_id",
                      changed_source ? "changed-source" : "layout-only")
                 .Set("summary", "Update layout"));
    EXPECT_EQ(!changed_source, published.FindBool("ok"))
        << published.DebugString();
    if (!changed_source) {
      ASSERT_TRUE(published.FindString("revision"));
      revision = *published.FindString("revision");
    }
  }
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       KeepsPreviewInDedicatedWindowAfterValidation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* ordinary = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("ok")) << result.DebugString();
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  EXPECT_NE(ordinary, preview);
  EXPECT_EQ(ordinary->GetBrowserContext(), preview->GetBrowserContext());
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  EXPECT_TRUE(ui->IsExternalPreviewValid(draft_id_));
  EXPECT_TRUE(content::EvalJs(preview, R"(
    !!document.querySelector('dao-home-app').shadowRoot.querySelector(
      '[data-test="preview-frame"].retained')
  )")
                  .ExtractBool());
  EXPECT_FALSE(
      DaoHomeExternalService::Get()->CanAccessDraft(ordinary, draft_id_));
  auto* project =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> internal;
  project->StageSnapshot("", BasicProject(), "Internal draft",
                         internal.GetCallback());
  ASSERT_TRUE(internal.Get().has_value());
  EXPECT_FALSE(DaoHomeExternalService::Get()->CanAccessDraft(
      preview, internal.Get()->id));
  EXPECT_TRUE(DaoHomeExternalService::Get()->CanAccessDraft(
      ordinary, internal.Get()->id));
  EXPECT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  EXPECT_TRUE(ui->IsExternalPreviewValid(draft_id_));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       OrdinaryHomeStopsEditorWithoutReplacingProjectFrames) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* ordinary = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  const auto previewed =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, previewed.FindBool("publishable")) << previewed.DebugString();
  const auto published = Call(
      "draft.publish", base::DictValue()
                           .Set("draft_id", draft_id_)
                           .Set("base_revision", "")
                           .Set("digest", digest_)
                           .Set("operation_id", "editor-menu-fixture")
                           .Set("summary", "Set up the ordinary Home fixture"));
  ASSERT_EQ(true, published.FindBool("ok")) << published.DebugString();
  ASSERT_TRUE(published.FindString("revision")) << published.DebugString();
  const std::string revision = *published.FindString("revision");
  ASSERT_EQ(true, Call("session.close").FindBool("ok"));
  token_.clear();
  auto* manager = g_browser_process->profile_manager();
  Profile& other_profile = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  auto* private_profile =
      browser()->GetProfile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  browser()->GetWindow()->Show();
  browser()->GetWindow()->Activate();
  ui_test_utils::WaitForBrowserSetLastActive(browser());
  ASSERT_TRUE(content::ExecJs(ordinary, content::JsReplace(R"(
    (async () => {
      const state = window.editorMenuTest = {};
      state.root = document.querySelector('dao-home-app').shadowRoot;
      state.waitFor = predicate => new Promise((resolve, reject) => {
        const deadline = performance.now() + 10000;
        const tick = () => {
          if (predicate()) resolve(true);
          else if (performance.now() > deadline) reject(new Error('Editor UI timed out'));
          else setTimeout(tick, 10);
        };
        tick();
      });
      state.call = (method, ...args) => new Promise((resolve, reject) => {
        const original = window.cr.webUIListenerCallback;
        const timeout = setTimeout(() => {
          window.cr.webUIListenerCallback = original;
          reject(new Error(method + ' timed out'));
        }, 10000);
        window.cr.webUIListenerCallback = (event, ...values) => {
          if (event === 'editor-menu-test') {
            clearTimeout(timeout);
            window.cr.webUIListenerCallback = original;
            resolve(values[0]);
          } else original(event, ...values);
        };
        chrome.send(method, ['editor-menu-test', ...args]);
      });
      await state.waitFor(() =>
          state.root.querySelector('[data-test="project-frame"]')?.src.includes('/' + $1 + '/') &&
          state.root.querySelector('[data-test="connector-sandbox"]') &&
          !state.root.querySelector('[data-test="stop-external-editor"]'));
      state.project = state.root.querySelector('[data-test="project-frame"]');
      state.connector = state.root.querySelector('[data-test="connector-sandbox"]');
      state.unchanged = () =>
          state.project === state.root.querySelector('[data-test="project-frame"]') &&
          state.connector === state.root.querySelector('[data-test="connector-sandbox"]');
    })()
  )",
                                                           revision)));

  const auto opened =
      Call("session.open",
           base::DictValue()
               .Set("reason", "Edit the published Home")
               .Set("profile",
                    browser()->GetProfile()->GetBaseName().AsUTF8Unsafe()));
  ASSERT_EQ(true, opened.FindBool("ok")) << opened.DebugString();
  ASSERT_TRUE(opened.FindString("token")) << opened.DebugString();
  token_ = *opened.FindString("token");
  EXPECT_FALSE(PreviewContents());
  EXPECT_EQ(R"({"client":"Home test"})", content::EvalJs(ordinary, R"(
    (async () => {
      const state = window.editorMenuTest;
      await state.waitFor(() => state.root.querySelector('[data-test="stop-external-editor"]')
          ?.textContent.includes('Home test'));
      return JSON.stringify(await state.call('getHomeExternalEditor'));
    })()
  )"));
  EXPECT_TRUE(content::EvalJs(ordinary, "window.editorMenuTest.unchanged()")
                  .ExtractBool());
  EXPECT_EQ("null", content::EvalJs(ordinary, R"(
    window.editorMenuTest.call('getHomeExternalSession').then(JSON.stringify)
  )"));
  auto* external = DaoHomeExternalService::Get();
  EXPECT_TRUE(external->GetContext(ordinary).is_none());
  EXPECT_TRUE(external->GetEditor(&other_profile).is_none());
  EXPECT_TRUE(external->GetEditor(private_profile).is_none());
  external->StopForProfile(&other_profile);
  external->StopForProfile(private_profile);
  EXPECT_EQ(true, Call("session.status").FindBool("ok"));

  auto files = BasicProject();
  files["manifest.json"] =
      R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[{"id":"feed","module":"connectors/feed.js","schema":"schemas/feed.json","permissions":{"origins":["https://example.test"],"paths":["/feed"],"capabilities":["read_dom"],"mode":"read"}}]})";
  files["connectors/feed.js"] =
      "export default {async collect() {return [];}};";
  files["schemas/feed.json"] = R"({"type":"array","items":{"type":"string"}})";
  const auto staged =
      Call("draft.stage", base::DictValue()
                              .Set("base_revision", revision)
                              .Set("files", EncodeHomeFileSnapshot(files))
                              .Set("digest", HomeFileSnapshotDigest(files)));
  ASSERT_EQ(true, staged.FindBool("ok")) << staged.DebugString();
  ASSERT_TRUE(staged.FindString("draft_id")) << staged.DebugString();
  draft_id_ = *staged.FindString("draft_id");
  EXPECT_FALSE(external->CanAccessDraft(ordinary, draft_id_));
  auto* project =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> draft;
  project->GetDraft(draft_id_, draft.GetCallback());
  ASSERT_TRUE(draft.Get().has_value());
  const auto permission = project->RequestPermission(*draft.Get(), {"feed"});
  ASSERT_TRUE(permission.has_value());
  EXPECT_EQ(R"({"permission":null,"approval":"cancelled","cancelled":false})",
            content::EvalJs(ordinary, content::JsReplace(R"(
    (async () => {
      const call = window.editorMenuTest.call;
      const permission = await call('getHomePermission');
      const approval = await call('approveHomePermission', $1);
      const cancelled = await call('cancelHomePermission', $1);
      return JSON.stringify({permission, approval: approval.code, cancelled});
    })()
  )",
                                                         permission->id)));
  ASSERT_TRUE(project->GetPendingPermission());
  EXPECT_EQ(permission->id, project->GetPendingPermission()->id);
  EXPECT_FALSE(project->IsDraftConnectorApproved(draft_id_, "feed"));
  EXPECT_FALSE(content::EvalJs(ordinary, R"(
    !!window.editorMenuTest.root.querySelector('[data-test="permission-dialog"]')
  )")
                   .ExtractBool());

  content::RenderFrameHost* project_frame = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    project_frame = nullptr;
    ordinary->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
      const auto& url = frame->GetLastCommittedURL();
      if (frame->IsRenderFrameLive() && url.host() == "dao-home-app" &&
          url.path() == "/" + revision + "/index.html") {
        project_frame = frame;
      }
    });
    return project_frame != nullptr;
  })) << "No committed Home project frame for revision "
      << revision;
  EXPECT_TRUE(content::EvalJs(project_frame, content::JsReplace(R"(
    new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        removeEventListener('message', listener);
        reject(new Error('Sandbox stop response timed out'));
      }, 10000);
      const listener = event => {
        if (event.source !== parent || event.data?.requestId !== 'sandbox-stop-editor') return;
        clearTimeout(timeout);
        removeEventListener('message', listener);
        resolve(typeof event.data.error === 'string');
      };
      addEventListener('message', listener);
      parent.postMessage({daoHome: 1, requestId: 'sandbox-stop-editor',
        revision: $1, method: 'stopHomeExternalEditor', params: {}}, '*');
    })
  )",
                                                                revision))
                  .ExtractBool());
  EXPECT_EQ(true, Call("session.status").FindBool("ok"));

  ASSERT_TRUE(content::ExecJs(ordinary, R"(
    (async () => {
      const state = window.editorMenuTest;
      state.root.querySelector('[data-test="project-menu"]').open = true;
      state.root.querySelector('[data-test="stop-external-editor"]').click();
      await state.waitFor(() => !state.root.querySelector('[data-test="stop-external-editor"]'));
    })()
  )"));
  const auto stopped = Call("session.status");
  EXPECT_EQ(false, stopped.FindBool("ok"));
  ASSERT_TRUE(stopped.FindStringByDottedPath("error.code"));
  EXPECT_EQ("SESSION_REVOKED", *stopped.FindStringByDottedPath("error.code"));
  token_.clear();
  EXPECT_TRUE(content::EvalJs(ordinary, "window.editorMenuTest.unchanged()")
                  .ExtractBool());
  EXPECT_TRUE(external->GetEditor(browser()->GetProfile()).is_none());
  EXPECT_EQ(ordinary, browser()->tab_strip_model()->GetActiveWebContents());
  EXPECT_EQ(GURL("dao://home/"), ordinary->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       RejectsStaleExternalSessionReadinessGeneration) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  const auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  ASSERT_TRUE(ui->IsReadyForExternal());
  const int old_generation = content::EvalJs(preview, R"(
    (async () => {
      const state = window.externalReadyTest = {
        originalSend: chrome.send,
        send: chrome.send.bind(chrome),
        originalListener: window.cr.webUIListenerCallback,
        pending: new Map(),
        nextId: 0,
      };
      state.ready = new Promise(resolve => { state.resolveReady = resolve; });
      state.getSession = () => new Promise(resolve => {
        const id = 'external-ready-test-' + ++state.nextId;
        state.pending.set(id, resolve);
        state.send('getHomeExternalSession', [id]);
      });
      window.cr.webUIListenerCallback = (event, ...values) => {
        const resolve = state.pending.get(event);
        if (resolve) {
          state.pending.delete(event);
          resolve(values[0]);
        } else {
          state.originalListener(event, ...values);
        }
      };
      chrome.send = (method, args) => {
        if (method === 'homeExternalSessionReady') {
          state.readyArgs = args;
          state.resolveReady();
        } else {
          state.send(method, args);
        }
      };
      return (await state.getSession()).generation;
    })()
  )")
                                 .ExtractInt();

  ui->NotifyExternalSessionChanged();
  const int current_generation = content::EvalJs(preview, R"(
    (async () => {
      await window.externalReadyTest.ready;
      return window.externalReadyTest.readyArgs[0];
    })()
  )")
                                     .ExtractInt();
  ASSERT_GT(current_generation, old_generation);
  EXPECT_FALSE(ui->IsReadyForExternal());

  // The session response is a barrier after the readiness message on the same
  // WebUI channel, so the assertion observes the native handler's result.
  EXPECT_EQ(current_generation,
            content::EvalJs(preview, content::JsReplace(R"(
    (async () => {
      const state = window.externalReadyTest;
      state.send('homeExternalSessionReady', [$1]);
      return (await state.getSession()).generation;
    })()
  )",
                                                        old_generation)));
  EXPECT_FALSE(ui->IsReadyForExternal());
  EXPECT_EQ(current_generation, content::EvalJs(preview, R"(
    (async () => {
      const state = window.externalReadyTest;
      state.send('homeExternalSessionReady', state.readyArgs);
      const session = await state.getSession();
      chrome.send = state.originalSend;
      window.cr.webUIListenerCallback = state.originalListener;
      delete window.externalReadyTest;
      return session.generation;
    })()
  )"));
  EXPECT_TRUE(ui->IsReadyForExternal());
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       LateRuntimeErrorRevokesRetainedPreviewReceipt) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  ASSERT_TRUE(ui->IsExternalPreviewValid(draft_id_));
  auto publication_lease = ui->CreateMutationLease();
  ASSERT_TRUE(publication_lease);
  ASSERT_TRUE(publication_lease->IsValid());
  content::RenderFrameHost* frame = nullptr;
  preview->ForEachRenderFrameHost([&](content::RenderFrameHost* candidate) {
    if (candidate->GetLastCommittedURL().path().find(draft_id_) !=
        std::string::npos) {
      frame = candidate;
    }
  });
  ASSERT_TRUE(frame);
  ASSERT_TRUE(content::ExecJs(
      frame, "setTimeout(() => console.error('late fixture failure'), 100)"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !ui->IsExternalPreviewValid(draft_id_); }));
  auto* project =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  EXPECT_FALSE(project->IsDraftPreviewed(draft_id_));
  EXPECT_FALSE(publication_lease->IsValid());
}

IN_PROC_BROWSER_TEST_F(
    DaoHomeExternalUIBrowserTest,
    PublishesUnsummarizedStageAndRecoversCommittedOperation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* ordinary = browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  auto previewed =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, previewed.FindBool("publishable")) << previewed.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  const std::string operation_id = "publish-home-fixture";
  base::DictValue publish_request = base::DictValue()
                                        .Set("draft_id", draft_id_)
                                        .Set("base_revision", "")
                                        .Set("digest", digest_)
                                        .Set("operation_id", operation_id)
                                        .Set("summary", "Publish fixture Home");
  const auto published = Call("draft.publish", publish_request.Clone());
  ASSERT_EQ(true, published.FindBool("ok")) << published.DebugString();
  ASSERT_TRUE(published.FindString("revision")) << published.DebugString();
  const std::string revision = *published.FindString("revision");
  EXPECT_FALSE(revision.empty());
  ASSERT_TRUE(published.FindString("digest")) << published.DebugString();
  EXPECT_EQ(digest_, *published.FindString("digest"));
  const auto recovered = Call(
      "session.status", base::DictValue().Set("operation_id", operation_id));
  ASSERT_EQ(true, recovered.FindBool("ok")) << recovered.DebugString();
  ASSERT_TRUE(recovered.FindString("current_revision"))
      << recovered.DebugString();
  EXPECT_EQ(revision, *recovered.FindString("current_revision"));
  const auto* operation = recovered.FindDict("operation");
  ASSERT_TRUE(operation);
  ASSERT_TRUE(operation->FindString("status"));
  ASSERT_TRUE(operation->FindString("operation_id"));
  ASSERT_TRUE(operation->FindString("revision"));
  ASSERT_TRUE(operation->FindString("digest"));
  EXPECT_EQ("committed", *operation->FindString("status"));
  EXPECT_EQ(operation_id, *operation->FindString("operation_id"));
  EXPECT_EQ(revision, *operation->FindString("revision"));
  EXPECT_EQ(digest_, *operation->FindString("digest"));
  const auto repeated = Call("draft.publish", std::move(publish_request));
  ASSERT_EQ(true, repeated.FindBool("ok")) << repeated.DebugString();
  ASSERT_TRUE(repeated.FindString("revision")) << repeated.DebugString();
  ASSERT_TRUE(repeated.FindString("digest")) << repeated.DebugString();
  EXPECT_EQ(revision, *repeated.FindString("revision"));
  EXPECT_EQ(digest_, *repeated.FindString("digest"));
  const auto versions = Call("project.versions");
  ASSERT_TRUE(versions.FindList("versions"));
  EXPECT_EQ(1u, versions.FindList("versions")->size());
  const auto context = DaoHomeExternalService::Get()->GetContext(preview);
  ASSERT_TRUE(context.is_dict());
  EXPECT_EQ(true, context.GetDict().FindBool("published"));
  const std::string wait_for_project = content::JsReplace(R"(
    new Promise(resolve => {
      const tick = () => {
        const frame = document.querySelector('dao-home-app').shadowRoot
            .querySelector('[data-test="project-frame"]');
        if (frame?.src.includes('/' + $1 + '/')) resolve(true);
        else setTimeout(tick, 10);
      };
      tick();
    });
  )",
                                                          revision);
  EXPECT_TRUE(content::EvalJs(ordinary, wait_for_project).ExtractBool());
  EXPECT_TRUE(content::EvalJs(preview, wait_for_project).ExtractBool());
  EXPECT_TRUE(
      content::EvalJs(preview,
                      content::JsReplace(R"(
    document.querySelector('dao-home-app').shadowRoot.querySelector(
      '[data-test="external-preview-status"]').textContent.includes($1)
  )",
                                         l10n_util::GetStringUTF8(
                                             IDS_DAO_HOME_EXTERNAL_PUBLISHED)))
          .ExtractBool());
  EXPECT_FALSE(content::EvalJs(preview, R"(
    !!document.querySelector('dao-home-app').shadowRoot.querySelector(
      '[data-test="preview-frame"]')
  )")
                   .ExtractBool());
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       CanPreviewAgainAfterWindowIsHiddenAndShown) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  auto first =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, first.FindBool("publishable")) << first.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* window =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(preview);
  ASSERT_TRUE(window);
  auto* ui = preview->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui);
  window->GetWindow()->Hide();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return preview->GetVisibility() == content::Visibility::HIDDEN; }));
  EXPECT_FALSE(ui->IsExternalPreviewValid(draft_id_));
  window->GetWindow()->Show();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return preview->GetVisibility() != content::Visibility::HIDDEN; }));
  const auto repeated =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, repeated.FindBool("ok")) << repeated.DebugString();
  EXPECT_EQ(true, repeated.FindBool("publishable"));
  ASSERT_TRUE(first.FindString("preview_window_id")) << first.DebugString();
  ASSERT_TRUE(repeated.FindString("preview_window_id"))
      << repeated.DebugString();
  EXPECT_EQ(*first.FindString("preview_window_id"),
            *repeated.FindString("preview_window_id"));
  EXPECT_EQ(preview, PreviewContents());
  EXPECT_TRUE(ui->IsExternalPreviewValid(draft_id_));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       SessionCloseDoesNotCloseWindowReceivingMovedPreviewTab) {
  ASSERT_NO_FATAL_FAILURE(OpenAndStage());
  auto result =
      Call("draft.preview", base::DictValue().Set("draft_id", draft_id_));
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  auto* source =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(preview);
  ASSERT_TRUE(source);
  auto* source_tabs = TabListInterface::From(source);
  ASSERT_TRUE(source_tabs);
  auto* moving_tab = source_tabs->GetActiveTab();
  ASSERT_TRUE(moving_tab);
  ASSERT_EQ(preview, moving_tab->GetContents());
  auto* destination = CreateBrowser(browser()->GetProfile());
  auto destination_weak = destination->GetWeakPtr();
  auto* destination_tabs = TabListInterface::From(destination);
  ASSERT_TRUE(destination_tabs);
  auto* original_tab = destination_tabs->GetActiveTab();
  ASSERT_TRUE(original_tab);
  const auto original_handle = original_tab->GetHandle();
  source_tabs->MoveTabToWindow(moving_tab->GetHandle(),
                               destination->GetSessionID(), 0);
  ASSERT_EQ(
      destination,
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(preview));
  destination_tabs->CloseTab(original_handle);
  ASSERT_EQ(1, destination_tabs->GetTabCount());
  EXPECT_FALSE(
      DaoHomeExternalService::Get()->CanAccessDraft(preview, draft_id_));
  const auto closed = Call("session.close");
  token_.clear();
  ASSERT_EQ(true, closed.FindBool("ok")) << closed.DebugString();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(destination_weak);
  EXPECT_FALSE(destination_weak->IsDeleteScheduled());
  EXPECT_TRUE(destination_weak->GetWindow()->IsVisible());
  EXPECT_EQ(preview,
            destination_weak->GetTabStripModel()->GetActiveWebContents());
  EXPECT_TRUE(ui_test_utils::NavigateToURL(destination_weak.get(),
                                           GURL("about:blank")));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       CachedSequentialSourceSamplesKeepFirstMediaAvailable) {
  auto visits = std::make_shared<std::array<std::atomic<int>, 2>>();
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](std::shared_ptr<std::array<std::atomic<int>, 2>> visits,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(net::HTTP_OK);
        if (request.relative_url == "/first") {
          ++(*visits)[0];
          response->set_content_type("text/html");
          response->set_content("<!doctype html><img src='/pixel.svg'>");
        } else if (request.relative_url == "/second") {
          ++(*visits)[1];
          response->set_content_type("text/html");
          response->set_content("<!doctype html><main>Second fixture</main>");
        } else if (request.relative_url == "/pixel.svg") {
          response->set_content_type("image/svg+xml");
          response->set_content(
              "<svg xmlns='http://www.w3.org/2000/svg' width='2' height='2'>"
              "<rect width='2' height='2' fill='#4678be'/></svg>");
        } else {
          return nullptr;
        }
        return response;
      },
      visits));
  ASSERT_TRUE(embedded_test_server()->Start());
  std::string manifest =
      R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[{"id":"first","module":"connectors/first.js","schema":"schemas/first.json","permissions":{"origins":["FIXTURE_ORIGIN"],"paths":["/first"],"capabilities":["read_dom"],"mode":"read"}},{"id":"second","module":"connectors/second.js","schema":"schemas/second.json","permissions":{"origins":["FIXTURE_ORIGIN"],"paths":["/second"],"capabilities":["read_dom"],"mode":"read"}}],"limits":{"max_result_bytes":1048576,"max_items_per_connector":100}})";
  base::ReplaceSubstringsAfterOffset(
      &manifest, 0, "FIXTURE_ORIGIN",
      embedded_test_server()->GetURL("/").spec());
  HomeFileSnapshot files = {
      {"manifest.json", std::move(manifest)},
      {"index.html",
       "<!doctype html><html><body><main>Loading</main><script "
       "src='app.js' defer></script></body></html>"},
      {"app.js", R"((async () => {
        const first = await dao.sources.collect('first');
        await dao.sources.collect('second');
        const image = document.createElement('img');
        image.id = 'resolved-media';
        image.onload = () => { document.querySelector('main').textContent = 'Media loaded'; };
        image.src = await dao.media.resolve(first[0].image);
        document.body.append(image);
      })();)"},
      {"connectors/first.js",
       "export default {async collect(page) { return [{image: await "
       "page.getAttribute('img', 'src')}]; }};"},
      {"connectors/second.js",
       "export default {async collect(page) { return await "
       "page.getText('main'); }};"},
      {"schemas/first.json",
       R"({"type":"array","items":{"type":"object","properties":{"image":{"type":"string"}},"required":["image"]}})"},
      {"schemas/second.json", R"({"type":"string"})"},
  };
  ASSERT_NO_FATAL_FAILURE(OpenAndStage(std::move(files)));
  base::test::TestFuture<base::DictValue> preview_result;
  DaoHomeExternalService::Get()->ExecuteForTesting(
      "draft.preview",
      base::DictValue().Set("token", token_).Set("draft_id", draft_id_),
      preview_result.GetCallback());
  auto* project =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return project->GetPendingPermission().has_value() ||
           preview_result.IsReady();
  }));
  const auto request = project->GetPendingPermission();
  ASSERT_TRUE(request);
  ASSERT_EQ(2u, request->items.size());
  EXPECT_EQ("first", request->items[0].connector_id);
  EXPECT_EQ("second", request->items[1].connector_id);
  EXPECT_FALSE(project->ApprovePermission(request->id, {"first", "unknown"})
                   .has_value());
  EXPECT_FALSE(project->IsDraftConnectorApproved(draft_id_, "first"));
  ASSERT_TRUE(
      project->ApprovePermission(request->id, {"first", "second"}).has_value());
  const auto result = preview_result.Take();
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  content::RenderFrameHost* frame = nullptr;
  preview->ForEachRenderFrameHost([&](content::RenderFrameHost* candidate) {
    if (candidate->GetLastCommittedURL().path().find(draft_id_) !=
        std::string::npos) {
      frame = candidate;
    }
  });
  ASSERT_TRUE(frame);
  EXPECT_TRUE(content::EvalJs(frame, R"(
    new Promise(resolve => {
      const tick = () => {
        const image = document.querySelector('#resolved-media');
        if (image?.complete && image.naturalWidth > 0) resolve(true);
        else setTimeout(tick, 10);
      };
      tick();
    });
  )")
                  .ExtractBool());
  EXPECT_EQ(1, (*visits)[0].load());
  EXPECT_EQ(1, (*visits)[1].load());
  EXPECT_TRUE(preview->GetWebUI()
                  ->GetController()
                  ->GetAs<DaoHomeUI>()
                  ->IsExternalPreviewValid(draft_id_));
  const std::string handle =
      content::EvalJs(frame,
                      "dao.sources.collect('first').then(v => v[0].image)")
          .ExtractString();
  ASSERT_TRUE(
      content::EvalJs(
          frame, content::JsReplace("dao.media.resolve($1).then(url => url === "
                                    "document.querySelector('img').src)",
                                    handle))
          .ExtractBool());
  base::test::TestFuture<void> cleared;
  browser()->GetProfile()->GetDefaultStoragePartition()->ClearDataForOrigin(
      content::StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE,
      embedded_test_server()->GetURL("/"), cleared.GetCallback());
  ASSERT_TRUE(cleared.Wait());
  EXPECT_EQ("not_found",
            content::EvalJs(
                frame,
                content::JsReplace(
                    "dao.media.resolve($1).then(() => 'resolved', e => e.code)",
                    handle)));
  const auto new_handle =
      content::EvalJs(frame,
                      "dao.sources.collect('first').then(v => v[0].image)")
          .ExtractString();
  auto* ordinary = browser()->tab_strip_model()->GetActiveWebContents();
  const auto resolve = content::JsReplace(
      "dao.media.resolve($1).then(() => 'resolved', e => e.code)", new_handle);
  ASSERT_TRUE(content::NavigateToURL(
      ordinary, embedded_test_server()->GetURL("localhost", "/second")));
  ASSERT_TRUE(
      content::ExecJs(ordinary, "localStorage.setItem('account', 'other')"));
  EXPECT_EQ("resolved", content::EvalJs(frame, resolve));
  ASSERT_TRUE(content::NavigateToURL(
      ordinary, embedded_test_server()->GetURL("/second")));
  ASSERT_TRUE(
      content::ExecJs(ordinary, "localStorage.setItem('account', 'new')"));
  EXPECT_EQ("not_found", content::EvalJs(frame, resolve));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       EarlyReadinessRejectsLateLoginAndScopeChanges) {
  net::test_server::ControllableHttpResponse login(embedded_test_server(),
                                                   "/login-signal");
  net::test_server::ControllableHttpResponse installed(embedded_test_server(),
                                                       "/installed");
  net::test_server::ControllableHttpResponse redirect(embedded_test_server(),
                                                      "/redirect-signal");
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content_type("text/html");
        if (request.relative_url == "/hydrate") {
          response->set_content(
              "<main></main><script>setTimeout(() => "
              "document.querySelector('main').id='ready', 5600)</script>");
        } else if (request.relative_url == "/form") {
          response->set_content(
              "<main>Ready</main><script>fetch('/login-signal').then(() => {"
              "document.body.innerHTML='<input type=password>'; "
              "fetch('/installed');});</script>");
        } else if (request.relative_url == "/move") {
          response->set_content(
              "<main>Ready</main><script>fetch('/redirect-signal').then(() => "
              "history.pushState({}, '', '/outside'));</script>");
        } else if (request.relative_url == "/401" ||
                   request.relative_url == "/403") {
          response->set_code(request.relative_url == "/401"
                                 ? net::HTTP_UNAUTHORIZED
                                 : net::HTTP_FORBIDDEN);
          response->set_content("Sign in");
        } else {
          return nullptr;
        }
        return response;
      }));
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* owner = browser()->tab_strip_model()->GetActiveWebContents();
  DaoHomeConnectorExecutor executor;
  auto start = [&](const std::string& path) {
    HomeConnector connector;
    connector.id = "fixture";
    connector.permissions.origins.push_back(
        url::Origin::Create(embedded_test_server()->GetURL(path)));
    connector.permissions.paths.push_back(path);
    connector.permissions.capabilities.insert(HomePageCapability::kReadDom);
    base::test::TestFuture<base::Value> ready;
    executor.Start(owner, browser()->GetProfile(), "revision",
                   std::move(connector), HomeLimits(), "export default {}",
                   R"({"type":"string"})", base::Value(base::DictValue()),
                   ready.GetCallback());
    return ready.Take().TakeDict();
  };
  for (const char* path : {"/401", "/403"}) {
    auto response = start(path);
    ASSERT_TRUE(response.FindString("code"));
    EXPECT_EQ("auth_required", *response.FindString("code"));
  }
  auto hydrated = start("/hydrate");
  ASSERT_TRUE(hydrated.FindString("execution_id"));
  base::test::TestFuture<base::Value> waited;
  executor.CallPage(*hydrated.FindString("execution_id"), "waitFor",
                    base::ListValue().Append("#ready").Append(9000),
                    waited.GetCallback());
  EXPECT_EQ(base::Value(true), waited.Get());
  auto started = start("/form");
  ASSERT_TRUE(started.FindString("execution_id")) << started.DebugString();
  login.WaitForRequest();
  login.Send(net::HTTP_OK, "text/plain", "ok");
  login.Done();
  installed.WaitForRequest();
  installed.Send(net::HTTP_OK, "text/plain", "ok");
  installed.Done();
  base::test::TestFuture<base::Value> finished;
  executor.Finish(*started.FindString("execution_id"), base::Value("stale"),
                  finished.GetCallback());
  ASSERT_EQ("auth_required", *finished.Get().GetDict().FindString("code"));

  started = start("/move");
  ASSERT_TRUE(started.FindString("execution_id"));
  redirect.WaitForRequest();
  redirect.Send(net::HTTP_OK, "text/plain", "ok");
  redirect.Done();
  ASSERT_TRUE(base::test::RunUntil([&] { return !executor.running(); }));
  base::test::TestFuture<base::Value> outside;
  executor.Finish(*started.FindString("execution_id"), base::Value("stale"),
                  outside.GetCallback());
  EXPECT_EQ("site_changed", *outside.Get().GetDict().FindString("code"));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       ParallelCardsReuseOnlyAuthorizedProfileResults) {
  static constexpr std::array kSourceIds{"first", "second", "third", "fourth",
                                         "fifth", "sixth", "seventh", "eighth",
                                         "ninth"};
  auto hits = std::make_shared<std::atomic_int>(0);
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](std::shared_ptr<std::atomic_int> hits,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url == "/never.png") {
          return std::make_unique<net::test_server::HungResponse>();
        }
        if (request.relative_url == "/icon.svg") {
          auto icon = std::make_unique<net::test_server::BasicHttpResponse>();
          icon->set_content_type("image/svg+xml");
          icon->set_content(
              "<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/>");
          return icon;
        }
        if (!std::ranges::contains(
                kSourceIds, std::string_view(request.relative_url).substr(1))) {
          return nullptr;
        }
        ++*hits;
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_content_type("text/html");
        response->AddCustomHeader("Cache-Control", "no-store");
        response->set_content("<main>" + request.relative_url +
                              "</main><section><div>A</div><div>B</div></"
                              "section><img id=icon src=/icon.svg>" +
                              (request.relative_url == "/first"
                                   ? "<img src='/never.png'>"
                                   : ""));
        return response;
      },
      hits));
  ASSERT_TRUE(embedded_test_server()->Start());
  auto files = BasicProject();
  auto manifest = base::JSONReader::ReadDict(files.at("manifest.json"),
                                             base::JSON_PARSE_RFC);
  ASSERT_TRUE(manifest);
  base::ListValue ids;
  base::ListValue expected_paths;
  for (const std::string id : kSourceIds) {
    ids.Append(id);
    expected_paths.Append("/" + id);
    const auto module = "connectors/" + id + ".js";
    manifest->FindList("connectors")
        ->Append(
            base::DictValue()
                .Set("id", id)
                .Set("module", module)
                .Set("schema", "schemas/value.json")
                .Set("permissions",
                     base::DictValue()
                         .Set("origins",
                              base::ListValue().Append(
                                  embedded_test_server()->GetURL("/").spec()))
                         .Set("paths", base::ListValue().Append("/" + id))
                         .Set("capabilities",
                              base::ListValue().Append("read_dom"))
                         .Set("mode", "read")));
    files[module] =
        "export default {async collect(page, input) {"
        "if (input.layout) return (await page.queryAll('section', "
        "{lines:'$innerText'}))[0].lines;"
        "await page.getAttribute('img:last-of-type', 'src');"
        "return input.media ? await page.getAttribute('#icon', 'src') : await "
        "page.getText('main');}}";
  }
  files["manifest.json"] = *base::WriteJson(*manifest);
  files["schemas/value.json"] = R"({"type":"string"})";
  auto* service =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  base::test::TestFuture<base::expected<HomeDraft, HomeError>> staged;
  service->StageSnapshot("", files, "Parallel cards", staged.GetCallback());
  ASSERT_TRUE(staged.Get().has_value());
  base::test::TestFuture<base::expected<HomeVersion, HomeError>> published;
  service->Publish(staged.Get()->id, HomeRevisionKind::kInitial,
                   published.GetCallback(), nullptr);
  ASSERT_TRUE(published.Get().has_value());
  for (const std::string id : kSourceIds) {
    base::test::TestFuture<base::expected<void, HomeError>> grant;
    service->GrantConnector(id, grant.GetCallback());
    ASSERT_TRUE(grant.Get().has_value());
  }
  const GURL home("dao://home/");
  const GURL app("chrome-untrusted://dao-home-app/" + published.Get()->id +
                 "/index.html?route=%2F");
  auto open = [&]() -> content::RenderFrameHost* {
    content::TestNavigationObserver loaded(app);
    loaded.StartWatchingNewWebContents();
    if (!AddTabAtIndex(-1, home, ui::PAGE_TRANSITION_TYPED)) {
      return nullptr;
    }
    loaded.Wait();
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    content::RenderFrameHost* project = nullptr;
    contents->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
      if (frame->GetLastCommittedURL() == app) {
        project = frame;
      }
    });
    return project;
  };
  const std::string read_cards = content::JsReplace(
      "Promise.all($1.map(id => dao.sources.collect(id)))", std::move(ids));
  auto* first = open();
  ASSERT_TRUE(first);
  auto start = base::TimeTicks::Now();
  EXPECT_EQ(expected_paths, content::EvalJs(first, read_cards));
  base::AddTagToTestResult(
      "cold_collect_ms",
      base::NumberToString((base::TimeTicks::Now() - start).InMilliseconds()));
  EXPECT_EQ(9, hits->load());
  EXPECT_EQ(true, content::EvalJs(first, R"(
    Promise.all(['first','second'].map(async id => {
      const handle = await dao.sources.collect(id, {media:true});
      return handle.startsWith('dao-media:') && (await dao.media.resolve(handle)).startsWith('blob:');
    })).then(v => v.every(Boolean))
  )"));
  EXPECT_EQ(11, hits->load());
  auto* second = open();
  ASSERT_TRUE(second);
  start = base::TimeTicks::Now();
  EXPECT_EQ(expected_paths, content::EvalJs(second, read_cards));
  base::AddTagToTestResult(
      "warm_collect_ms",
      base::NumberToString((base::TimeTicks::Now() - start).InMilliseconds()));
  EXPECT_EQ(11, hits->load());
  EXPECT_EQ("/first",
            content::EvalJs(second,
                            "dao.sources.collect('first', {}, {fresh:true})"));
  EXPECT_EQ(12, hits->load());
  EXPECT_EQ("A\nB", content::EvalJs(
                        second, "dao.sources.collect('first', {layout:true})"));

  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  constexpr char kStartEight[] = R"(
    (async () => {
      const bridge = await import('./home_bridge.js');
      const {revision, connectors} = await bridge.getHomeSnapshot();
      const start = ({id}) => bridge.startHomeConnector(revision, id, {}, true);
      const active = await Promise.all(connectors.slice(0, 8).map(start));
      const overflow = await start(connectors[8]);
      return active.every(result => !!result.execution_id) &&
          overflow.code === 'temporarily_unavailable';
    })()
  )";
  ASSERT_EQ(true, content::EvalJs(contents, kStartEight));
  auto* ui = contents->GetWebUI()->GetController()->GetAs<DaoHomeUI>();
  ASSERT_TRUE(ui->HasActiveRuntimeConnectorForTesting());
  ASSERT_TRUE(
      AddTabAtIndex(-1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  EXPECT_FALSE(ui->HasActiveRuntimeConnectorForTesting());
  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(contents));
  ASSERT_EQ(true, content::EvalJs(contents, kStartEight));
  base::test::TestFuture<base::expected<void, HomeError>> revoked;
  service->RevokeConnector("first", revoked.GetCallback());
  ASSERT_TRUE(revoked.Get().has_value());
  EXPECT_FALSE(ui->HasActiveRuntimeConnectorForTesting());
  EXPECT_EQ("permission_required", content::EvalJs(contents, R"(
    (async () => {
      const bridge = await import('./home_bridge.js');
      return (await bridge.startHomeConnector((await bridge.getHomeSnapshot()).revision, 'first', {})).code;
    })()
  )"));
}

IN_PROC_BROWSER_TEST_F(DaoHomeExternalUIBrowserTest,
                       CollectsSignedInProfileSourceWhileMcpIsDisabled) {
  g_browser_process->local_state()->SetBoolean(prefs::kDaoMcpServerEnabled,
                                               false);
  auto hits = std::make_shared<std::atomic_int>(0);
  auto hold_next = std::make_shared<std::atomic_bool>(false);
  auto status =
      std::make_shared<std::atomic<net::HttpStatusCode>>(net::HTTP_OK);
  embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
      [](std::shared_ptr<std::atomic_int> hits,
         std::shared_ptr<std::atomic_bool> hold_next,
         std::shared_ptr<std::atomic<net::HttpStatusCode>> status,
         const net::test_server::HttpRequest& request)
          -> std::unique_ptr<net::test_server::HttpResponse> {
        if (request.relative_url != "/private-feed") {
          return nullptr;
        }
        ++*hits;
        if (hold_next->exchange(false)) {
          return nullptr;
        }
        const auto cookie = request.headers.find("Cookie");
        const std::string cookies =
            cookie == request.headers.end() ? "" : cookie->second;
        auto response = std::make_unique<net::test_server::BasicHttpResponse>();
        response->set_code(status->load());
        response->AddCustomHeader("Cache-Control", "no-store");
        response->set_content_type("text/html");
        if (cookies.contains("bootstrap=1") &&
            !cookies.contains("site_session=ready")) {
          response->AddCustomHeader("Set-Cookie",
                                    "site_session=ready; Path=/; HttpOnly");
        }
        if (status->load() == net::HTTP_OK &&
            cookies.contains("home_fixture=private-cookie-value")) {
          response->set_content(
              "<main data-signed-in>Signed-in fixture content</main>");
        } else if (status->load() == net::HTTP_OK &&
                   cookies.contains("home_fixture=second-account")) {
          response->set_content("<main data-signed-in>Second account</main>");
        } else if (status->load() == net::HTTP_OK) {
          response->set_content(
              "<main "
              "data-signed-in></"
              "main><script>document.querySelector('main').textContent = "
              "localStorage.getItem('account') || 'Guest';</script>");
        } else {
          response->set_content("<main>Source unavailable</main>");
        }
        return response;
      },
      hits, hold_next, status));
  net::test_server::ControllableHttpResponse slow(embedded_test_server(),
                                                  "/private-feed");
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL fixture = embedded_test_server()->GetURL("/private-feed");
  ASSERT_TRUE(
      content::SetCookie(browser()->GetProfile(), fixture,
                         "home_fixture=private-cookie-value; Max-Age=600; "
                         "Path=/; HttpOnly; SameSite=Lax"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("dao://home/")));
  auto* ordinary = browser()->tab_strip_model()->GetActiveWebContents();
  std::string manifest =
      R"({"format_version":1,"entry":"index.html","routes":["/"],"connectors":[{"id":"feed","module":"connectors/feed.js","schema":"schemas/feed.json","permissions":{"origins":["FIXTURE_ORIGIN"],"paths":["/private-feed"],"capabilities":["read_dom"],"mode":"read"}}],"limits":{"max_result_bytes":1048576,"max_items_per_connector":100}})";
  base::ReplaceSubstringsAfterOffset(&manifest, 0, "FIXTURE_ORIGIN",
                                     fixture.DeprecatedGetOriginAsURL().spec());
  HomeFileSnapshot files = {
      {"manifest.json", std::move(manifest)},
      {"index.html",
       "<!doctype html><html><body><main id='feed'>Loading</main><script "
       "src='app.js' defer></script></body></html>"},
      {"app.js",
       "dao.sources.collect('feed').then(items => { "
       "document.getElementById('feed').textContent = items[0].title; });"},
      {"connectors/feed.js",
       "export default {async collect(page) { if (!await "
       "page.exists('[data-signed-in]')) throw new Error('Fixture is not "
       "signed in'); return [{title: await page.getText('[data-signed-in]')}]; "
       "}};"},
      {"schemas/feed.json",
       R"({"type":"array","items":{"type":"object","properties":{"title":{"type":"string"}},"required":["title"]}})"},
  };
  ASSERT_NO_FATAL_FAILURE(OpenAndStage(std::move(files)));
  base::test::TestFuture<base::DictValue> preview_result;
  DaoHomeExternalService::Get()->ExecuteForTesting(
      "draft.preview",
      base::DictValue().Set("token", token_).Set("draft_id", draft_id_),
      preview_result.GetCallback());
  auto* project =
      DaoHomeProjectServiceFactory::GetForProfile(browser()->GetProfile());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return project->GetPendingPermission().has_value() ||
           preview_result.IsReady();
  }));
  ASSERT_TRUE(project->GetPendingPermission())
      << (preview_result.IsReady() ? preview_result.Get().DebugString()
                                   : "Preview is still pending");
  const std::string permission_id = project->GetPendingPermission()->id;
  auto* preview = PreviewContents();
  ASSERT_TRUE(preview);
  EXPECT_FALSE(content::EvalJs(ordinary, R"(
    !!document.querySelector('dao-home-app').shadowRoot.querySelector(
      '[data-test="permission-dialog"]')
  )")
                   .ExtractBool());
  EXPECT_EQ(R"({"permission":null,"approval":"cancelled","cancelled":false})",
            content::EvalJs(ordinary, content::JsReplace(R"(
        (async () => {
          const call = (method, ...args) => new Promise(resolve => {
            const original = window.cr.webUIListenerCallback;
            window.cr.webUIListenerCallback = (event, ...values) => {
              if (event === 'external-isolation-test') {
                window.cr.webUIListenerCallback = original;
                resolve(values[0]);
              } else original(event, ...values);
            };
            chrome.send(method, ['external-isolation-test', ...args]);
          });
          const permission = await call('getHomePermission');
          const approval = await call('approveHomePermission', $1);
          const cancelled = await call('cancelHomePermission', $1);
          return JSON.stringify({permission, approval: approval.code, cancelled});
        })()
      )",
                                                         permission_id)));
  ASSERT_TRUE(project->GetPendingPermission());
  ASSERT_TRUE(content::ExecJs(preview, R"(
    new Promise(resolve => {
      const tick = () => {
        const button = document.querySelector('dao-home-app').shadowRoot
            .querySelector('[data-test="approve"]');
        if (button) { button.click(); resolve(); }
        else setTimeout(tick, 10);
      };
      tick();
    });
  )"));
  const auto result = preview_result.Take();
  ASSERT_EQ(true, result.FindBool("ok")) << result.DebugString();
  ASSERT_EQ(true, result.FindBool("publishable")) << result.DebugString();
  std::string serialized;
  ASSERT_TRUE(base::JSONWriter::Write(result, &serialized));
  EXPECT_EQ(std::string::npos, serialized.find("private-cookie-value"));
  EXPECT_EQ(std::string::npos, serialized.find("Signed-in fixture content"));
  content::RenderFrameHost* frame = nullptr;
  preview->ForEachRenderFrameHost([&](content::RenderFrameHost* candidate) {
    if (candidate->GetLastCommittedURL().path().find(draft_id_) !=
        std::string::npos) {
      frame = candidate;
    }
  });
  ASSERT_TRUE(frame);
  EXPECT_EQ(
      "Signed-in fixture content",
      content::EvalJs(frame, "document.querySelector('main').textContent"));
  EXPECT_EQ(1, hits->load());
  const char read[] =
      "dao.sources.collect('feed').then(v => v[0].title, e => e.code)";
  const char refresh[] =
      "dao.sources.collect('feed', {}, {fresh:true}).then(v => v[0].title, e "
      "=> e.code)";
  ASSERT_TRUE(
      content::SetCookie(browser()->GetProfile(), fixture,
                         "home_fixture=private-cookie-value; Max-Age=3600; "
                         "Path=/; HttpOnly; SameSite=Lax"));
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(1, hits->load());
  status->store(net::HTTP_SERVICE_UNAVAILABLE);
  EXPECT_EQ("temporarily_unavailable", content::EvalJs(frame, refresh));
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(2, hits->load());

  status->store(net::HTTP_OK);
  ASSERT_TRUE(content::SetCookie(browser()->GetProfile(),
                                 GURL("https://elsewhere.test/"),
                                 "unrelated=value; Path=/"));
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(2, hits->load());
  const std::string pending =
      content::EvalJs(preview, content::JsReplace(R"(
        (async () => {
          const bridge = await import('./home_bridge.js');
          return (await bridge.startHomeDraftConnector($1, 'feed', {pending:true}, true)).execution_id;
        })()
      )",
                                                  draft_id_))
          .ExtractString();
  ASSERT_FALSE(pending.empty());
  ASSERT_TRUE(content::SetCookie(
      browser()->GetProfile(), fixture,
      "home_fixture=second-account; Path=/; HttpOnly; SameSite=Lax"));
  EXPECT_EQ(true, content::EvalJs(preview, content::JsReplace(R"(
    (async () => {
      const bridge = await import('./home_bridge.js');
      return (await bridge.startHomeDraftConnector($1, 'feed', {pending:true}, 'cache-only')).previous_session;
    })()
  )",
                                                              draft_id_)));
  EXPECT_EQ("session_changed",
            content::EvalJs(preview, content::JsReplace(R"(
    (async () => {
      const bridge = await import('./home_bridge.js');
      return (await bridge.finishHomeConnector($1, [{title:'Old account'}])).code;
    })()
  )",
                                                        pending)));
  EXPECT_EQ("Second account", content::EvalJs(frame, read));
  EXPECT_EQ(4, hits->load());

  base::test::TestFuture<void> cleared;
  browser()->GetProfile()->GetDefaultStoragePartition()->ClearDataForOrigin(
      content::StoragePartition::REMOVE_DATA_MASK_LOCAL_STORAGE,
      fixture.DeprecatedGetOriginAsURL(), cleared.GetCallback());
  ASSERT_TRUE(cleared.Wait());
  EXPECT_EQ("Second account", content::EvalJs(frame, read));
  EXPECT_EQ(5, hits->load());

  hold_next->store(true);
  ASSERT_TRUE(content::ExecJs(
      frame, std::string("window.pendingRead = ") + refresh + "; true;"));
  slow.WaitForRequest();
  ASSERT_TRUE(slow.http_request()->headers.at("Cookie").contains(
      "home_fixture=second-account"));
  slow.Send(
      "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nCache-Control: "
      "no-store\r\n\r\n"
      "<main data-signed-in>Second account</main>");
  ASSERT_TRUE(
      content::SetCookie(browser()->GetProfile(), fixture,
                         "home_fixture=private-cookie-value; Max-Age=600; "
                         "Path=/; HttpOnly; SameSite=Lax"));
  slow.Done();
  EXPECT_EQ("session_changed", content::EvalJs(frame, "window.pendingRead"));
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(7, hits->load());

  ASSERT_TRUE(content::SetCookie(
      browser()->GetProfile(), fixture,
      "home_fixture=private-cookie-value; Max-Age=3600; Path=/; SameSite=Lax"));
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(8, hits->load());

  status->store(net::HTTP_UNAUTHORIZED);
  EXPECT_EQ("auth_required", content::EvalJs(frame, refresh));
  EXPECT_EQ("auth_required", content::EvalJs(frame, read));
  EXPECT_EQ(10, hits->load());
  status->store(net::HTTP_OK);
  ASSERT_TRUE(content::SetCookie(browser()->GetProfile(), fixture,
                                 "bootstrap=1; Path=/"));
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(12, hits->load());
  EXPECT_EQ("Signed-in fixture content", content::EvalJs(frame, read));
  EXPECT_EQ(12, hits->load());
  ASSERT_TRUE(
      content::SetCookie(browser()->GetProfile(), fixture,
                         "home_fixture=; Max-Age=0; Path=/; SameSite=Lax"));
  ASSERT_TRUE(content::SetCookie(browser()->GetProfile(), fixture,
                                 "home_fixture=second-account; Max-Age=2; "
                                 "Path=/private-feed; HttpOnly; SameSite=Lax"));
  EXPECT_EQ("Second account", content::EvalJs(frame, read));
  EXPECT_EQ("Second account", content::EvalJs(frame, read));
  EXPECT_EQ(13, hits->load());
  ASSERT_TRUE(content::ExecJs(
      frame, "new Promise(resolve => setTimeout(resolve, 2200))"));
  EXPECT_EQ("Guest", content::EvalJs(frame, read));
  EXPECT_EQ(14, hits->load());

  ASSERT_TRUE(content::NavigateToURL(ordinary, fixture));
  ASSERT_TRUE(
      content::ExecJs(ordinary, "localStorage.setItem('account', 'Local A')"));
  EXPECT_EQ("Local A", content::EvalJs(frame, read));
  const auto account_a_visits = hits->load();
  ASSERT_TRUE(
      content::ExecJs(ordinary, "localStorage.setItem('account', 'Local A')"));
  EXPECT_EQ("Local A", content::EvalJs(frame, read));
  EXPECT_EQ(account_a_visits, hits->load());

  const auto pending_local =
      content::EvalJs(preview, content::JsReplace(R"(
    import('./home_bridge.js').then(async bridge =>
      (await bridge.startHomeDraftConnector($1, 'feed', {}, true)).execution_id)
  )",
                                                  draft_id_))
          .ExtractString();
  ASSERT_TRUE(
      content::ExecJs(ordinary, "localStorage.setItem('account', 'Local B')"));
  EXPECT_EQ("session_changed",
            content::EvalJs(preview, content::JsReplace(R"(
    import('./home_bridge.js').then(async bridge =>
      (await bridge.finishHomeConnector($1, [{title: 'Local A'}])).code)
  )",
                                                        pending_local)));
  EXPECT_EQ("Local B", content::EvalJs(frame, read));
  ASSERT_TRUE(content::ExecJs(ordinary, "localStorage.removeItem('account')"));
  EXPECT_EQ("Guest", content::EvalJs(frame, read));
  ASSERT_TRUE(
      content::ExecJs(ordinary, "localStorage.setItem('account', 'Local C')"));
  EXPECT_EQ("Local C", content::EvalJs(frame, read));
  ASSERT_TRUE(content::ExecJs(ordinary, "localStorage.clear()"));
  EXPECT_EQ("Guest", content::EvalJs(frame, read));
  const int before_retest = hits->load();
  const auto retest = Call(
      "draft.test_source",
      base::DictValue().Set("draft_id", draft_id_).Set("connector_id", "feed"));
  ASSERT_EQ(true, retest.FindBool("ok")) << retest.DebugString();
  EXPECT_EQ(before_retest + 1, hits->load());
  EXPECT_FALSE(g_browser_process->local_state()->GetBoolean(
      prefs::kDaoMcpServerEnabled));
}

}  // namespace
}  // namespace dao
