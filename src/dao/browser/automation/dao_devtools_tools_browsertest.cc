// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_devtools_tools.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include "base/base64.h"
#include "base/check.h"
#include "base/containers/span.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/automation/dao_page_tools.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

constexpr size_t kExpectedTextLimit = 512 * 1024;
constexpr char kTinyPngBase64[] =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk"
    "+A8AAQUBAScY42YAAAAASUVORK5CYII=";

class TestDevToolsUiDelegate : public DaoPageTools::UiDelegate {
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

class DaoMcpDevToolsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(base::BindRepeating(
        &DaoMcpDevToolsBrowserTest::HandleRequest, base::Unretained(this)));
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

  std::unique_ptr<net::test_server::HttpResponse> HandleRequest(
      const net::test_server::HttpRequest& request) {
    auto response = std::make_unique<net::test_server::BasicHttpResponse>();
    response->set_code(net::HTTP_OK);
    if (request.relative_url == "/devtools.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<!doctype html><link rel=\"stylesheet\" href=\"/search.css\">"
          "<script src=\"/search.js\"></script>"
          "<script src=\"/large.js\"></script>"
          "<img src=\"/binary.png\">"
          "<p>dao main document token</p>"
          "<iframe src=\"/frame-redirect\"></iframe>");
      return response;
    }
    if (request.relative_url == "/oversize-page.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<!doctype html><script src=\"/oversize.js\"></script>");
      return response;
    }
    if (request.relative_url == "/oversize-document.html") {
      response->set_content_type("text/html");
      response->set_content("<!doctype html><main>" +
                            std::string(2 * 1024 * 1024, 'd') +
                            "document-tail-hit</main>");
      return response;
    }
    if (request.relative_url == "/aggregate-search.html") {
      response->set_content_type("text/html");
      std::string html = "<!doctype html>";
      for (int index = 0; index < 9; ++index) {
        html += "<script src=\"/aggregate-" + base::NumberToString(index) +
                ".js\"></script>";
      }
      response->set_content(std::move(html));
      return response;
    }
    if (request.relative_url == "/frame-redirect") {
      response->set_code(net::HTTP_TEMPORARY_REDIRECT);
      response->AddCustomHeader("Location", "/frame.html");
      return response;
    }
    if (request.relative_url == "/frame.html") {
      response->set_content_type("text/html");
      response->set_content(
          "<!doctype html><title>dao nested document token</title>");
      return response;
    }
    if (request.relative_url == "/second.html") {
      response->set_content_type("text/html");
      response->set_content("<!doctype html><title>Second</title>");
      return response;
    }
    if (request.relative_url == "/search.js") {
      response->set_content_type("application/javascript");
      response->set_content(
          "const noMatch = 'plain';\n"
          "const firstHit = 'DAO-search-hit one';\n"
          "const longHit = '" +
          std::string(260, 'x') +
          " dao-search-hit two';\n"
          "const thirdHit = 'dao-search-hit three';\n");
      return response;
    }
    if (request.relative_url == "/large.js") {
      response->set_content_type("application/javascript");
      response->set_content("/*" + std::string(kExpectedTextLimit - 3, 'x') +
                            "\xF0\x9F\x98\x80post-cutoff-hit*/");
      return response;
    }
    if (request.relative_url == "/oversize.js") {
      response->set_content_type("application/javascript");
      response->set_content("/*" + std::string(2 * 1024 * 1024, 'z') + "*/");
      return response;
    }
    if (base::StartsWith(request.relative_url, "/aggregate-")) {
      response->set_content_type("application/javascript");
      response->set_content("/*" + std::string(kExpectedTextLimit - 4, 'a') +
                            "*/");
      return response;
    }
    if (request.relative_url == "/search.css") {
      response->set_content_type("text/css");
      response->set_content(".dao-search-hit { color: rgb(1, 2, 3); }\n");
      return response;
    }
    if (request.relative_url == "/binary.png") {
      response->set_content_type("image/png");
      std::string png;
      CHECK(base::Base64Decode(kTinyPngBase64, &png));
      response->set_content(std::move(png));
      return response;
    }
    if (request.relative_url == "/api-large-body") {
      response->set_content_type("text/plain");
      response->set_content(std::string(kExpectedTextLimit - 1, 'y') +
                            "\xF0\x9F\x98\x80post-cutoff-hit");
      return response;
    }
    if (request.relative_url == "/api-binary-body") {
      response->set_content_type("image/png");
      std::string png;
      CHECK(base::Base64Decode(kTinyPngBase64, &png));
      response->set_content(std::move(png));
      return response;
    }
    if (base::StartsWith(request.relative_url, "/api-body")) {
      response->set_content_type("application/json");
      response->set_content(R"({"source":"dao-network-body"})");
      return response;
    }
    return nullptr;
  }

  GURL page_url() const {
    return embedded_test_server()->GetURL("/devtools.html");
  }

  GURL second_url() const {
    return embedded_test_server()->GetURL("/second.html");
  }

  std::unique_ptr<DaoBrowserAutomationSession> MakeSession(
      content::WebContents* target = nullptr) {
    if (!target) {
      target = browser()->tab_strip_model()->GetActiveWebContents();
    }
    CHECK(target);
    return std::make_unique<DaoBrowserAutomationSession>(browser(), target);
  }

 public:
  DaoBrowserToolResult Execute(DaoBrowserAutomationSession* session,
                               std::string name,
                               base::DictValue arguments = base::DictValue(),
                               base::TimeDelta timeout = base::Seconds(5),
                               DaoToolClient client = DaoToolClient::kMcp) {
    DaoBrowserToolCall call;
    call.request_id =
        "devtools-browser-test-" + base::NumberToString(++request_id_);
    call.name = std::move(name);
    call.arguments = std::move(arguments);
    call.timeout = timeout;
    base::test::TestFuture<DaoBrowserToolResult> future;
    executor_->Execute(session, client, std::move(call), future.GetCallback());
    return future.Take();
  }

 protected:
  std::string ExecuteAsync(DaoBrowserAutomationSession* session,
                           std::string name,
                           base::DictValue arguments,
                           base::test::TestFuture<DaoBrowserToolResult>* future,
                           int* callback_count) {
    DaoBrowserToolCall call;
    call.request_id =
        "devtools-browser-async-test-" + base::NumberToString(++request_id_);
    call.name = std::move(name);
    call.arguments = std::move(arguments);
    call.timeout = base::Seconds(30);
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

  static const base::DictValue* FindEntry(const base::ListValue& entries,
                                          std::string_view field,
                                          std::string_view value) {
    for (const base::Value& entry : entries) {
      const base::DictValue* dict = entry.GetIfDict();
      const std::string* candidate = dict ? dict->FindString(field) : nullptr;
      if (candidate && *candidate == value) {
        return dict;
      }
    }
    return nullptr;
  }

  bool WaitForResponseRequestId(DaoBrowserAutomationSession* session,
                                const GURL& url,
                                std::string* request_id) {
    return base::test::RunUntil([&] {
      DaoBrowserToolResult captured = Execute(session, "get_network_requests");
      if (!captured.ok || !captured.data.is_dict()) {
        return false;
      }
      const base::ListValue* requests =
          captured.data.GetDict().FindList("requests");
      if (!requests) {
        return false;
      }
      for (const base::Value& value : *requests) {
        const base::DictValue* entry = value.GetIfDict();
        const std::string* candidate_url =
            entry ? entry->FindString("url") : nullptr;
        const std::string* phase = entry ? entry->FindString("phase") : nullptr;
        const std::string* candidate_id =
            entry ? entry->FindString("request_id") : nullptr;
        if (candidate_url && phase && candidate_id &&
            *candidate_url == url.spec() && *phase == "response") {
          *request_id = *candidate_id;
          return true;
        }
      }
      return false;
    });
  }

  TestDevToolsUiDelegate ui_delegate_;
  std::unique_ptr<DaoDevToolsClient> devtools_client_;
  std::unique_ptr<DaoBrowserToolExecutor> executor_;
  int request_id_ = 0;
};

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       RegistersExactlyTheElevenSharedDevToolsTools) {
  constexpr std::array<std::string_view, 11> kNames = {
      "enable_network_tracking",   "get_network_requests",
      "wait_for_network_response", "clear_network_requests",
      "get_network_body",          "enable_console_tracking",
      "get_console_messages",      "clear_console_messages",
      "list_page_resources",       "get_resource_content",
      "search_in_resources",
  };
  for (std::string_view name : kNames) {
    EXPECT_TRUE(DaoDevToolsTools::Handles(name)) << name;
  }
  EXPECT_FALSE(DaoDevToolsTools::Handles("get_page_info"));
  EXPECT_FALSE(DaoDevToolsTools::Handles("list_tabs"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       DomainEnableConfirmationIsMonotonicWithinBinding) {
  auto session = MakeSession();
  DaoBrowserAutomationSession::DevToolsState& state = session->devtools_state();
  state.binding_generation = 7;
  state.bound_host = content::DevToolsAgentHost::GetOrCreateFor(
      browser()->tab_strip_model()->GetActiveWebContents());
  ASSERT_TRUE(state.bound_host);

  for (bool network_domain : {true, false}) {
    bool& enabled = network_domain ? state.network_tracking_enabled
                                   : state.console_tracking_enabled;
    bool& requested = network_domain ? state.network_tracking_requested
                                     : state.console_tracking_requested;
    uint64_t& epoch = network_domain ? state.network_enable_attempt_epoch
                                     : state.console_enable_attempt_epoch;

    // Automatic success followed by a redundant explicit failure.
    enabled = false;
    requested = true;
    epoch = 2;
    EXPECT_TRUE(DaoDevToolsTools::ApplyDomainEnableResultForTesting(
        session.get(), 7, state.bound_host.get(), 1, network_domain, true));
    EXPECT_TRUE(enabled);
    EXPECT_TRUE(DaoDevToolsTools::ApplyDomainEnableResultForTesting(
        session.get(), 7, state.bound_host.get(), 2, network_domain, false));
    EXPECT_TRUE(enabled);
    EXPECT_TRUE(requested);

    // Explicit failure followed by a late automatic success.
    enabled = false;
    epoch = 3;
    EXPECT_TRUE(DaoDevToolsTools::ApplyDomainEnableResultForTesting(
        session.get(), 7, state.bound_host.get(), 3, network_domain, false));
    EXPECT_TRUE(DaoDevToolsTools::ApplyDomainEnableResultForTesting(
        session.get(), 7, state.bound_host.get(), 2, network_domain, true));
    EXPECT_TRUE(enabled);

    // Two failures without any success remain unconfirmed.
    enabled = false;
    epoch = 5;
    EXPECT_TRUE(DaoDevToolsTools::ApplyDomainEnableResultForTesting(
        session.get(), 7, state.bound_host.get(), 4, network_domain, false));
    EXPECT_TRUE(DaoDevToolsTools::ApplyDomainEnableResultForTesting(
        session.get(), 7, state.bound_host.get(), 5, network_domain, false));
    EXPECT_FALSE(enabled);
    EXPECT_TRUE(requested);
  }
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       EventsBeforeEnableResponseCommitForBothDomains) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  struct DomainCase {
    const char* tool;
    const char* command;
    const char* event;
    bool network;
  };
  constexpr DomainCase kCases[] = {
      {"enable_network_tracking", "Network.enable",
       R"({"method":"Network.requestWillBeSent","params":{"requestId":"staged-network","request":{"url":"https://staged.test/network","method":"GET"},"type":"Fetch","timestamp":1}})",
       true},
      {"enable_console_tracking", "Runtime.enable",
       R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"staged-runtime"}],"timestamp":1}})",
       false},
  };
  for (const DomainCase& domain : kCases) {
    auto session = MakeSession();
    bool injected = false;
    devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
        [](DaoDevToolsClient* client, const DomainCase* domain, bool* injected,
           const std::string& method) {
          if (*injected || method != domain->command) {
            return;
          }
          *injected = true;
          client->DispatchProtocolMessage(
              client->agent_host(),
              base::as_byte_span(std::string_view(domain->event)));
        },
        devtools_client_.get(), &domain, &injected));

    DaoBrowserToolResult result = Execute(session.get(), domain.tool);
    ASSERT_TRUE(result.ok) << result.error->message;
    const auto& state = session->devtools_state();
    EXPECT_TRUE(domain.network ? state.network_tracking_enabled
                               : state.console_tracking_enabled);
    const bool found =
        domain.network
            ? std::ranges::any_of(state.network_requests,
                                  [](const base::DictValue& entry) {
                                    return entry.FindString("url") &&
                                           *entry.FindString("url") ==
                                               "https://staged.test/network";
                                  })
            : std::ranges::any_of(
                  state.console_messages, [](const base::DictValue& entry) {
                    return entry.FindString("text") &&
                           *entry.FindString("text") == "staged-runtime";
                  });
    EXPECT_TRUE(found);
  }
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       EventsBeforeAllEnableFailuresAreDropped) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  for (
      const auto& [tool, command, event, network] :
      std::array<std::tuple<const char*, const char*, const char*, bool>, 2>{
          std::tuple{
              "enable_network_tracking", "Network.enable",
              R"({"method":"Network.requestWillBeSent","params":{"requestId":"failed-network","request":{"url":"https://staged.test/failed","method":"GET"},"type":"Fetch","timestamp":1}})",
              true},
          std::tuple{
              "enable_console_tracking", "Runtime.enable",
              R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"failed-runtime"}],"timestamp":1}})",
              false},
      }) {
    auto session = MakeSession();
    bool injected = false;
    devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
        [](DaoDevToolsClient* client, const char* command, const char* event,
           bool* injected, const std::string& method) {
          if (*injected || method != command) {
            return;
          }
          *injected = true;
          client->DispatchProtocolMessage(
              client->agent_host(),
              base::as_byte_span(std::string_view(event)));
          client->CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                             "Injected enable failure."));
        },
        devtools_client_.get(), command, event, &injected));

    DaoBrowserToolResult result = Execute(session.get(), tool);
    ASSERT_FALSE(result.ok);
    const auto& state = session->devtools_state();
    EXPECT_FALSE(network ? state.network_tracking_enabled
                         : state.console_tracking_enabled);
    EXPECT_TRUE(network ? state.network_requests.empty()
                        : state.console_messages.empty());
    EXPECT_TRUE(network ? state.staged_network_requests.empty()
                        : state.staged_console_messages.empty());
    EXPECT_TRUE(network ? state.network_pending_enable_attempts.empty()
                        : state.console_pending_enable_attempts.empty());
  }
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       WrongHostEventsNeverEnterEnableStaging) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  chrome::AddTabAt(browser(), second_url(), 1, false);
  content::WebContents* other_target =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  ASSERT_TRUE(content::WaitForLoadStop(other_target));
  scoped_refptr<content::DevToolsAgentHost> wrong_host =
      content::DevToolsAgentHost::GetOrCreateFor(other_target);
  ASSERT_TRUE(wrong_host);

  for (
      const auto& [tool, command, event, network] :
      std::array<std::tuple<const char*, const char*, const char*, bool>, 2>{
          std::tuple{
              "enable_network_tracking", "Network.enable",
              R"({"method":"Network.requestWillBeSent","params":{"requestId":"wrong-host-network","request":{"url":"https://wrong-host.test/network","method":"GET"},"type":"Fetch","timestamp":1}})",
              true},
          std::tuple{
              "enable_console_tracking", "Runtime.enable",
              R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"wrong-host-runtime"}],"timestamp":1}})",
              false},
      }) {
    auto session = MakeSession();
    bool injected = false;
    devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
        [](DaoDevToolsClient* client,
           scoped_refptr<content::DevToolsAgentHost> wrong_host,
           const char* command, const char* event, bool* injected,
           const std::string& method) {
          if (*injected || method != command) {
            return;
          }
          *injected = true;
          client->DispatchProtocolMessage(
              wrong_host.get(), base::as_byte_span(std::string_view(event)));
        },
        devtools_client_.get(), wrong_host, command, event, &injected));

    DaoBrowserToolResult result = Execute(session.get(), tool);
    ASSERT_TRUE(result.ok) << result.error->message;
    const auto& state = session->devtools_state();
    const bool found =
        network
            ? std::ranges::any_of(
                  state.network_requests,
                  [](const base::DictValue& entry) {
                    return entry.FindString("url") &&
                           *entry.FindString("url") ==
                               "https://wrong-host.test/network";
                  })
            : std::ranges::any_of(
                  state.console_messages, [](const base::DictValue& entry) {
                    return entry.FindString("text") &&
                           *entry.FindString("text") == "wrong-host-runtime";
                  });
    EXPECT_FALSE(found);
  }
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       RebindDropsEventsStagedByOldGeneration) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* first_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome::AddTabAt(browser(), second_url(), 1, false);
  content::WebContents* second_target =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  ASSERT_TRUE(content::WaitForLoadStop(second_target));

  for (
      const auto& [tool, command, event, network] :
      std::array<std::tuple<const char*, const char*, const char*, bool>, 2>{
          std::tuple{
              "enable_network_tracking", "Network.enable",
              R"({"method":"Network.requestWillBeSent","params":{"requestId":"old-generation-network","request":{"url":"https://old-generation.test/network","method":"GET"},"type":"Fetch","timestamp":1}})",
              true},
          std::tuple{
              "enable_console_tracking", "Runtime.enable",
              R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"old-generation-runtime"}],"timestamp":1}})",
              false},
      }) {
    auto session = MakeSession(first_target);
    bool injected = false;
    uint64_t event_generation = 0;
    devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
        [](DaoDevToolsClient* client,
           base::WeakPtr<DaoBrowserAutomationSession> session,
           base::WeakPtr<content::WebContents> second_target,
           const char* command, const char* event, bool* injected,
           uint64_t* event_generation, const std::string& method) {
          if (*injected || method != command) {
            return;
          }
          *injected = true;
          *event_generation = session->devtools_state().binding_generation;
          client->DispatchProtocolMessage(
              client->agent_host(),
              base::as_byte_span(std::string_view(event)));
          if (session && second_target) {
            session->SetTarget(second_target.get());
          }
        },
        devtools_client_.get(), session->GetWeakPtr(),
        second_target->GetWeakPtr(), command, event, &injected,
        &event_generation));

    DaoBrowserToolResult result = Execute(session.get(), tool);
    ASSERT_FALSE(result.ok);
    EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
    base::RunLoop().RunUntilIdle();
    const auto& state = session->devtools_state();
    EXPECT_GT(state.binding_generation, event_generation);
    EXPECT_EQ(second_target, state.bound_target.get());
    EXPECT_EQ(devtools_client_->agent_host(), state.bound_host.get());
    EXPECT_TRUE(network ? state.staged_network_requests.empty()
                        : state.staged_console_messages.empty());
    const bool found =
        network ? std::ranges::any_of(
                      state.network_requests,
                      [](const base::DictValue& entry) {
                        return entry.FindString("url") &&
                               *entry.FindString("url") ==
                                   "https://old-generation.test/network";
                      })
                : std::ranges::any_of(state.console_messages,
                                      [](const base::DictValue& entry) {
                                        return entry.FindString("text") &&
                                               *entry.FindString("text") ==
                                                   "old-generation-runtime";
                                      });
    EXPECT_FALSE(found);
  }
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       RuntimeEnableReplayBeforeResponseIsRetained) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(target, "console.log('runtime-replay-before')"));
  auto session = MakeSession();

  DaoBrowserToolResult result =
      Execute(session.get(), "enable_console_tracking");
  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_TRUE(std::ranges::any_of(session->devtools_state().console_messages,
                                  [](const base::DictValue& entry) {
                                    return entry.FindString("text") &&
                                           *entry.FindString("text") ==
                                               "runtime-replay-before";
                                  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ClearNetworkDuringEnableDropsOnlyEarlierStaging) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  bool cleared = false;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](DaoMcpDevToolsBrowserTest* test, DaoBrowserAutomationSession* session,
         DaoDevToolsClient* client, bool* cleared, const std::string& method) {
        if (*cleared || method != "Network.enable") {
          return;
        }
        *cleared = true;
        client->DispatchProtocolMessage(
            client->agent_host(),
            base::as_byte_span(std::string_view(
                R"({"method":"Network.requestWillBeSent","params":{"requestId":"before-inflight-clear","request":{"url":"https://clear.test/before","method":"GET"},"type":"Fetch","timestamp":1}})")));
        auto& state = session->devtools_state();
        EXPECT_FALSE(state.staged_network_requests.empty());
        EXPECT_GT(state.staged_network_request_bytes, 0u);
        const size_t pending_attempts =
            state.network_pending_enable_attempts.size();

        DaoBrowserToolResult result =
            test->Execute(session, "clear_network_requests");
        ASSERT_TRUE(result.ok) << result.error->message;
        EXPECT_TRUE(state.staged_network_requests.empty());
        EXPECT_EQ(0u, state.staged_network_request_bytes);
        EXPECT_EQ(0u, state.staged_network_requests_dropped);
        EXPECT_EQ(0u, state.staged_network_fields_truncated);
        EXPECT_EQ(pending_attempts,
                  state.network_pending_enable_attempts.size());

        client->DispatchProtocolMessage(
            client->agent_host(),
            base::as_byte_span(std::string_view(
                R"({"method":"Network.requestWillBeSent","params":{"requestId":"after-inflight-clear","request":{"url":"https://clear.test/after","method":"GET"},"type":"Fetch","timestamp":2}})")));
      },
      base::Unretained(this), session.get(), devtools_client_.get(), &cleared));

  DaoBrowserToolResult result =
      Execute(session.get(), "enable_network_tracking");
  ASSERT_TRUE(result.ok) << result.error->message;
  const auto& state = session->devtools_state();
  EXPECT_FALSE(std::ranges::any_of(
      state.network_requests, [](const base::DictValue& entry) {
        return entry.FindString("url") &&
               *entry.FindString("url") == "https://clear.test/before";
      }));
  EXPECT_TRUE(std::ranges::any_of(
      state.network_requests, [](const base::DictValue& entry) {
        return entry.FindString("url") &&
               *entry.FindString("url") == "https://clear.test/after";
      }));
  EXPECT_TRUE(state.staged_network_requests.empty());
  EXPECT_TRUE(state.network_pending_enable_attempts.empty());
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ClearConsoleDuringEnableDropsOnlyEarlierStaging) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  bool cleared = false;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](DaoMcpDevToolsBrowserTest* test, DaoBrowserAutomationSession* session,
         DaoDevToolsClient* client, bool* cleared, const std::string& method) {
        if (*cleared || method != "Runtime.enable") {
          return;
        }
        *cleared = true;
        client->DispatchProtocolMessage(
            client->agent_host(),
            base::as_byte_span(std::string_view(
                R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"before-inflight-clear"}],"timestamp":1}})")));
        auto& state = session->devtools_state();
        EXPECT_FALSE(state.staged_console_messages.empty());
        EXPECT_GT(state.staged_console_message_bytes, 0u);
        const size_t pending_attempts =
            state.console_pending_enable_attempts.size();

        DaoBrowserToolResult result =
            test->Execute(session, "clear_console_messages");
        ASSERT_TRUE(result.ok) << result.error->message;
        EXPECT_TRUE(state.staged_console_messages.empty());
        EXPECT_EQ(0u, state.staged_console_message_bytes);
        EXPECT_EQ(0u, state.staged_console_messages_dropped);
        EXPECT_EQ(0u, state.staged_console_fields_truncated);
        EXPECT_EQ(pending_attempts,
                  state.console_pending_enable_attempts.size());

        client->DispatchProtocolMessage(
            client->agent_host(),
            base::as_byte_span(std::string_view(
                R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"after-inflight-clear"}],"timestamp":2}})")));
      },
      base::Unretained(this), session.get(), devtools_client_.get(), &cleared));

  DaoBrowserToolResult result =
      Execute(session.get(), "enable_console_tracking");
  ASSERT_TRUE(result.ok) << result.error->message;
  const auto& state = session->devtools_state();
  EXPECT_FALSE(std::ranges::any_of(
      state.console_messages, [](const base::DictValue& entry) {
        return entry.FindString("text") &&
               *entry.FindString("text") == "before-inflight-clear";
      }));
  EXPECT_TRUE(std::ranges::any_of(
      state.console_messages, [](const base::DictValue& entry) {
        return entry.FindString("text") &&
               *entry.FindString("text") == "after-inflight-clear";
      }));
  EXPECT_TRUE(state.staged_console_messages.empty());
  EXPECT_TRUE(state.console_pending_enable_attempts.empty());
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       CancelNetworkEnableCleansAttemptAndAllowsRetry) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  constexpr std::string_view kRequestId = "cancel-network-enable";
  bool cancelled = false;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](DaoBrowserToolExecutor* executor, DaoDevToolsClient* client,
         std::string_view request_id, bool* cancelled,
         const std::string& method) {
        if (*cancelled || method != "Network.enable") {
          return;
        }
        *cancelled = true;
        client->DispatchProtocolMessage(
            client->agent_host(),
            base::as_byte_span(std::string_view(
                R"({"method":"Network.requestWillBeSent","params":{"requestId":"cancelled-network","request":{"url":"https://cancel.test/network","method":"GET"},"type":"Fetch","timestamp":1}})")));
        executor->Cancel(request_id);
      },
      executor_.get(), devtools_client_.get(), kRequestId, &cancelled));

  DaoBrowserToolCall call;
  call.request_id = std::string(kRequestId);
  call.name = "enable_network_tracking";
  call.timeout = base::Seconds(30);
  base::test::TestFuture<DaoBrowserToolResult> future;
  int callback_count = 0;
  executor_->Execute(
      session.get(), DaoToolClient::kMcp, std::move(call),
      base::BindOnce(
          [](base::test::TestFuture<DaoBrowserToolResult>* future, int* count,
             DaoBrowserToolResult result) {
            ++*count;
            future->SetValue(std::move(result));
          },
          &future, &callback_count));
  DaoBrowserToolResult cancelled_result = future.Take();
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(cancelled_result.ok);
  ASSERT_TRUE(cancelled_result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, cancelled_result.error->code);
  EXPECT_TRUE(
      session->devtools_state().network_pending_enable_attempts.empty());
  EXPECT_TRUE(session->devtools_state().staged_network_requests.empty());
  EXPECT_FALSE(devtools_client_->has_pending_commands());
  EXPECT_EQ(1, callback_count);

  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
  DaoBrowserToolResult retried =
      Execute(session.get(), "enable_network_tracking");
  ASSERT_TRUE(retried.ok) << retried.error->message;
  devtools_client_->DispatchProtocolMessage(
      devtools_client_->agent_host(),
      base::as_byte_span(std::string_view(
          R"({"method":"Network.requestWillBeSent","params":{"requestId":"retried-network","request":{"url":"https://cancel.test/retried-network","method":"GET"},"type":"Fetch","timestamp":2}})")));
  EXPECT_TRUE(
      std::ranges::any_of(session->devtools_state().network_requests,
                          [](const base::DictValue& entry) {
                            return entry.FindString("url") &&
                                   *entry.FindString("url") ==
                                       "https://cancel.test/retried-network";
                          }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       CancelConsoleEnableCleansAttemptAndAllowsRetry) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  constexpr std::string_view kRequestId = "cancel-console-enable";
  bool cancelled = false;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](DaoBrowserToolExecutor* executor, DaoDevToolsClient* client,
         std::string_view request_id, bool* cancelled,
         const std::string& method) {
        if (*cancelled || method != "Runtime.enable") {
          return;
        }
        *cancelled = true;
        client->DispatchProtocolMessage(
            client->agent_host(),
            base::as_byte_span(std::string_view(
                R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"cancelled-console"}],"timestamp":1}})")));
        executor->Cancel(request_id);
      },
      executor_.get(), devtools_client_.get(), kRequestId, &cancelled));

  DaoBrowserToolCall call;
  call.request_id = std::string(kRequestId);
  call.name = "enable_console_tracking";
  call.timeout = base::Seconds(30);
  base::test::TestFuture<DaoBrowserToolResult> future;
  int callback_count = 0;
  executor_->Execute(
      session.get(), DaoToolClient::kMcp, std::move(call),
      base::BindOnce(
          [](base::test::TestFuture<DaoBrowserToolResult>* future, int* count,
             DaoBrowserToolResult result) {
            ++*count;
            future->SetValue(std::move(result));
          },
          &future, &callback_count));
  DaoBrowserToolResult cancelled_result = future.Take();
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(cancelled_result.ok);
  ASSERT_TRUE(cancelled_result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, cancelled_result.error->code);
  EXPECT_TRUE(
      session->devtools_state().console_pending_enable_attempts.empty());
  EXPECT_TRUE(session->devtools_state().staged_console_messages.empty());
  EXPECT_FALSE(devtools_client_->has_pending_commands());
  EXPECT_EQ(1, callback_count);

  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
  DaoBrowserToolResult retried =
      Execute(session.get(), "enable_console_tracking");
  ASSERT_TRUE(retried.ok) << retried.error->message;
  devtools_client_->DispatchProtocolMessage(
      devtools_client_->agent_host(),
      base::as_byte_span(std::string_view(
          R"({"method":"Runtime.consoleAPICalled","params":{"type":"log","args":[{"type":"string","value":"retried-console"}],"timestamp":2}})")));
  EXPECT_TRUE(std::ranges::any_of(session->devtools_state().console_messages,
                                  [](const base::DictValue& entry) {
                                    return entry.FindString("text") &&
                                           *entry.FindString("text") ==
                                               "retried-console";
                                  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       SyntheticResourceTraversalIsBoundedAndExplicit) {
  base::ListValue resources;
  resources.reserve(10001);
  for (int index = 0; index < 10001; ++index) {
    resources.Append(
        base::DictValue()
            .Set("url", "https://example.test/r/" + base::NumberToString(index))
            .Set("type", "Script")
            .Set("mimeType", "application/javascript"));
  }
  base::DictValue tree;
  tree.Set("frame", base::DictValue()
                        .Set("id", "main")
                        .Set("url", "https://example.test/")
                        .Set("mimeType", "text/html"));
  tree.Set("resources", std::move(resources));

  base::DictValue result =
      DaoDevToolsTools::TraverseResourceTreeForTesting(tree, "all");
  const base::ListValue* bounded = result.FindList("resources");
  ASSERT_NE(nullptr, bounded);
  EXPECT_EQ(10000u, bounded->size());
  EXPECT_TRUE(result.FindBool("resource_limit_hit").value_or(false));
  EXPECT_GT(result.FindDouble("skipped").value_or(0), 0);
  EXPECT_EQ("Document", *(*bounded)[0].GetDict().FindString("type"));

  base::ListValue long_urls;
  for (int index = 0; index < 200; ++index) {
    long_urls.Append(base::DictValue()
                         .Set("url", "https://example.test/" +
                                         std::string(12000, 'u') +
                                         base::NumberToString(index))
                         .Set("type", "Script")
                         .Set("mimeType", "application/javascript"));
  }
  base::DictValue url_tree;
  url_tree.Set("frame", base::DictValue()
                            .Set("id", "url-frame")
                            .Set("url", "https://example.test/")
                            .Set("mimeType", "text/html"));
  url_tree.Set("resources", std::move(long_urls));
  base::DictValue url_result =
      DaoDevToolsTools::TraverseResourceTreeForTesting(url_tree, "all");
  EXPECT_TRUE(url_result.FindBool("resource_limit_hit").value_or(false));
  EXPECT_LE(url_result.FindDouble("url_bytes").value_or(1e9), 2 * 1024 * 1024);
  EXPECT_LT(url_result.FindList("resources")->size(), 10000u);

  base::DictValue deep_tree;
  deep_tree.Set("frame", base::DictValue()
                             .Set("id", "depth-65")
                             .Set("url", "https://example.test/depth-65")
                             .Set("mimeType", "text/html"));
  for (int depth = 64; depth >= 0; --depth) {
    base::ListValue children;
    children.Append(std::move(deep_tree));
    base::DictValue parent;
    parent.Set("frame", base::DictValue()
                            .Set("id", "depth-" + base::NumberToString(depth))
                            .Set("url", "https://example.test/depth-" +
                                            base::NumberToString(depth))
                            .Set("mimeType", "text/html"));
    parent.Set("childFrames", std::move(children));
    deep_tree = std::move(parent);
  }
  base::DictValue depth_result =
      DaoDevToolsTools::TraverseResourceTreeForTesting(deep_tree, "all");
  EXPECT_TRUE(depth_result.FindBool("resource_limit_hit").value_or(false));
  EXPECT_EQ(65u, depth_result.FindList("resources")->size());

  base::ListValue duplicates;
  for (int index = 0; index < 3; ++index) {
    duplicates.Append(base::DictValue()
                          .Set("url", "https://example.test/duplicate.js")
                          .Set("type", "Script")
                          .Set("mimeType", "application/javascript"));
  }
  base::DictValue duplicate_tree;
  duplicate_tree.Set("frame", base::DictValue()
                                  .Set("id", "duplicate-frame")
                                  .Set("url", "https://example.test/")
                                  .Set("mimeType", "text/html"));
  duplicate_tree.Set("resources", std::move(duplicates));
  base::DictValue duplicate_result =
      DaoDevToolsTools::TraverseResourceTreeForTesting(duplicate_tree, "all");
  EXPECT_EQ(2u, duplicate_result.FindList("resources")->size());
  EXPECT_EQ(2, duplicate_result.FindDouble("skipped").value_or(0));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       CapturesNetworkRequestsAndRetrievesResponseBody) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  DaoBrowserToolResult enabled =
      Execute(session.get(), "enable_network_tracking");
  ASSERT_TRUE(enabled.ok) << enabled.error->message;

  ASSERT_EQ("dao-network-body",
            content::EvalJs(
                browser()->tab_strip_model()->GetActiveWebContents(),
                "fetch('/api-body').then(r => r.json()).then(v => v.source)")
                .ExtractString());

  std::string request_id;
  ASSERT_TRUE(base::test::RunUntil([&] {
    DaoBrowserToolResult captured =
        Execute(session.get(), "get_network_requests");
    if (!captured.ok || !captured.data.is_dict()) {
      return false;
    }
    const base::ListValue* requests =
        captured.data.GetDict().FindList("requests");
    if (!requests) {
      return false;
    }
    for (const base::Value& value : *requests) {
      const base::DictValue* entry = value.GetIfDict();
      const std::string* url = entry ? entry->FindString("url") : nullptr;
      const std::string* phase = entry ? entry->FindString("phase") : nullptr;
      const std::string* id = entry ? entry->FindString("request_id") : nullptr;
      if (url && phase && id &&
          *url == embedded_test_server()->GetURL("/api-body").spec() &&
          *phase == "response") {
        request_id = *id;
        return true;
      }
    }
    return false;
  }));

  DaoBrowserToolResult body =
      Execute(session.get(), "get_network_body",
              base::DictValue().Set("request_id", request_id));
  ASSERT_TRUE(body.ok) << body.error->message;
  ASSERT_TRUE(body.data.is_dict());
  EXPECT_EQ(request_id, *body.data.GetDict().FindString("request_id"));
  EXPECT_EQ(R"({"source":"dao-network-body"})",
            *body.data.GetDict().FindString("body"));
  EXPECT_FALSE(body.data.GetDict().FindBool("base64_encoded").value_or(true));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       WaitsForMatchingNetworkJsonAfterCursor) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  DaoBrowserToolResult enabled =
      Execute(session.get(), "enable_network_tracking");
  ASSERT_TRUE(enabled.ok) << enabled.error->message;
  const double cursor =
      enabled.data.GetDict().FindDouble("cursor").value_or(-1);
  ASSERT_GE(cursor, 0);

  const GURL response_url = embedded_test_server()->GetURL("/api-body?wait=1");
  base::ListValue any_of;
  any_of.Append("dao-network-body");
  base::ListValue select;
  select.Append("$.source");
  base::test::TestFuture<DaoBrowserToolResult> future;
  int callback_count = 0;
  ExecuteAsync(session.get(), "wait_for_network_response",
               base::DictValue()
                   .Set("cursor", cursor)
                   .Set("url_pattern", response_url.spec())
                   .Set("method", "GET")
                   .Set("status", 200)
                   .Set("json_path", "$.source")
                   .Set("any_of", std::move(any_of))
                   .Set("select", std::move(select))
                   .Set("timeout_ms", 5000),
               &future, &callback_count);

  ASSERT_EQ(
      "dao-network-body",
      content::EvalJs(browser()->tab_strip_model()->GetActiveWebContents(),
                      "fetch('/api-body?wait=1').then(r => r.json()).then(v => "
                      "v.source)")
          .ExtractString());
  DaoBrowserToolResult result = future.Take();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(result.ok) << result.error->message;
  const base::DictValue& data = result.data.GetDict();
  EXPECT_EQ(response_url.spec(), *data.FindString("url"));
  EXPECT_GT(data.FindDouble("next_cursor").value_or(cursor), cursor);
  ASSERT_NE(nullptr, data.FindDict("selected"));
  EXPECT_EQ("dao-network-body",
            *data.FindDict("selected")->FindString("$.source"));
  EXPECT_EQ(1, callback_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       NetworkBodiesCapTextAndPreserveBase64) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  ASSERT_TRUE(Execute(session.get(), "enable_network_tracking").ok);
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();

  ASSERT_TRUE(content::EvalJs(target, R"(
    Promise.all(['/api-large-body', '/api-binary-body'].map((url) =>
      new Promise((resolve, reject) => {
        const request = new XMLHttpRequest();
        request.open('GET', url);
        request.onload = () => resolve(request.response);
        request.onerror = reject;
        request.send();
      }))).then(() => true)
  )")
                  .ExtractBool());

  std::string large_request_id;
  std::string binary_request_id;
  ASSERT_TRUE(WaitForResponseRequestId(
      session.get(), embedded_test_server()->GetURL("/api-large-body"),
      &large_request_id));
  ASSERT_TRUE(WaitForResponseRequestId(
      session.get(), embedded_test_server()->GetURL("/api-binary-body"),
      &binary_request_id));

  DaoBrowserToolResult large =
      Execute(session.get(), "get_network_body",
              base::DictValue().Set("request_id", large_request_id));
  ASSERT_TRUE(large.ok) << large.error->message;
  const std::string* large_body = large.data.GetDict().FindString("body");
  ASSERT_NE(nullptr, large_body);
  EXPECT_FALSE(large.data.GetDict().FindBool("base64_encoded").value_or(true));
  EXPECT_TRUE(large.data.GetDict().FindBool("truncated").value_or(false));
  EXPECT_EQ(kExpectedTextLimit - 1 + std::string("\n...[truncated]").size(),
            large_body->size());
  EXPECT_TRUE(base::IsStringUTF8(*large_body));
  EXPECT_TRUE(base::EndsWith(*large_body, "\n...[truncated]"));

  DaoBrowserToolResult binary =
      Execute(session.get(), "get_network_body",
              base::DictValue().Set("request_id", binary_request_id));
  ASSERT_TRUE(binary.ok) << binary.error->message;
  EXPECT_TRUE(binary.data.GetDict().FindBool("base64_encoded").value_or(false));
  EXPECT_FALSE(binary.data.GetDict().FindBool("truncated").value_or(false));
  const std::string* binary_body = binary.data.GetDict().FindString("body");
  ASSERT_NE(nullptr, binary_body);
  EXPECT_EQ(kTinyPngBase64, *binary_body);
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       EnforcesNetworkAndConsoleBufferCaps) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  ASSERT_TRUE(Execute(session.get(), "enable_network_tracking").ok);
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();

  ASSERT_TRUE(content::EvalJs(target, R"(
    Promise.all(Array.from(
      {length: 220}, (_, i) => fetch('/api-body?cap=' + i)))
      .then(() => true)
  )")
                  .ExtractBool());
  ASSERT_TRUE(content::ExecJs(target, R"(
    for (let i = 0; i < 510; ++i) console.log('dao-console-cap-' + i);
  )"));

  ASSERT_TRUE(base::test::RunUntil([&] {
    DaoBrowserToolResult requests =
        Execute(session.get(), "get_network_requests");
    DaoBrowserToolResult messages =
        Execute(session.get(), "get_console_messages");
    return requests.ok && messages.ok &&
           requests.data.GetDict().FindInt("count").value_or(0) == 200 &&
           messages.data.GetDict().FindInt("count").value_or(0) == 500;
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ClearHandlersPreserveTrackingAndCaptureNewEvents) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  ASSERT_TRUE(Execute(session.get(), "enable_network_tracking").ok);
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();

  ASSERT_TRUE(content::ExecJs(target, "console.log('before-clear')"));
  ASSERT_TRUE(content::EvalJs(
                  target, "fetch('/api-body?before-clear').then(() => true)")
                  .ExtractBool());
  ASSERT_TRUE(base::test::RunUntil([&] {
    DaoBrowserToolResult requests =
        Execute(session.get(), "get_network_requests");
    DaoBrowserToolResult messages =
        Execute(session.get(), "get_console_messages");
    return requests.ok && messages.ok &&
           requests.data.GetDict().FindInt("count").value_or(0) > 0 &&
           messages.data.GetDict().FindInt("count").value_or(0) > 0;
  }));

  ASSERT_TRUE(Execute(session.get(), "clear_network_requests").ok);
  ASSERT_TRUE(Execute(session.get(), "clear_console_messages").ok);
  DaoBrowserToolResult requests =
      Execute(session.get(), "get_network_requests");
  DaoBrowserToolResult messages =
      Execute(session.get(), "get_console_messages");
  ASSERT_TRUE(requests.ok);
  ASSERT_TRUE(messages.ok);
  EXPECT_EQ(0, requests.data.GetDict().FindInt("count").value_or(-1));
  EXPECT_EQ(0, messages.data.GetDict().FindInt("count").value_or(-1));
  EXPECT_TRUE(requests.data.GetDict().FindBool("enabled").value_or(false));
  EXPECT_TRUE(messages.data.GetDict().FindBool("enabled").value_or(false));

  ASSERT_TRUE(content::ExecJs(target, "console.log('after-clear')"));
  ASSERT_TRUE(
      content::EvalJs(target, "fetch('/api-body?after-clear').then(() => true)")
          .ExtractBool());
  ASSERT_TRUE(base::test::RunUntil([&] {
    requests = Execute(session.get(), "get_network_requests");
    messages = Execute(session.get(), "get_console_messages");
    return requests.ok && messages.ok &&
           requests.data.GetDict().FindInt("count").value_or(0) > 0 &&
           messages.data.GetDict().FindInt("count").value_or(0) > 0;
  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       CapturesAndFiltersConsoleMessages) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(target,
                              "console.log('dao-console-log');"
                              "console.error('dao-console-error');"));

  DaoBrowserToolResult filtered;
  ASSERT_TRUE(base::test::RunUntil([&] {
    filtered = Execute(session.get(), "get_console_messages",
                       base::DictValue().Set("filter", "error"));
    if (!filtered.ok) {
      return false;
    }
    const base::ListValue* messages =
        filtered.data.GetDict().FindList("messages");
    return messages && FindEntry(*messages, "text", "dao-console-error");
  }));
  EXPECT_TRUE(filtered.data.GetDict().FindBool("enabled").value_or(false));
  const base::ListValue* messages =
      filtered.data.GetDict().FindList("messages");
  ASSERT_NE(nullptr, messages);
  EXPECT_EQ(nullptr, FindEntry(*messages, "text", "dao-console-log"));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       TrackingBuffersRetainRecentBoundedUtf8Entries) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);
  ASSERT_TRUE(Execute(session.get(), "enable_network_tracking").ok);
  auto dispatch_event = [this](base::DictValue event) {
    std::string serialized;
    CHECK(base::JSONWriter::Write(event, &serialized));
    devtools_client_->DispatchProtocolMessage(devtools_client_->agent_host(),
                                              base::as_byte_span(serialized));
  };
  auto dispatch_console = [&dispatch_event](std::string value,
                                            double timestamp) {
    base::ListValue args;
    args.Append(
        base::DictValue().Set("type", "string").Set("value", std::move(value)));
    dispatch_event(base::DictValue()
                       .Set("method", "Runtime.consoleAPICalled")
                       .Set("params", base::DictValue()
                                          .Set("type", "log")
                                          .Set("args", std::move(args))
                                          .Set("timestamp", timestamp)));
  };
  std::string multibyte;
  multibyte.reserve(20000 * 4);
  for (int index = 0; index < 20000; ++index) {
    multibyte.append("\xF0\x9F\x98\x80");
  }
  dispatch_console(std::move(multibyte), 1);
  for (int index = 0; index < 70; ++index) {
    dispatch_console("aggregate-" + base::NumberToString(index) + "-" +
                         std::string(16000, 'x'),
                     index + 2);
  }
  dispatch_console("dao-console-sentinel", 72);

  auto dispatch_network = [&dispatch_event](std::string request_id,
                                            std::string url, double timestamp) {
    dispatch_event(
        base::DictValue()
            .Set("method", "Network.requestWillBeSent")
            .Set("params", base::DictValue()
                               .Set("requestId", std::move(request_id))
                               .Set("request", base::DictValue()
                                                   .Set("url", std::move(url))
                                                   .Set("method", "GET"))
                               .Set("type", "Fetch")
                               .Set("timestamp", timestamp)));
  };
  for (int index = 0; index < 40; ++index) {
    const std::string index_string = base::NumberToString(index);
    dispatch_network("bounded-" + index_string,
                     "https://bounded.test/api?index=" + index_string +
                         "&long=" + std::string(16000, 'x'),
                     index + 1);
  }
  dispatch_network("dao-network-sentinel",
                   "https://bounded.test/api?dao-network-sentinel", 41);

  DaoBrowserToolResult console;
  ASSERT_TRUE(base::test::RunUntil([&] {
    console = Execute(session.get(), "get_console_messages");
    const base::ListValue* messages =
        console.ok ? console.data.GetDict().FindList("messages") : nullptr;
    return messages && FindEntry(*messages, "text", "dao-console-sentinel");
  }));
  EXPECT_LE(console.data.GetDict().FindDouble("stored_bytes").value_or(1e9),
            1024 * 1024);
  EXPECT_GT(console.data.GetDict().FindDouble("dropped_count").value_or(0), 0);
  EXPECT_GT(
      console.data.GetDict().FindDouble("truncated_field_count").value_or(0),
      0);
  EXPECT_LT(console.data.GetDict().FindList("messages")->size(), 500u);
  for (const base::Value& message :
       *console.data.GetDict().FindList("messages")) {
    const std::string* text = message.GetDict().FindString("text");
    EXPECT_TRUE(!text || base::IsStringUTF8(*text));
  }

  DaoBrowserToolResult network;
  ASSERT_TRUE(base::test::RunUntil([&] {
    network = Execute(session.get(), "get_network_requests");
    const base::ListValue* requests =
        network.ok ? network.data.GetDict().FindList("requests") : nullptr;
    return requests &&
           std::ranges::any_of(*requests, [](const base::Value& value) {
             const std::string* url = value.GetDict().FindString("url");
             return url &&
                    url->find("dao-network-sentinel") != std::string::npos;
           });
  }));
  EXPECT_LE(network.data.GetDict().FindDouble("stored_bytes").value_or(1e9),
            512 * 1024);
  EXPECT_GT(network.data.GetDict().FindDouble("dropped_count").value_or(0), 0);
  EXPECT_LT(network.data.GetDict().FindList("requests")->size(), 200u);
  EXPECT_TRUE(std::ranges::any_of(*network.data.GetDict().FindList("requests"),
                                  [](const base::Value& value) {
                                    const std::string* url =
                                        value.GetDict().FindString("url");
                                    return url && url->size() > 15000;
                                  }));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ListsResourcesAndPreservesTextAndBinaryContracts) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();

  DaoBrowserToolResult listed = Execute(session.get(), "list_page_resources");
  ASSERT_TRUE(listed.ok) << listed.error->message;
  const base::ListValue* resources =
      listed.data.GetDict().FindList("resources");
  ASSERT_NE(nullptr, resources);
  const std::string large_url =
      embedded_test_server()->GetURL("/large.js").spec();
  const std::string binary_url =
      embedded_test_server()->GetURL("/binary.png").spec();
  const base::DictValue* large = FindEntry(*resources, "url", large_url);
  const base::DictValue* binary = FindEntry(*resources, "url", binary_url);
  ASSERT_NE(nullptr, large);
  ASSERT_NE(nullptr, binary);
  EXPECT_EQ("Script", *large->FindString("type"));
  EXPECT_EQ("Image", *binary->FindString("type"));

  DaoBrowserToolResult text =
      Execute(session.get(), "get_resource_content",
              base::DictValue()
                  .Set("url", large_url)
                  .Set("frame_id", *large->FindString("frameId")));
  ASSERT_TRUE(text.ok) << text.error->message;
  const std::string* content = text.data.GetDict().FindString("content");
  ASSERT_NE(nullptr, content);
  EXPECT_TRUE(text.data.GetDict().FindBool("truncated").value_or(false));
  EXPECT_FALSE(text.data.GetDict().FindBool("base64_encoded").value_or(true));
  EXPECT_EQ(kExpectedTextLimit - 1 + std::string("\n...[truncated]").size(),
            content->size());
  EXPECT_TRUE(base::IsStringUTF8(*content));
  EXPECT_TRUE(base::EndsWith(*content, "\n...[truncated]"));

  DaoBrowserToolResult binary_content =
      Execute(session.get(), "get_resource_content",
              base::DictValue()
                  .Set("url", binary_url)
                  .Set("frame_id", *binary->FindString("frameId")));
  ASSERT_TRUE(binary_content.ok) << binary_content.error->message;
  EXPECT_TRUE(
      binary_content.data.GetDict().FindBool("base64_encoded").value_or(false));
  EXPECT_FALSE(
      binary_content.data.GetDict().FindBool("truncated").value_or(false));
  EXPECT_FALSE(binary_content.data.GetDict().FindString("content")->empty());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ListsAndSearchesEachFrameDocumentExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  DaoBrowserToolResult listed =
      Execute(session.get(), "list_page_resources",
              base::DictValue().Set("type_filter", "Document"));
  ASSERT_TRUE(listed.ok) << listed.error->message;
  const base::ListValue* resources =
      listed.data.GetDict().FindList("resources");
  ASSERT_NE(nullptr, resources);
  const std::string main_url = page_url().spec();
  const std::string frame_url =
      embedded_test_server()->GetURL("/frame.html").spec();
  int main_count = 0;
  int frame_count = 0;
  for (const base::Value& value : *resources) {
    const base::DictValue& entry = value.GetDict();
    EXPECT_EQ("Document", *entry.FindString("type"));
    main_count += *entry.FindString("url") == main_url;
    frame_count += *entry.FindString("url") == frame_url;
  }
  EXPECT_EQ(1, main_count);
  EXPECT_EQ(1, frame_count);

  DaoBrowserToolResult searched =
      Execute(session.get(), "search_in_resources",
              base::DictValue()
                  .Set("pattern", "dao nested document token")
                  .Set("types", "Document"));
  ASSERT_TRUE(searched.ok) << searched.error->message;
  const base::ListValue* matches = searched.data.GetDict().FindList("matches");
  ASSERT_NE(nullptr, matches);
  ASSERT_EQ(1u, matches->size());
  EXPECT_EQ(frame_url, *(*matches)[0].GetDict().FindString("url"));
  EXPECT_FALSE((*matches)[0].GetDict().FindString("frame_id")->empty());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ResourceSearchSeparatesSourceAndMatchLimits) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  for (const std::string& pattern :
       std::array<std::string, 2>{R"(\[truncated\])", "post-cutoff-hit"}) {
    DaoBrowserToolResult result = Execute(session.get(), "search_in_resources",
                                          base::DictValue()
                                              .Set("pattern", pattern)
                                              .Set("types", "Script")
                                              .Set("max_matches", 10));
    ASSERT_TRUE(result.ok) << result.error->message;
    EXPECT_TRUE(result.data.GetDict().FindList("matches")->empty());
    EXPECT_FALSE(result.data.GetDict().FindBool("truncated").value_or(true));
    EXPECT_TRUE(
        result.data.GetDict().FindBool("source_truncated").value_or(false));
    EXPECT_EQ(static_cast<double>(kExpectedTextLimit),
              result.data.GetDict().FindDouble("source_limit").value_or(0));
  }
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ResourceSearchReportsAggregateScanLimit) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/aggregate-search.html")));
  auto session = MakeSession();
  DaoBrowserToolResult result =
      Execute(session.get(), "search_in_resources",
              base::DictValue()
                  .Set("pattern", "definitely-not-present")
                  .Set("types", "Script"));

  ASSERT_TRUE(result.ok) << result.error->message;
  const base::DictValue& data = result.data.GetDict();
  EXPECT_TRUE(data.FindList("matches")->empty());
  EXPECT_TRUE(data.FindBool("scan_limit_hit").value_or(false));
  EXPECT_TRUE(data.FindBool("incomplete").value_or(false));
  EXPECT_EQ(4 * 1024 * 1024, data.FindDouble("scanned_bytes").value_or(0));
  EXPECT_EQ(8, data.FindInt("attempted").value_or(0));
  EXPECT_EQ(8, data.FindInt("searched").value_or(0));
  EXPECT_EQ(9, data.FindDouble("eligible").value_or(0));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       OversizedScriptMakesSearchExplicitlyIncomplete) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/oversize-page.html")));
  auto session = MakeSession();
  base::test::TestFuture<DaoBrowserToolResult> result_future;
  int callback_count = 0;
  ExecuteAsync(session.get(), "search_in_resources",
               base::DictValue()
                   .Set("pattern", "not-present-after-oversize")
                   .Set("types", "Script"),
               &result_future, &callback_count);
  DaoBrowserToolResult result = result_future.Take();
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(result.ok) << result.error->message;
  const base::DictValue& data = result.data.GetDict();
  EXPECT_TRUE(data.FindList("matches")->empty());
  EXPECT_TRUE(data.FindBool("incomplete").value_or(false));
  EXPECT_TRUE(data.FindBool("source_limit_hit").value_or(false));
  EXPECT_EQ(1, data.FindInt("fetch_failed").value_or(0));
  EXPECT_EQ(1, data.FindInt("content_limit_failures").value_or(0));
  EXPECT_EQ(1, callback_count);
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       OversizedDocumentMakesSearchExplicitlyIncomplete) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/oversize-document.html")));
  auto session = MakeSession();
  DaoBrowserToolResult result = Execute(session.get(), "search_in_resources",
                                        base::DictValue()
                                            .Set("pattern", "document-tail-hit")
                                            .Set("types", "Document"));

  ASSERT_TRUE(result.ok) << result.error->message;
  const base::DictValue& data = result.data.GetDict();
  EXPECT_TRUE(data.FindList("matches")->empty());
  EXPECT_TRUE(data.FindBool("incomplete").value_or(false));
  EXPECT_TRUE(data.FindBool("source_limit_hit").value_or(false));
  EXPECT_EQ(1, data.FindInt("fetch_failed").value_or(0));
  EXPECT_EQ(1, data.FindInt("content_limit_failures").value_or(0));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       DocumentRuntimeExceptionMakesSearchIncomplete) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(content::ExecJs(target, "document.documentElement.remove()"));
  auto session = MakeSession();
  DaoBrowserToolResult result = Execute(
      session.get(), "search_in_resources",
      base::DictValue().Set("pattern", "anything").Set("types", "Document"));

  ASSERT_TRUE(result.ok) << result.error->message;
  const base::DictValue& data = result.data.GetDict();
  EXPECT_TRUE(data.FindList("matches")->empty());
  EXPECT_TRUE(data.FindBool("incomplete").value_or(false));
  EXPECT_FALSE(data.FindBool("source_limit_hit").value_or(true));
  EXPECT_EQ(1, data.FindInt("fetch_failed").value_or(0));
  EXPECT_EQ(0, data.FindInt("content_limit_failures").value_or(-1));
  EXPECT_EQ(0, data.FindInt("searched").value_or(-1));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       OversizedResourceReturnsTypedErrorWithoutPayload) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/oversize-page.html")));
  auto session = MakeSession();
  DaoBrowserToolResult listed = Execute(session.get(), "list_page_resources");
  ASSERT_TRUE(listed.ok) << listed.error->message;
  const std::string url = embedded_test_server()->GetURL("/oversize.js").spec();
  const base::DictValue* resource =
      FindEntry(*listed.data.GetDict().FindList("resources"), "url", url);
  ASSERT_NE(nullptr, resource);

  std::vector<std::string> dispatched_methods;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](std::vector<std::string>* methods, const std::string& method) {
        methods->push_back(method);
      },
      &dispatched_methods));
  base::test::TestFuture<DaoBrowserToolResult> result_future;
  int callback_count = 0;
  ExecuteAsync(session.get(), "get_resource_content",
               base::DictValue()
                   .Set("url", url)
                   .Set("frame_id", *resource->FindString("frameId")),
               &result_future, &callback_count);
  DaoBrowserToolResult result = result_future.Take();
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_NE(std::string::npos, result.error->message.find("decoded limit"));
  EXPECT_TRUE(result.data.is_none());
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
  EXPECT_EQ((std::vector<std::string>{"Page.enable", "Page.getResourceTree"}),
            dispatched_methods);
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       BogusFrameIdNeverFallsBackToMatchingMainResource) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/oversize-page.html")));
  auto session = MakeSession();
  const std::string url = embedded_test_server()->GetURL("/oversize.js").spec();
  std::vector<std::string> dispatched_methods;
  devtools_client_->SetCommandCallbackForTesting(base::BindRepeating(
      [](std::vector<std::string>* methods, const std::string& method) {
        methods->push_back(method);
      },
      &dispatched_methods));

  DaoBrowserToolResult result =
      Execute(session.get(), "get_resource_content",
              base::DictValue().Set("url", url).Set("frame_id", "bogus-frame"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_NE(std::string::npos, result.error->message.find("does not own"));
  EXPECT_EQ((std::vector<std::string>{"Page.enable", "Page.getResourceTree"}),
            dispatched_methods);
  devtools_client_->SetCommandCallbackForTesting(
      DaoDevToolsClient::CommandCallbackForTesting());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       LargeNonResourceResponseHasNoResourceBudget) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(devtools_client_->AttachTo(target));

  constexpr size_t kLargeResponseBytes = 2 * 1024 * 1024 + 4096;
  base::test::TestFuture<DaoDevToolsClient::CommandResult> result_future;
  const int command_id = devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression",
               "'x'.repeat(" + base::NumberToString(kLargeResponseBytes) + ")")
          .Set("returnByValue", true),
      result_future.GetCallback());
  ASSERT_GT(command_id, 0);

  DaoDevToolsClient::CommandResult result = result_future.Take();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  const base::DictValue* runtime_result = result->GetDict().FindDict("result");
  ASSERT_NE(nullptr, runtime_result);
  const std::string* value = runtime_result->FindString("value");
  ASSERT_NE(nullptr, value);
  EXPECT_EQ(kLargeResponseBytes, value->size());
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       BudgetedResponseUsesIndependentPreparseBackstop) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(devtools_client_->AttachTo(target));

  constexpr size_t kResponseBudget = 2 * 1024 * 1024;
  constexpr size_t kLargeResponseBytes = kResponseBudget + 4096;
  base::test::TestFuture<DaoDevToolsClient::CommandResult> result_future;
  int callback_count = 0;
  const int command_id = devtools_client_->SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression",
               "'x'.repeat(" + base::NumberToString(kLargeResponseBytes) + ")")
          .Set("returnByValue", true),
      base::BindOnce(
          [](base::test::TestFuture<DaoDevToolsClient::CommandResult>* future,
             int* count, DaoDevToolsClient::CommandResult result) {
            ++*count;
            future->SetValue(std::move(result));
          },
          &result_future, &callback_count),
      kResponseBudget);
  ASSERT_GT(command_id, 0);

  DaoDevToolsClient::CommandResult result = result_future.Take();
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error().code);
  EXPECT_NE(std::string::npos,
            result.error().message.find("protocol response exceeds"));
  EXPECT_EQ(1, callback_count);
  EXPECT_EQ(0u, devtools_client_->pending_command_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       SearchesTextResourcesWithStableLimitsAndExcerpts) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();

  DaoBrowserToolResult result = Execute(session.get(), "search_in_resources",
                                        base::DictValue()
                                            .Set("pattern", "dao-search-hit")
                                            .Set("flags", "i")
                                            .Set("types", "Script,Image")
                                            .Set("max_matches", 2));
  ASSERT_TRUE(result.ok) << result.error->message;
  ASSERT_TRUE(result.data.is_dict());
  EXPECT_EQ("dao-search-hit", *result.data.GetDict().FindString("pattern"));
  EXPECT_EQ("i", *result.data.GetDict().FindString("flags"));
  EXPECT_TRUE(result.data.GetDict().FindBool("truncated").value_or(false));
  const base::ListValue* matches = result.data.GetDict().FindList("matches");
  ASSERT_NE(nullptr, matches);
  ASSERT_EQ(2u, matches->size());
  const base::DictValue& first = (*matches)[0].GetDict();
  const base::DictValue& second = (*matches)[1].GetDict();
  EXPECT_EQ(2, first.FindInt("line"));
  EXPECT_EQ("const firstHit = 'DAO-search-hit one';",
            *first.FindString("excerpt"));
  EXPECT_EQ(3, second.FindInt("line"));
  EXPECT_EQ(243u, second.FindString("excerpt")->size());
  EXPECT_TRUE(base::EndsWith(*second.FindString("excerpt"), "…"));
  for (const base::Value& match : *matches) {
    EXPECT_EQ(embedded_test_server()->GetURL("/search.js").spec(),
              *match.GetDict().FindString("url"));
  }

  DaoBrowserToolResult binary_only =
      Execute(session.get(), "search_in_resources",
              base::DictValue()
                  .Set("pattern", "dao-search-hit")
                  .Set("types", "Image,Font,Media,Manifest"));
  ASSERT_TRUE(binary_only.ok) << binary_only.error->message;
  EXPECT_EQ(0, binary_only.data.GetDict().FindInt("searched"));
  EXPECT_TRUE(binary_only.data.GetDict().FindList("matches")->empty());

  DaoBrowserToolResult defaults = Execute(session.get(), "search_in_resources",
                                          base::DictValue()
                                              .Set("pattern", "DAO-search-hit")
                                              .Set("flags", "")
                                              .Set("types", "")
                                              .Set("max_matches", 1));
  ASSERT_TRUE(defaults.ok) << defaults.error->message;
  EXPECT_EQ("i", *defaults.data.GetDict().FindString("flags"));
  EXPECT_GT(defaults.data.GetDict().FindInt("searched").value_or(0), 0);
  EXPECT_EQ(1u, defaults.data.GetDict().FindList("matches")->size());
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       InvalidRegexReturnsTypedInvalidArgument) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();

  DaoBrowserToolResult result =
      Execute(session.get(), "search_in_resources",
              base::DictValue().Set("pattern", "[unterminated"));

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_TRUE(base::StartsWith(result.error->message, "Invalid regex:"));

  result = Execute(session.get(), "search_in_resources",
                   base::DictValue().Set("pattern", "dao").Set("flags", "g"));
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_TRUE(base::StartsWith(result.error->message, "Invalid regex:"));

  result = Execute(session.get(), "search_in_resources",
                   base::DictValue().Set("pattern", "dao(?=-search)"));
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
  EXPECT_TRUE(base::StartsWith(result.error->message, "Invalid regex:"));

  result =
      Execute(session.get(), "search_in_resources",
              base::DictValue().Set("pattern", "dao").Set("max_matches", 1001));
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kInvalidArgument, result.error->code);
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpDevToolsBrowserTest,
    ReplacementResolutionSurvivesTargetCallbackDestroyingSession) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  tabs::TabInterface* target_tab =
      TabListInterface::From(browser())->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  auto session = MakeSession(target_tab->GetContents());

  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* replacement_contents = replacement.get();
  const int target_index =
      TabListInterface::From(browser())->GetIndexOfTab(target_tab->GetHandle());
  ASSERT_GE(target_index, 0);
  std::unique_ptr<content::WebContents> discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          target_index, std::move(replacement));
  ASSERT_TRUE(content::NavigateToURL(replacement_contents, second_url()));
  session->SetTargetChangedCallback(base::BindRepeating(
      [](std::unique_ptr<DaoBrowserAutomationSession>* owned_session,
         DaoBrowserAutomationSession*) { owned_session->reset(); },
      &session));

  auto resolved = session->ResolveEligibleTarget();

  EXPECT_FALSE(session);
  ASSERT_FALSE(resolved.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, resolved.error().code);
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpDevToolsBrowserTest,
    ExecutorResolutionSurvivesTargetCallbackDestroyingOwners) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  tabs::TabInterface* target_tab =
      TabListInterface::From(browser())->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  auto session = MakeSession(target_tab->GetContents());

  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* replacement_contents = replacement.get();
  const int target_index =
      TabListInterface::From(browser())->GetIndexOfTab(target_tab->GetHandle());
  ASSERT_GE(target_index, 0);
  std::unique_ptr<content::WebContents> discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          target_index, std::move(replacement));
  ASSERT_TRUE(content::NavigateToURL(replacement_contents, second_url()));
  session->SetTargetChangedCallback(base::BindRepeating(
      [](std::unique_ptr<DaoBrowserToolExecutor>* owned_executor,
         std::unique_ptr<DaoBrowserAutomationSession>* owned_session,
         DaoBrowserAutomationSession*) {
        owned_executor->reset();
        owned_session->reset();
      },
      &executor_, &session));

  DaoBrowserToolCall call;
  call.request_id = "destroy-owners-during-resolution";
  call.name = "get_page_info";
  base::test::TestFuture<DaoBrowserToolResult> future;
  DaoBrowserToolExecutor* executor = executor_.get();
  executor->Execute(session.get(), DaoToolClient::kMcp, std::move(call),
                    future.GetCallback());
  DaoBrowserToolResult result = future.Take();

  EXPECT_FALSE(executor_);
  EXPECT_FALSE(session);
  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       NestedAbaTargetChangeDuringAttachIsNotDropped) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* old_host =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome::AddTabAt(browser(), second_url(), 1, false);
  chrome::AddTabAt(browser(), page_url(), 2, false);
  content::WebContents* stable_target =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  content::WebContents* bounce_target =
      browser()->tab_strip_model()->GetWebContentsAt(2);
  ASSERT_TRUE(content::WaitForLoadStop(stable_target));
  ASSERT_TRUE(content::WaitForLoadStop(bounce_target));

  DaoDevToolsClient client;
  DaoDevToolsTools tools(&client);
  auto session = MakeSession(stable_target);
  base::test::TestFuture<DaoBrowserToolResult> prime_future;
  tools.Execute("prime", session.get(), "get_network_requests",
                base::DictValue(), prime_future.GetCallback());
  ASSERT_TRUE(prime_future.Take().ok);

  session->devtools_state().network_requests.push_back(
      base::DictValue().Set("url", "https://sentinel.invalid/"));
  ASSERT_TRUE(client.AttachTo(old_host));

  int cancelled_command_count = 0;
  std::optional<DaoToolErrorCode> cancelled_command_code;
  const int pending_command = client.SendCommand(
      "Runtime.evaluate",
      base::DictValue()
          .Set("expression", "new Promise(() => {})")
          .Set("awaitPromise", true),
      base::BindOnce(
          [](base::WeakPtr<DaoBrowserAutomationSession> session,
             base::WeakPtr<content::WebContents> bounce_target,
             base::WeakPtr<content::WebContents> stable_target,
             int* callback_count,
             std::optional<DaoToolErrorCode>* cancelled_code,
             DaoDevToolsClient::CommandResult result) {
            ++*callback_count;
            if (!result.has_value()) {
              *cancelled_code = result.error().code;
            }
            if (session && bounce_target && stable_target) {
              session->SetTarget(bounce_target.get());
              session->SetTarget(stable_target.get());
            }
          },
          session->GetWeakPtr(), bounce_target->GetWeakPtr(),
          stable_target->GetWeakPtr(), &cancelled_command_count,
          &cancelled_command_code));
  ASSERT_GT(pending_command, 0);
  ASSERT_TRUE(client.has_pending_commands());

  base::test::TestFuture<DaoBrowserToolResult> result_future;
  tools.Execute("trigger", session.get(), "get_network_requests",
                base::DictValue(), result_future.GetCallback());
  DaoBrowserToolResult result = result_future.Take();

  EXPECT_EQ(1, cancelled_command_count);
  ASSERT_TRUE(cancelled_command_code.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, *cancelled_command_code);
  ASSERT_TRUE(result.ok) << result.error->message;
  EXPECT_EQ(0, result.data.GetDict().FindInt("count").value_or(-1));
  EXPECT_TRUE(result.data.GetDict().FindList("requests")->empty());
  ASSERT_EQ(stable_target, session->ResolveTarget().value());
  EXPECT_EQ(stable_target, session->devtools_state().bound_target.get());
  EXPECT_EQ(client.agent_host(), session->devtools_state().bound_host.get());
  EXPECT_EQ(content::DevToolsAgentHost::GetOrCreateFor(stable_target).get(),
            client.agent_host());
  EXPECT_FALSE(client.has_pending_commands());
}

IN_PROC_BROWSER_TEST_F(
    DaoMcpDevToolsBrowserTest,
    TargetSwitchClearsBuffersRejectsOldEventsAndReenablesDomains) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* first_target =
      browser()->tab_strip_model()->GetActiveWebContents();
  chrome::AddTabAt(browser(), second_url(), 1, false);
  content::WebContents* second_target =
      browser()->tab_strip_model()->GetWebContentsAt(1);
  ASSERT_TRUE(content::WaitForLoadStop(second_target));
  auto session = MakeSession(first_target);
  ASSERT_TRUE(Execute(session.get(), "enable_network_tracking").ok);
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);
  ASSERT_TRUE(content::ExecJs(first_target, "console.log('before-switch')"));
  ASSERT_TRUE(
      content::EvalJs(first_target,
                      "fetch('/api-body?before-switch').then(() => true)")
          .ExtractBool());
  ASSERT_TRUE(base::test::RunUntil([&] {
    DaoBrowserToolResult messages =
        Execute(session.get(), "get_console_messages");
    return messages.ok &&
           messages.data.GetDict().FindInt("count").value_or(0) > 0;
  }));

  DaoBrowserToolResult switched =
      Execute(session.get(), "switch_tab", base::DictValue().Set("index", 1));
  ASSERT_TRUE(switched.ok) << switched.error->message;
  ASSERT_EQ(second_target, session->ResolveTarget().value());
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(content::ExecJs(first_target, "console.log('old-after-switch')"));
  ASSERT_TRUE(content::ExecJs(second_target, "console.log('new-target')"));
  ASSERT_TRUE(content::EvalJs(second_target,
                              "fetch('/api-body?new-target').then(() => true)")
                  .ExtractBool());

  DaoBrowserToolResult messages;
  ASSERT_TRUE(base::test::RunUntil([&] {
    messages = Execute(session.get(), "get_console_messages");
    if (!messages.ok) {
      return false;
    }
    const base::ListValue* values =
        messages.data.GetDict().FindList("messages");
    return values && FindEntry(*values, "text", "new-target");
  }));
  EXPECT_TRUE(messages.data.GetDict().FindBool("enabled").value_or(false));
  const base::ListValue* values = messages.data.GetDict().FindList("messages");
  EXPECT_EQ(nullptr, FindEntry(*values, "text", "before-switch"));
  EXPECT_EQ(nullptr, FindEntry(*values, "text", "old-after-switch"));

  DaoBrowserToolResult requests;
  ASSERT_TRUE(base::test::RunUntil([&] {
    requests = Execute(session.get(), "get_network_requests");
    return requests.ok &&
           requests.data.GetDict().FindInt("count").value_or(0) > 0;
  }));
  EXPECT_TRUE(requests.data.GetDict().FindBool("enabled").value_or(false));
  ASSERT_TRUE(requests.target.has_value());
  EXPECT_EQ(second_url().spec(), requests.target->url);
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       ReplacementDocumentRebindsAndKeepsEnabledSemantics) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  tabs::TabInterface* target_tab =
      TabListInterface::From(browser())->GetActiveTab();
  ASSERT_NE(nullptr, target_tab);
  auto session = MakeSession(target_tab->GetContents());
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);

  auto replacement = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->profile()));
  content::WebContents* replacement_contents = replacement.get();
  const int target_index =
      TabListInterface::From(browser())->GetIndexOfTab(target_tab->GetHandle());
  ASSERT_GE(target_index, 0);
  std::unique_ptr<content::WebContents> discarded =
      browser()->tab_strip_model()->DiscardWebContentsAt(
          target_index, std::move(replacement));
  ASSERT_TRUE(content::NavigateToURL(replacement_contents, second_url()));
  ASSERT_EQ(replacement_contents, session->ResolveTarget().value());
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(
      content::ExecJs(replacement_contents, "console.log('replacement-doc')"));

  DaoBrowserToolResult messages;
  ASSERT_TRUE(base::test::RunUntil([&] {
    messages = Execute(session.get(), "get_console_messages");
    const base::ListValue* values =
        messages.ok ? messages.data.GetDict().FindList("messages") : nullptr;
    return values && FindEntry(*values, "text", "replacement-doc");
  }));
  EXPECT_TRUE(messages.data.GetDict().FindBool("enabled").value_or(false));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       SameTargetNavigationRebindsAndClearsOldEvents) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  content::WebContents* target =
      browser()->tab_strip_model()->GetActiveWebContents();
  auto session = MakeSession(target);
  ASSERT_TRUE(Execute(session.get(), "enable_console_tracking").ok);
  ASSERT_TRUE(content::ExecJs(target, "console.log('old-document')"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    DaoBrowserToolResult messages =
        Execute(session.get(), "get_console_messages");
    const base::ListValue* values =
        messages.ok ? messages.data.GetDict().FindList("messages") : nullptr;
    return values && FindEntry(*values, "text", "old-document");
  }));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url()));
  ASSERT_EQ(target, browser()->tab_strip_model()->GetActiveWebContents());
  DaoBrowserToolResult rebound = Execute(session.get(), "get_console_messages");
  ASSERT_TRUE(rebound.ok) << rebound.error->message;
  EXPECT_TRUE(rebound.data.GetDict().FindBool("enabled").value_or(false));
  EXPECT_EQ(0, rebound.data.GetDict().FindInt("count").value_or(-1));

  ASSERT_TRUE(content::ExecJs(target, "console.log('new-document')"));
  DaoBrowserToolResult messages;
  ASSERT_TRUE(base::test::RunUntil([&] {
    messages = Execute(session.get(), "get_console_messages");
    const base::ListValue* values =
        messages.ok ? messages.data.GetDict().FindList("messages") : nullptr;
    return values && FindEntry(*values, "text", "new-document");
  }));
  const base::ListValue* values = messages.data.GetDict().FindList("messages");
  ASSERT_NE(nullptr, values);
  EXPECT_EQ(nullptr, FindEntry(*values, "text", "old-document"));
  EXPECT_TRUE(messages.data.GetDict().FindBool("enabled").value_or(false));
}

IN_PROC_BROWSER_TEST_F(DaoMcpDevToolsBrowserTest,
                       CancellingCompositeSearchCompletesExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), page_url()));
  auto session = MakeSession();
  int callback_count = 0;
  base::test::TestFuture<DaoBrowserToolResult> future;

  const std::string request_id =
      ExecuteAsync(session.get(), "search_in_resources",
                   base::DictValue().Set("pattern", "dao-search-hit"), &future,
                   &callback_count);
  executor_->Cancel(request_id);
  DaoBrowserToolResult result = future.Take();
  base::RunLoop().RunUntilIdle();

  ASSERT_FALSE(result.ok);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(DaoToolErrorCode::kToolCancelled, result.error->code);
  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(devtools_client_->has_pending_commands());
}

}  // namespace
}  // namespace dao
