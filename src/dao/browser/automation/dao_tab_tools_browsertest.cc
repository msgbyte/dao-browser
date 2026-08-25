// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_tab_tools.h"

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/views/dao_tab_identity.h"
#include "net/dns/mock_host_resolver.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/webview/webview.h"

namespace dao {
namespace {

template <typename T>
T* FindDescendantViewOfClass(views::View* root) {
  if (!root) {
    return nullptr;
  }
  if (auto* view = views::AsViewClass<T>(root)) {
    return view;
  }
  for (views::View* child : root->children()) {
    if (auto* view = FindDescendantViewOfClass<T>(child)) {
      return view;
    }
  }
  return nullptr;
}

const base::DictValue* FindListedTabByUrl(const base::ListValue& tabs,
                                          const GURL& url) {
  for (const base::Value& value : tabs) {
    const base::DictValue* tab = value.GetIfDict();
    const std::string* listed_url = tab ? tab->FindString("url") : nullptr;
    if (listed_url && *listed_url == url.spec()) {
      return tab;
    }
  }
  return nullptr;
}

bool ContainsContents(TabListInterface* tabs, content::WebContents* contents) {
  if (!tabs || !contents) {
    return false;
  }
  for (tabs::TabInterface* tab : tabs->GetAllTabs()) {
    if (tab->GetContents() == contents) {
      return true;
    }
  }
  return false;
}

class TestTabUiDelegate : public DaoPageTools::UiDelegate {
 public:
  void MoveCursor(content::WebContents* target,
                  double x,
                  double y,
                  base::OnceCallback<void(bool)> callback) override {
    std::move(callback).Run(false);
  }
  void PlayClickRipple(content::WebContents* target) override {}
  void CancelCursor(content::WebContents* target) override {}
  bool IsTargetLocked(content::WebContents* target) override { return false; }
  void LockTarget(content::WebContents* target) override {}
  void UnlockTarget(content::WebContents* target) override {}
};

class DaoMcpTabToolsBrowserTest : public InProcessBrowserTest {
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
    return embedded_test_server()->GetURL("/title1.html");
  }

  GURL second_url() const {
    return embedded_test_server()->GetURL("/title2.html");
  }

  GURL third_url() const {
    return embedded_test_server()->GetURL("/simple.html");
  }

  TabListInterface* tabs(BrowserWindowInterface* window = nullptr) {
    return TabListInterface::From(window ? window : browser());
  }

  tabs::TabInterface* OpenTab(BrowserWindowInterface* window,
                              const GURL& url,
                              bool foreground = false) {
    TabListInterface* tab_list = tabs(window);
    CHECK(tab_list);
    tabs::TabInterface* tab =
        tab_list->OpenTab(url, tab_list->GetActiveIndex(), foreground);
    CHECK(tab);
    CHECK(content::WaitForLoadStop(tab->GetContents()));
    return tab;
  }

  std::unique_ptr<DaoBrowserAutomationSession> MakeSession(
      BrowserWindowInterface* window = nullptr,
      content::WebContents* target = nullptr) {
    BrowserWindowInterface* authorized_window = window ? window : browser();
    if (!target) {
      tabs::TabInterface* active = tabs(authorized_window)->GetActiveTab();
      CHECK(active);
      target = active->GetContents();
    }
    return std::make_unique<DaoBrowserAutomationSession>(authorized_window,
                                                         target);
  }

  DaoBrowserToolResult Execute(DaoBrowserAutomationSession* session,
                               std::string name,
                               base::DictValue arguments = base::DictValue(),
                               DaoToolClient client = DaoToolClient::kMcp) {
    DaoBrowserToolCall call;
    call.request_id = "tab-browser-test-" + base::NumberToString(++request_id_);
    call.name = std::move(name);
    call.arguments = std::move(arguments);
    base::test::TestFuture<DaoBrowserToolResult> future;
    executor_->Execute(session, client, std::move(call), future.GetCallback());
    return future.Take();
  }

  std::string ExecuteAsync(DaoBrowserAutomationSession* session,
                           std::string name,
                           base::DictValue arguments,
                           base::TimeDelta timeout,
                           base::test::TestFuture<DaoBrowserToolResult>* future,
                           int* callback_count) {
    DaoBrowserToolCall call;
    call.request_id =
        "tab-browser-async-test-" + base::NumberToString(++request_id_);
    call.name = std::move(name);
    call.arguments = std::move(arguments);
    call.timeout = timeout;
    const std::string request_id = call.request_id;
    executor_->Execute(
        session, DaoToolClient::kMcp, std::move(call),
        base::BindOnce(
            [](base::test::TestFuture<DaoBrowserToolResult>* result_future,
               int* count, DaoBrowserToolResult result) {
              ++*count;
              result_future->SetValue(std::move(result));
            },
            future, callback_count));
    return request_id;
  }

  content::WebContents* LoadAgentWebUi() {
    auto* sidebar =
        BrowserView::GetBrowserViewForBrowser(browser())->dao_agent_sidebar();
    CHECK(sidebar);
    auto* web_view = FindDescendantViewOfClass<views::WebView>(sidebar);
    CHECK(web_view);
    content::WebContents* agent_contents = web_view->GetWebContents();
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

  base::DictValue CallAgentNative(content::WebContents* agent_contents,
                                  const std::string& method,
                                  const std::string& params_json = "{}") {
    const std::string script = base::StrCat({
        R"JS(
      (async () => {
        const method = ')JS",
        method,
        R"JS(';
        const params = JSON.parse(')JS",
        params_json,
        R"JS(');
        return await new Promise(resolve => {
          const id = method + '_tab_tools_browser_test_' +
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
            resolve(JSON.stringify(payload || {}));
          };
          chrome.send(method, [id, params]);
        });
      })()
    )JS"});
    std::optional<base::Value> parsed = base::JSONReader::Read(
        content::EvalJs(agent_contents, script).ExtractString(),
        base::JSON_PARSE_RFC);
    CHECK(parsed && parsed->is_dict());
    return std::move(*parsed).TakeDict();
  }

  void StartAgentNativeAsync(content::WebContents* agent_contents,
                             const std::string& method,
                             const std::string& params_json = "{}") {
    const std::string script = base::StrCat({
        R"JS(
      (() => {
        const method = ')JS",
        method,
        R"JS(';
        const params = JSON.parse(')JS",
        params_json,
        R"JS(');
        const id = method + '_tab_tools_async_browser_test_' +
            Math.random().toString(36).slice(2);
        window.__daoTabToolsAsyncPayload = null;
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
          window.__daoTabToolsAsyncPayload = JSON.stringify(payload || {});
        };
        chrome.send(method, [id, params]);
        return true;
      })()
    )JS"});
    ASSERT_TRUE(content::EvalJs(agent_contents, script).ExtractBool());
  }

  base::DictValue TakeAgentNativeAsync(content::WebContents* agent_contents) {
    const std::string payload = content::EvalJs(agent_contents, R"JS(
          (async () => {
            while (window.__daoTabToolsAsyncPayload === null) {
              await new Promise(resolve => setTimeout(resolve, 0));
            }
            const result = window.__daoTabToolsAsyncPayload;
            window.__daoTabToolsAsyncPayload = null;
            return result;
          })()
        )JS")
                                    .ExtractString();
    std::optional<base::Value> parsed =
        base::JSONReader::Read(payload, base::JSON_PARSE_RFC);
    CHECK(parsed && parsed->is_dict());
    return std::move(*parsed).TakeDict();
  }

  TestTabUiDelegate ui_delegate_;
  std::unique_ptr<DaoDevToolsClient> devtools_client_;
  std::unique_ptr<DaoBrowserToolExecutor> executor_;
  int request_id_ = 0;
};

class DaoSidebarTabIdentityBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoSidebarTabIdentityBrowserTest,
                       GetOrCreateReturnsStableIdentity) {
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(nullptr, contents);

  const std::string first = GetOrCreateSidebarTabId(contents);
  const std::string second = GetOrCreateSidebarTabId(contents);

  EXPECT_FALSE(first.empty());
  EXPECT_EQ(first, second);
  EXPECT_EQ(first, GetSidebarTabId(contents));
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       RegistersExactlyTheFourSharedTabTools) {
  constexpr std::array<std::string_view, 4> kSharedTabTools = {
      "list_tabs", "switch_tab", "open_tab", "close_tab"};
  for (std::string_view name : kSharedTabTools) {
    EXPECT_TRUE(DaoTabTools::Handles(name)) << name;
  }
  EXPECT_FALSE(DaoTabTools::Handles("get_page_info"));
  EXPECT_FALSE(DaoTabTools::Handles("resolve_element_context"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       CatalogAcceptsStableOrLegacyTabSelectors) {
  const DaoBrowserToolCatalog* catalog = DaoBrowserToolCatalog::Get();
  for (std::string_view name : {"switch_tab", "close_tab"}) {
    const DaoBrowserToolDefinition* definition =
        catalog->Find(name, DaoToolClient::kMcp);
    ASSERT_NE(nullptr, definition);
    const base::DictValue* properties =
        definition->input_schema.FindDict("properties");
    ASSERT_NE(nullptr, properties);
    EXPECT_NE(nullptr, properties->FindDict("tab_id")) << name;
    EXPECT_NE(nullptr, properties->FindDict("index")) << name;
  }
  const DaoBrowserToolDefinition* switch_definition =
      catalog->Find("switch_tab", DaoToolClient::kMcp);
  ASSERT_NE(nullptr, switch_definition);
  const base::ListValue* required =
      switch_definition->input_schema.FindList("required");
  ASSERT_NE(nullptr, required);
  EXPECT_TRUE(required->empty());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       ListTabsKeepsStableIdsAcrossReorder) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* second = OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), first->GetContents());

  DaoBrowserToolResult before = Execute(session.get(), "list_tabs");
  ASSERT_TRUE(before.ok) << before.error->message;
  ASSERT_TRUE(before.data.is_dict());
  const base::ListValue* before_tabs = before.data.GetDict().FindList("tabs");
  ASSERT_NE(nullptr, before_tabs);
  ASSERT_EQ(2u, before_tabs->size());
  const base::DictValue* first_before =
      FindListedTabByUrl(*before_tabs, first_url());
  const base::DictValue* second_before =
      FindListedTabByUrl(*before_tabs, second_url());
  ASSERT_NE(nullptr, first_before);
  ASSERT_NE(nullptr, second_before);
  const std::string first_id = *first_before->FindString("tab_id");
  const std::string second_id = *second_before->FindString("tab_id");
  const int first_index = first_before->FindInt("index").value();
  const int second_index = second_before->FindInt("index").value();
  EXPECT_NE(first_id, second_id);
  EXPECT_TRUE(first_before->FindBool("active").value());
  EXPECT_FALSE(second_before->FindBool("active").value());
  EXPECT_EQ("Title Of Awesomeness", *second_before->FindString("title"));

  tabs()->MoveTab(second->GetHandle(), first_index);

  DaoBrowserToolResult after = Execute(session.get(), "list_tabs");
  ASSERT_TRUE(after.ok) << after.error->message;
  const base::ListValue* after_tabs = after.data.GetDict().FindList("tabs");
  ASSERT_NE(nullptr, after_tabs);
  const base::DictValue* first_after =
      FindListedTabByUrl(*after_tabs, first_url());
  const base::DictValue* second_after =
      FindListedTabByUrl(*after_tabs, second_url());
  ASSERT_NE(nullptr, first_after);
  ASSERT_NE(nullptr, second_after);
  EXPECT_EQ(first_id, *first_after->FindString("tab_id"));
  EXPECT_EQ(second_id, *second_after->FindString("tab_id"));
  EXPECT_EQ(second_index, first_after->FindInt("index").value());
  EXPECT_EQ(first_index, second_after->FindInt("index").value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       DuplicateTabIdMakesSwitchFailClosed) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* duplicate = OpenTab(browser(), second_url());
  tabs::TabInterface* pinned = OpenTab(browser(), third_url(), true);
  auto session = MakeSession(browser(), pinned->GetContents());
  const std::string duplicate_id =
      GetOrCreateSidebarTabId(first->GetContents());
  SetSidebarTabId(duplicate->GetContents(), duplicate_id);

  DaoBrowserToolResult result =
      Execute(session.get(), "switch_tab",
              base::DictValue().Set("tab_id", duplicate_id));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_EQ(pinned, tabs()->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(pinned->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       DuplicateTabIdMakesCloseFailClosed) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* duplicate = OpenTab(browser(), second_url());
  tabs::TabInterface* pinned = OpenTab(browser(), third_url(), true);
  auto session = MakeSession(browser(), pinned->GetContents());
  const std::string duplicate_id =
      GetOrCreateSidebarTabId(first->GetContents());
  SetSidebarTabId(duplicate->GetContents(), duplicate_id);
  const int original_count = tabs()->GetTabCount();

  DaoBrowserToolResult result =
      Execute(session.get(), "close_tab",
              base::DictValue().Set("tab_id", duplicate_id));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_EQ(original_count, tabs()->GetTabCount());
  EXPECT_TRUE(ContainsContents(tabs(), first->GetContents()));
  EXPECT_TRUE(ContainsContents(tabs(), duplicate->GetContents()));
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(pinned->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       ListTabsDeterministicallyRepairsDuplicateIds) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* duplicate = OpenTab(browser(), second_url());
  auto session = MakeSession();
  const std::string duplicate_id =
      GetOrCreateSidebarTabId(first->GetContents());
  SetSidebarTabId(duplicate->GetContents(), duplicate_id);
  std::vector<tabs::TabInterface*> ordered_tabs = tabs()->GetAllTabs();
  ASSERT_EQ(2u, ordered_tabs.size());
  tabs::TabInterface* canonical = ordered_tabs[0];
  tabs::TabInterface* repaired_copy = ordered_tabs[1];

  DaoBrowserToolResult listed = Execute(session.get(), "list_tabs");

  ASSERT_TRUE(listed.ok) << listed.error->message;
  const base::ListValue* listed_tabs = listed.data.GetDict().FindList("tabs");
  ASSERT_NE(nullptr, listed_tabs);
  const base::DictValue* listed_canonical = FindListedTabByUrl(
      *listed_tabs, canonical->GetContents()->GetVisibleURL());
  const base::DictValue* listed_repaired = FindListedTabByUrl(
      *listed_tabs, repaired_copy->GetContents()->GetVisibleURL());
  ASSERT_NE(nullptr, listed_canonical);
  ASSERT_NE(nullptr, listed_repaired);
  EXPECT_EQ(duplicate_id, *listed_canonical->FindString("tab_id"));
  EXPECT_NE(duplicate_id, *listed_repaired->FindString("tab_id"));
  EXPECT_NE(*listed_canonical->FindString("tab_id"),
            *listed_repaired->FindString("tab_id"));
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpTabToolsBrowserTest,
    ListRepairsIdentityAfterPinnedReopenAndHistoricalRestore) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* original = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, original);
  const std::string historical_id =
      GetOrCreateSidebarTabId(original->GetContents());
  std::map<std::string, std::string> historical_extra_data;
  PopulateSidebarTabIdentityExtraData(original->GetContents(),
                                      &historical_extra_data);
  OpenTab(browser(), second_url(), true);
  tabs()->CloseTab(original->GetHandle());

  tabs::TabInterface* pinned_reopen = OpenTab(browser(), first_url(), true);
  SetSidebarTabId(pinned_reopen->GetContents(), historical_id);
  auto historical_restore = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* historical_restore_contents = historical_restore.get();
  RestoreSidebarTabIdentityFromExtraData(historical_restore_contents,
                                         historical_extra_data);
  tabs::TabInterface* restored_tab = tabs()->InsertWebContentsAt(
      tabs()->GetTabCount(), std::move(historical_restore),
      /*should_pin=*/false, std::nullopt);
  ASSERT_NE(nullptr, restored_tab);
  ASSERT_EQ(historical_id,
            GetOrCreateSidebarTabId(historical_restore_contents));
  auto session = MakeSession(browser(), pinned_reopen->GetContents());

  DaoBrowserToolResult listed = Execute(session.get(), "list_tabs");

  ASSERT_TRUE(listed.ok) << listed.error->message;
  EXPECT_NE(GetOrCreateSidebarTabId(pinned_reopen->GetContents()),
            GetOrCreateSidebarTabId(historical_restore_contents));
  std::set<std::string> listed_ids;
  const base::ListValue* listed_tabs = listed.data.GetDict().FindList("tabs");
  ASSERT_NE(nullptr, listed_tabs);
  for (const base::Value& listed_tab : *listed_tabs) {
    const base::DictValue* tab = listed_tab.GetIfDict();
    ASSERT_NE(nullptr, tab);
    const std::string* id = tab->FindString("tab_id");
    ASSERT_NE(nullptr, id);
    EXPECT_TRUE(listed_ids.insert(*id).second);
  }
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       ListTabsOnlyReturnsAuthorizedWindow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSession();
  Browser* other_window = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(other_window, third_url()));

  DaoBrowserToolResult result = Execute(session.get(), "list_tabs");

  ASSERT_TRUE(result.ok) << result.error->message;
  const base::ListValue* listed = result.data.GetDict().FindList("tabs");
  ASSERT_NE(nullptr, listed);
  EXPECT_EQ(tabs()->GetTabCount(), static_cast<int>(listed->size()));
  EXPECT_NE(nullptr, FindListedTabByUrl(*listed, first_url()));
  EXPECT_EQ(nullptr, FindListedTabByUrl(*listed, third_url()));
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       SwitchTabIdSurvivesReorderAndTakesPrecedence) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* second = OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), first->GetContents());
  const std::string second_id = GetOrCreateSidebarTabId(second->GetContents());
  const int old_second_index = tabs()->GetIndexOfTab(second->GetHandle());
  tabs()->MoveTab(second->GetHandle(),
                  tabs()->GetIndexOfTab(first->GetHandle()));
  ASSERT_NE(second, tabs()->GetTab(old_second_index));

  DaoBrowserToolResult result = Execute(session.get(), "switch_tab",
                                        base::DictValue()
                                            .Set("tab_id", second_id)
                                            .Set("index", old_second_index));

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(second, tabs()->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(second->GetContents(), session->ResolveTarget().value());
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(second_id, result.target->tab_id);
  EXPECT_EQ(second_url().spec(), result.target->url);
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       SwitchRequiresAtLeastOneSelector) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSession();

  DaoBrowserToolResult result = Execute(session.get(), "switch_tab");

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpTabToolsBrowserTest,
    McpSwitchRejectsForbiddenCandidateBeforeActivatingOrRetargeting) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* pinned = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, pinned);
  tabs::TabInterface* forbidden = OpenTab(browser(), GURL("chrome://version/"));
  auto session = MakeSession(browser(), pinned->GetContents());
  const std::string forbidden_id =
      GetOrCreateSidebarTabId(forbidden->GetContents());
  ASSERT_NE(forbidden, tabs()->GetActiveTab());

  DaoBrowserToolResult result = Execute(
      session.get(), "switch_tab",
      base::DictValue().Set("tab_id", forbidden_id), DaoToolClient::kMcp);

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, result.error->code);
  EXPECT_EQ(pinned, tabs()->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(pinned->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       AgentSwitchRetargetsWithoutActivation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* pinned = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, pinned);
  tabs::TabInterface* internal = OpenTab(browser(), GURL("chrome://version/"));
  auto session = MakeSession(browser(), pinned->GetContents());

  DaoBrowserToolResult result =
      Execute(session.get(), "switch_tab",
              base::DictValue().Set(
                  "tab_id", GetOrCreateSidebarTabId(internal->GetContents())),
              DaoToolClient::kDaoAgent);

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(pinned, tabs()->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(internal->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       AgentTargetStaysPinnedAcrossUserTabChanges) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* foreground = tabs()->GetActiveTab();
  tabs::TabInterface* target = OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), foreground->GetContents());

  DaoBrowserToolResult switched = Execute(
      session.get(), "switch_tab",
      base::DictValue().Set(
          "tab_id", GetOrCreateSidebarTabId(target->GetContents())),
      DaoToolClient::kDaoAgent);
  ASSERT_TRUE(switched.ok) << switched.error->message;
  EXPECT_EQ(foreground, tabs()->GetActiveTab());

  tabs::TabInterface* new_foreground = OpenTab(browser(), third_url(), true);
  DaoBrowserToolResult page =
      Execute(session.get(), "get_page_info", base::DictValue(),
              DaoToolClient::kDaoAgent);

  ASSERT_TRUE(page.ok) << page.error->message;
  EXPECT_EQ(second_url().spec(), *page.data.GetDict().FindString("url"));
  EXPECT_EQ(new_foreground, tabs()->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(target->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpTabToolsBrowserTest,
    SwitchSurvivesTargetCallbackDestroyingSessionAndExecutor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* second = OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), first->GetContents());
  session->SetTargetChangedCallback(base::BindRepeating(
      [](std::unique_ptr<DaoBrowserToolExecutor>* owned_executor,
         std::unique_ptr<DaoBrowserAutomationSession>* owned_session,
         DaoBrowserAutomationSession*) {
        owned_executor->reset();
        owned_session->reset();
      },
      &executor_, &session));

  DaoBrowserToolCall call;
  call.request_id = "switch-destroys-owners";
  call.name = "switch_tab";
  call.arguments.Set("tab_id", GetOrCreateSidebarTabId(second->GetContents()));
  base::test::TestFuture<DaoBrowserToolResult> future;
  DaoBrowserToolExecutor* executor = executor_.get();
  executor->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                    future.GetCallback());
  DaoBrowserToolResult result = future.Take();

  EXPECT_FALSE(executor_);
  EXPECT_FALSE(session);
  EXPECT_EQ(second, tabs()->GetActiveTab());
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       OtherWindowTabIdIsInvalidWithoutFallback) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* authorized_active = tabs()->GetActiveTab();
  auto session = MakeSession();
  Browser* other_window = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(other_window, third_url()));
  TabListInterface* other_tabs = tabs(other_window);
  tabs::TabInterface* other_active = other_tabs->GetActiveTab();
  const std::string other_id =
      GetOrCreateSidebarTabId(other_active->GetContents());

  DaoBrowserToolResult result =
      Execute(session.get(), "switch_tab",
              base::DictValue().Set("tab_id", other_id).Set("index", 0));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_EQ(authorized_active, tabs()->GetActiveTab());
  EXPECT_EQ(other_active, other_tabs->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(authorized_active->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       OpenTabStaysInWindowAndUpdatesTarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSession();
  Browser* other_window = CreateBrowser(browser()->profile());
  const int source_count = tabs()->GetTabCount();
  const int other_count = tabs(other_window)->GetTabCount();

  DaoBrowserToolResult result =
      Execute(session.get(), "open_tab",
              base::DictValue().Set("url", second_url().spec()));

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(source_count + 1, tabs()->GetTabCount());
  EXPECT_EQ(other_count, tabs(other_window)->GetTabCount());
  tabs::TabInterface* opened = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, opened);
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(opened->GetContents(), session->ResolveTarget().value());
  EXPECT_TRUE(ContainsContents(tabs(), session->ResolveTarget().value()));
  EXPECT_FALSE(
      ContainsContents(tabs(other_window), session->ResolveTarget().value()));
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(GetOrCreateSidebarTabId(opened->GetContents()),
            result.target->tab_id);
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       AgentOpenTabRetargetsWithoutActivation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* foreground = tabs()->GetActiveTab();
  auto session = MakeSession(browser(), foreground->GetContents());
  const int original_count = tabs()->GetTabCount();

  DaoBrowserToolResult result =
      Execute(session.get(), "open_tab",
              base::DictValue().Set("url", second_url().spec()),
              DaoToolClient::kDaoAgent);

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(original_count + 1, tabs()->GetTabCount());
  EXPECT_EQ(foreground, tabs()->GetActiveTab());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  content::WebContents* opened = session->ResolveTarget().value();
  EXPECT_NE(foreground->GetContents(), opened);
  EXPECT_EQ(second_url(), opened->GetVisibleURL());
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(GetOrCreateSidebarTabId(opened), result.target->tab_id);
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       OpenTabRejectsForbiddenSchemesBeforeMutation) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* original_active = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, original_active);
  auto session = MakeSession();
  const int original_count = tabs()->GetTabCount();

  constexpr std::array<std::string_view, 9> kForbiddenUrls = {
      "chrome://settings/",
      "chrome://agent/",
      "dao://settings/",
      "file:///tmp/dao-tab-test",
      "chrome-extension://abcdefghijklmnopabcdefghijklmnop/index.html",
      "devtools://devtools/bundled/inspector.html",
      "data:text/plain,blocked",
      "javascript:document.body.textContent='blocked'",
      "custom-scheme://blocked/",
  };
  for (std::string_view forbidden_url : kForbiddenUrls) {
    DaoBrowserToolResult result =
        Execute(session.get(), "open_tab",
                base::DictValue().Set("url", std::string(forbidden_url)));

    ASSERT_FALSE(result.ok) << forbidden_url;
    ASSERT_TRUE(result.error.has_value()) << forbidden_url;
    EXPECT_EQ(DaoToolErrorCode::kTargetForbidden, result.error->code)
        << forbidden_url;
    EXPECT_EQ(original_count, tabs()->GetTabCount()) << forbidden_url;
    EXPECT_EQ(original_active, tabs()->GetActiveTab()) << forbidden_url;
    ASSERT_TRUE(session->ResolveTarget().has_value()) << forbidden_url;
    EXPECT_EQ(original_active->GetContents(), session->ResolveTarget().value())
        << forbidden_url;
  }
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       OpenTabAllowsHttpHttpsMissingAndLiteralBlank) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSession();
  const int original_count = tabs()->GetTabCount();

  std::array<base::DictValue, 4> arguments = {
      base::DictValue(),
      base::DictValue().Set("url", "about:blank"),
      base::DictValue().Set("url", second_url().spec()),
      base::DictValue().Set("url", "https://example.test/allowed"),
  };
  for (size_t i = 0; i < arguments.size(); ++i) {
    SCOPED_TRACE(i);
    DaoBrowserToolResult result =
        Execute(session.get(), "open_tab", std::move(arguments[i]));
    ASSERT_TRUE(result.ok) << (result.error ? result.error->message
                                            : "missing error");
    if (i + 1 < arguments.size()) {
      ASSERT_TRUE(
          content::WaitForLoadStop(tabs()->GetActiveTab()->GetContents()));
    }
  }

  EXPECT_EQ(original_count + static_cast<int>(arguments.size()),
            tabs()->GetTabCount());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(tabs()->GetActiveTab()->GetContents(),
            session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       OpenTabFindsActualNewTabWhenPinnedIndexIsConstrained) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* pinned = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, pinned);
  tabs()->PinTab(pinned->GetHandle());
  ASSERT_TRUE(pinned->IsPinned());
  auto session = MakeSession();
  const int original_count = tabs()->GetTabCount();

  DaoBrowserToolResult result =
      Execute(session.get(), "open_tab",
              base::DictValue().Set("url", second_url().spec()));

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(original_count + 1, tabs()->GetTabCount());
  tabs::TabInterface* opened = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, opened);
  EXPECT_NE(pinned, opened);
  EXPECT_EQ(second_url(), opened->GetContents()->GetVisibleURL());
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(opened->GetContents(), session->ResolveTarget().value());
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(GetOrCreateSidebarTabId(opened->GetContents()),
            result.target->tab_id);
  EXPECT_EQ(tabs()->GetIndexOfTab(opened->GetHandle()),
            result.data.GetDict().FindInt("index").value_or(-1));
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       CloseTabIdTakesPrecedenceAndKeepsPinnedTarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* pinned = tabs()->GetActiveTab();
  tabs::TabInterface* second = OpenTab(browser(), second_url());
  tabs::TabInterface* third = OpenTab(browser(), third_url());
  auto session = MakeSession(browser(), pinned->GetContents());
  base::WeakPtr<content::WebContents> second_contents =
      second->GetContents()->GetWeakPtr();
  const std::string second_id = GetOrCreateSidebarTabId(second->GetContents());
  const int third_index = tabs()->GetIndexOfTab(third->GetHandle());

  DaoBrowserToolResult result = Execute(
      session.get(), "close_tab",
      base::DictValue().Set("tab_id", second_id).Set("index", third_index));

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_FALSE(second_contents);
  EXPECT_TRUE(ContainsContents(tabs(), third->GetContents()));
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(pinned->GetContents(), session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       ClosingPinnedTargetSelectsSameWindowTarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* pinned = tabs()->GetActiveTab();
  tabs::TabInterface* visible = OpenTab(browser(), second_url(), true);
  auto session = MakeSession(browser(), pinned->GetContents());
  base::WeakPtr<content::WebContents> pinned_contents =
      pinned->GetContents()->GetWeakPtr();
  Browser* other_window = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(other_window, third_url()));
  content::WebContents* other_contents =
      tabs(other_window)->GetActiveTab()->GetContents();

  DaoBrowserToolResult result = Execute(session.get(), "close_tab");

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_FALSE(pinned_contents);
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(visible->GetContents(), session->ResolveTarget().value());
  EXPECT_NE(other_contents, session->ResolveTarget().value());
  EXPECT_TRUE(ContainsContents(tabs(), session->ResolveTarget().value()));
  EXPECT_EQ(visible, tabs()->GetActiveTab());
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(GetOrCreateSidebarTabId(visible->GetContents()),
            result.target->tab_id);
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       CloseWaitsForBeforeUnloadAcceptanceBeforeRetargeting) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  tabs::TabInterface* closing_tab = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, closing_tab);
  content::WebContents* closing_contents = closing_tab->GetContents();
  tabs::TabInterface* replacement = OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), closing_contents);
  base::WeakPtr<content::WebContents> closing_weak =
      closing_contents->GetWeakPtr();
  content::PrepContentsForBeforeUnloadTest(closing_contents);
  int callback_count = 0;
  base::test::TestFuture<DaoBrowserToolResult> future;

  ExecuteAsync(session.get(), "close_tab", base::DictValue(), base::Seconds(30),
               &future, &callback_count);
  javascript_dialogs::AppModalDialogController* dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_NE(nullptr, dialog);
  EXPECT_FALSE(future.IsReady());

  dialog->view()->AcceptAppModalDialog();
  DaoBrowserToolResult result = future.Take();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(closing_weak);
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(replacement->GetContents(), session->ResolveTarget().value());
  ASSERT_TRUE(result.target.has_value());
  EXPECT_EQ(GetOrCreateSidebarTabId(replacement->GetContents()),
            result.target->tab_id);
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       BeforeUnloadCancellationFailsOnceAndKeepsTarget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  tabs::TabInterface* target_tab = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  content::WebContents* target = target_tab->GetContents();
  OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), target);
  content::PrepContentsForBeforeUnloadTest(target);
  int callback_count = 0;
  base::test::TestFuture<DaoBrowserToolResult> future;

  ExecuteAsync(session.get(), "close_tab", base::DictValue(), base::Seconds(30),
               &future, &callback_count);
  javascript_dialogs::AppModalDialogController* dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_NE(nullptr, dialog);
  EXPECT_FALSE(future.IsReady());

  dialog->view()->CancelAppModalDialog();
  DaoBrowserToolResult result = future.Take();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(content::ExecJs(target, "window.onbeforeunload = null;"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
  EXPECT_EQ(1, callback_count);
  EXPECT_TRUE(ContainsContents(tabs(), target));
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(target, session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       PendingCloseExplicitCancellationCompletesOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  content::WebContents* target = tabs()->GetActiveTab()->GetContents();
  OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), target);
  content::PrepContentsForBeforeUnloadTest(target);
  int callback_count = 0;
  base::test::TestFuture<DaoBrowserToolResult> future;

  const std::string request_id =
      ExecuteAsync(session.get(), "close_tab", base::DictValue(),
                   base::Seconds(30), &future, &callback_count);
  javascript_dialogs::AppModalDialogController* dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_NE(nullptr, dialog);
  executor_->Cancel(request_id);
  DaoBrowserToolResult result = future.Take();
  dialog->view()->CancelAppModalDialog();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(content::ExecJs(target, "window.onbeforeunload = null;"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
  EXPECT_EQ(1, callback_count);
  EXPECT_TRUE(ContainsContents(tabs(), target));
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(target, session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       PendingCloseTimeoutCompletesOnceAndUnobserves) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  content::WebContents* target = tabs()->GetActiveTab()->GetContents();
  OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), target);
  content::PrepContentsForBeforeUnloadTest(target);
  int callback_count = 0;
  base::test::TestFuture<DaoBrowserToolResult> future;

  ExecuteAsync(session.get(), "close_tab", base::DictValue(),
               base::Milliseconds(10), &future, &callback_count);
  javascript_dialogs::AppModalDialogController* dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_NE(nullptr, dialog);
  DaoBrowserToolResult result = future.Take();
  dialog->view()->CancelAppModalDialog();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(content::ExecJs(target, "window.onbeforeunload = null;"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolTimeout, result.error->code);
  EXPECT_EQ(1, callback_count);
  EXPECT_TRUE(ContainsContents(tabs(), target));
  ASSERT_TRUE(session->ResolveTarget().has_value());
  EXPECT_EQ(target, session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       AgentAcceptedCloseResetsTargetScopedState) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/beforeunload.html")));
  content::WebContents* closing_target = tabs()->GetActiveTab()->GetContents();
  tabs::TabInterface* replacement = OpenTab(browser(), second_url());
  content::WebContents* agent_contents = LoadAgentWebUi();
  content::PrepContentsForBeforeUnloadTest(closing_target);

  base::DictValue begin = CallAgentNative(agent_contents, "beginAgentTurn");
  ASSERT_TRUE(begin.FindBool("success").value_or(false));
  EXPECT_TRUE(CallAgentNative(agent_contents, "enableNetworkTracking")
                  .FindBool("success")
                  .value_or(false));
  EXPECT_TRUE(CallAgentNative(agent_contents, "getNetworkRequests")
                  .FindBool("enabled")
                  .value_or(false));

  StartAgentNativeAsync(agent_contents, "closeTab");
  javascript_dialogs::AppModalDialogController* dialog =
      ui_test_utils::WaitForAppModalDialog();
  ASSERT_NE(nullptr, dialog);
  dialog->view()->AcceptAppModalDialog();
  base::DictValue closed = TakeAgentNativeAsync(agent_contents);

  EXPECT_TRUE(closed.FindBool("success").value_or(false));
  EXPECT_EQ(replacement, tabs()->GetActiveTab());
  base::DictValue tracking =
      CallAgentNative(agent_contents, "getNetworkRequests");
  EXPECT_TRUE(tracking.FindBool("enabled").value_or(false));
  EXPECT_EQ(0, tracking.FindInt("count").value_or(-1));
  base::DictValue page = CallAgentNative(agent_contents, "getPageInfo");
  ASSERT_NE(nullptr, page.FindString("url"));
  EXPECT_EQ(second_url().spec(), *page.FindString("url"));

  const std::string* turn_id = begin.FindString("turnId");
  ASSERT_NE(nullptr, turn_id);
  base::DictValue ended =
      CallAgentNative(agent_contents, "endAgentTurn",
                      base::StrCat({R"({"turnId":")", *turn_id, R"("})"}));
  EXPECT_TRUE(ended.FindBool("success").value_or(false));
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest, RejectsClosingLastTab) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  auto session = MakeSession();
  ASSERT_EQ(1, tabs()->GetTabCount());

  DaoBrowserToolResult result = Execute(session.get(), "close_tab");

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_EQ(1, tabs()->GetTabCount());
  EXPECT_TRUE(session->ResolveTarget().has_value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       MovedTargetFailsEveryTabToolWithoutFallback) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* target = tabs()->GetActiveTab();
  OpenTab(browser(), second_url());
  auto session = MakeSession(browser(), target->GetContents());
  Browser* other_window = CreateBrowser(browser()->profile());
  TabListInterface* other_tabs = tabs(other_window);
  tabs()->MoveTabToWindow(target->GetHandle(), other_window->GetSessionID(), 0);
  ASSERT_TRUE(ContainsContents(other_tabs, target->GetContents()));
  ASSERT_FALSE(ContainsContents(tabs(), target->GetContents()));
  const int source_count = tabs()->GetTabCount();
  const int other_count = other_tabs->GetTabCount();

  struct CallCase {
    const char* name;
    base::DictValue arguments;
  };
  std::array<CallCase, 4> calls = {
      CallCase{"list_tabs", base::DictValue()},
      CallCase{"switch_tab", base::DictValue().Set("index", 0)},
      CallCase{"open_tab", base::DictValue().Set("url", third_url().spec())},
      CallCase{"close_tab", base::DictValue().Set("index", 0)},
  };
  for (CallCase& call : calls) {
    DaoBrowserToolResult result =
        Execute(session.get(), call.name, std::move(call.arguments));
    ASSERT_FALSE(result.ok) << call.name;
    ASSERT_TRUE(result.error.has_value()) << call.name;
    EXPECT_EQ(DaoToolErrorCode::kTargetGone, result.error->code) << call.name;
  }
  EXPECT_EQ(source_count, tabs()->GetTabCount());
  EXPECT_EQ(other_count, other_tabs->GetTabCount());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       ReplacedPinnedTargetStillSupportsPageAndTabTools) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* target_tab = tabs()->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  const tabs::TabHandle target_handle = target_tab->GetHandle();
  content::WebContents* original_contents = target_tab->GetContents();
  const std::string stable_id = GetOrCreateSidebarTabId(original_contents);
  auto session = MakeSession(browser(), original_contents);
  OpenTab(browser(), second_url(), true);

  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* replacement_contents = replacement.get();
  const int target_index = tabs()->GetIndexOfTab(target_handle);
  ASSERT_GE(target_index, 0);
  std::unique_ptr<content::WebContents> discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          target_index, std::move(replacement));
  ASSERT_EQ(original_contents, discarded.get());
  ASSERT_TRUE(content::NavigateToURL(replacement_contents, first_url()));

  DaoBrowserToolResult page = Execute(session.get(), "get_page_info");
  ASSERT_TRUE(page.ok) << page.error->message;
  EXPECT_EQ(first_url().spec(), *page.data.GetDict().FindString("url"));

  DaoBrowserToolResult listed = Execute(session.get(), "list_tabs");
  ASSERT_TRUE(listed.ok) << listed.error->message;
  const base::ListValue* listed_tabs = listed.data.GetDict().FindList("tabs");
  ASSERT_NE(nullptr, listed_tabs);
  const base::DictValue* listed_target =
      FindListedTabByUrl(*listed_tabs, first_url());
  ASSERT_NE(nullptr, listed_target);
  EXPECT_EQ(stable_id, *listed_target->FindString("tab_id"));

  DaoBrowserToolResult switched = Execute(
      session.get(), "switch_tab", base::DictValue().Set("tab_id", stable_id));
  ASSERT_TRUE(switched.ok) << switched.error->message;
  EXPECT_EQ(target_handle.Get(), tabs()->GetActiveTab());
  EXPECT_EQ(replacement_contents, session->ResolveTarget().value());
}

IN_PROC_BROWSER_TEST_F(DaoMcpTabToolsBrowserTest,
                       AgentHandlersAcceptStableIdsAndKeepLegacyPayloads) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url()));
  tabs::TabInterface* first = tabs()->GetActiveTab();
  tabs::TabInterface* second = OpenTab(browser(), second_url());
  base::WeakPtr<content::WebContents> first_contents =
      first->GetContents()->GetWeakPtr();
  const std::string first_id = GetOrCreateSidebarTabId(first->GetContents());
  content::WebContents* agent_contents = LoadAgentWebUi();

  base::DictValue begin = CallAgentNative(agent_contents, "beginAgentTurn");
  ASSERT_TRUE(begin.FindBool("success").value_or(false));
  const std::string* turn_id = begin.FindString("turnId");
  ASSERT_NE(nullptr, turn_id);

  base::DictValue listed = CallAgentNative(agent_contents, "listTabs");
  const base::ListValue* listed_tabs = listed.FindList("tabs");
  ASSERT_NE(nullptr, listed_tabs);
  EXPECT_EQ(2, listed.FindInt("count").value_or(-1));
  const base::DictValue* listed_second =
      FindListedTabByUrl(*listed_tabs, second_url());
  ASSERT_NE(nullptr, listed_second);
  const std::string* second_id = listed_second->FindString("tab_id");
  ASSERT_NE(nullptr, second_id);
  ASSERT_FALSE(second_id->empty());

  const int first_index = tabs()->GetIndexOfTab(first->GetHandle());
  base::DictValue switched = CallAgentNative(
      agent_contents, "switchTab",
      base::StrCat({R"({"tab_id":")", *second_id, R"(","index":)",
                    base::NumberToString(first_index), "}"}));
  EXPECT_TRUE(switched.FindBool("success").value_or(false));
  EXPECT_EQ(first, tabs()->GetActiveTab());
  base::DictValue switched_page =
      CallAgentNative(agent_contents, "getPageInfo");
  ASSERT_NE(nullptr, switched_page.FindString("url"));
  EXPECT_EQ(second_url().spec(), *switched_page.FindString("url"));

  base::DictValue closed = CallAgentNative(
      agent_contents, "closeTab",
      base::StrCat(
          {R"({"tab_id":")", first_id, R"(","index":)",
           base::NumberToString(tabs()->GetIndexOfTab(second->GetHandle())),
           "}"}));
  EXPECT_TRUE(closed.FindBool("success").value_or(false));
  EXPECT_FALSE(first_contents);
  EXPECT_TRUE(ContainsContents(tabs(), second->GetContents()));
  base::DictValue remaining_page =
      CallAgentNative(agent_contents, "getPageInfo");
  ASSERT_NE(nullptr, remaining_page.FindString("url"));
  EXPECT_EQ(second_url().spec(), *remaining_page.FindString("url"));

  base::DictValue ended =
      CallAgentNative(agent_contents, "endAgentTurn",
                      base::StrCat({R"({"turnId":")", *turn_id, R"("})"}));
  EXPECT_TRUE(ended.FindBool("success").value_or(false));
}

}  // namespace
}  // namespace dao
