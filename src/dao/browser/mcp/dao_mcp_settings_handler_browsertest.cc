// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_settings_handler.h"

#include <memory>
#include <string>
#include <utility>

#include "base/apple/bundle_locations.h"
#include "base/files/file_path.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/scoped_path_override.h"
#include "base/test/test_future.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_task_environment.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_web_ui.h"
#include "dao/browser/dao_pref_names.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/clipboard/test/test_clipboard.h"
#include "url/gurl.h"

namespace dao {
namespace {

constexpr char kTestMcpConfiguration[] =
    R"({"mcpServers":{"dao":{"command":"/test/dao-mcp"}}})";
constexpr char kPrettyTestMcpConfiguration[] = R"({
   "mcpServers": {
      "dao": {
         "command": "/test/dao-mcp"
      }
   }
}
)";

class FakeDaoMcpSettingsService : public DaoMcpSettingsService {
 public:
  bool IsEnabled() const override { return enabled_; }

  DaoMcpServiceStatus GetStatus() const override { return status_; }

  void SetEnabled(bool enabled) override {
    enabled_ = enabled;
    ++set_enabled_call_count_;
  }

  void StopControl() override { ++stop_control_call_count_; }

  std::string GetMcpConfiguration() const override {
    return mcp_configuration_;
  }

  base::CallbackListSubscription AddObserver(StatusObserver observer) override {
    return observers_.Add(std::move(observer));
  }

  void SetStatus(DaoMcpServiceStatus status) {
    status_ = std::move(status);
    observers_.Notify(status_);
  }

  void SetMcpConfiguration(std::string configuration) {
    mcp_configuration_ = std::move(configuration);
  }

  int set_enabled_call_count() const { return set_enabled_call_count_; }
  int stop_control_call_count() const { return stop_control_call_count_; }

 private:
  bool enabled_ = false;
  std::string mcp_configuration_ = kTestMcpConfiguration;
  DaoMcpServiceStatus status_;
  int set_enabled_call_count_ = 0;
  int stop_control_call_count_ = 0;
  base::RepeatingCallbackList<void(const DaoMcpServiceStatus&)> observers_;
};

class TestDaoMcpSettingsHandler : public DaoMcpSettingsHandler {
 public:
  explicit TestDaoMcpSettingsHandler(DaoMcpSettingsService* service)
      : DaoMcpSettingsHandler(service) {}

  using DaoMcpSettingsHandler::HandleGetDaoMcpStatus;
  using DaoMcpSettingsHandler::HandleGetDaoMcpSetupContent;
  using DaoMcpSettingsHandler::HandleCopyDaoMcpSetupContent;
  using DaoMcpSettingsHandler::HandleResetDaoMcpUsageStats;
  using DaoMcpSettingsHandler::HandleSetDaoMcpEnabled;
  using DaoMcpSettingsHandler::HandleStopDaoMcpControl;
  using DaoMcpSettingsHandler::set_web_ui;
};

class DaoMcpSettingsHandlerTest : public testing::Test {
 public:
  DaoMcpSettingsHandlerTest()
      : user_data_override_(
            chrome::DIR_USER_DATA,
            base::FilePath(FILE_PATH_LITERAL("/Users/Test/Dao Debug")),
            true),
        handler_(std::make_unique<TestDaoMcpSettingsHandler>(&service_)) {
    handler_->set_web_ui(&web_ui_);
  }

  TestDaoMcpSettingsHandler* handler() { return handler_.get(); }
  FakeDaoMcpSettingsService* service() { return &service_; }
  content::TestWebUI* web_ui() { return &web_ui_; }

  const base::DictValue& GetStatus() {
    base::ListValue args;
    args.Append("callback");
    handler()->HandleGetDaoMcpStatus(args);
    EXPECT_FALSE(web_ui()->call_data().empty());
    const content::TestWebUI::CallData& call = *web_ui()->call_data().back();
    EXPECT_EQ("cr.webUIResponse", call.function_name());
    EXPECT_EQ("callback", call.arg1()->GetString());
    EXPECT_TRUE(call.arg2()->GetBool());
    return call.arg3()->GetDict();
  }

 private:
  content::BrowserTaskEnvironment task_environment_;
  base::ScopedPathOverride user_data_override_;
  content::TestWebUI web_ui_;
  FakeDaoMcpSettingsService service_;
  std::unique_ptr<TestDaoMcpSettingsHandler> handler_;
};

TEST_F(DaoMcpSettingsHandlerTest, ReportsAllStatesAndOnlyExposesActiveClient) {
  const base::DictValue& disabled = GetStatus();
  EXPECT_FALSE(disabled.FindBool("enabled").value());
  EXPECT_EQ("disabled", *disabled.FindString("state"));
  EXPECT_FALSE(disabled.FindBool("canStop").value());
  const base::DictValue* usage = disabled.FindDict("usageStats");
  ASSERT_TRUE(usage);
  EXPECT_EQ(0.0, usage->FindDouble("totalCalls").value_or(-1.0));

  web_ui()->ClearTrackedCalls();
  service()->SetEnabled(true);
  DaoMcpServiceStatus ready;
  ready.state = DaoMcpStatus::kListening;
  service()->SetStatus(std::move(ready));
  web_ui()->ClearTrackedCalls();
  const base::DictValue& ready_result = GetStatus();
  EXPECT_TRUE(ready_result.FindBool("enabled").value());
  EXPECT_EQ("ready", *ready_result.FindString("state"));

  web_ui()->ClearTrackedCalls();
  DaoMcpServiceStatus pending;
  pending.state = DaoMcpStatus::kPendingApproval;
  pending.client = DaoMcpClientInfo{
      .name = "Pending client",
      .version = "1",
      .verified_pid = 111,
  };
  service()->SetStatus(std::move(pending));
  web_ui()->ClearTrackedCalls();
  const base::DictValue& pending_result = GetStatus();
  EXPECT_EQ("approval_requested", *pending_result.FindString("state"));
  EXPECT_FALSE(pending_result.contains("clientName"));
  EXPECT_FALSE(pending_result.contains("clientPid"));
  EXPECT_FALSE(pending_result.FindBool("canStop").value());

  web_ui()->ClearTrackedCalls();
  DaoMcpServiceStatus connected;
  connected.state = DaoMcpStatus::kLeaseActive;
  connected.client = DaoMcpClientInfo{
      .name = "Codex",
      .version = "1",
      .verified_pid = 12345,
  };
  service()->SetStatus(std::move(connected));
  web_ui()->ClearTrackedCalls();
  const base::DictValue& connected_result = GetStatus();
  EXPECT_EQ("connected", *connected_result.FindString("state"));
  EXPECT_EQ("Codex", *connected_result.FindString("clientName"));
  EXPECT_EQ(12345, connected_result.FindInt("clientPid").value());
  EXPECT_TRUE(connected_result.FindBool("canStop").value());
}

TEST_F(DaoMcpSettingsHandlerTest, SanitizesConnectedClientNameForWebUi) {
  constexpr std::string_view kReportedName =
      "  Codex\r\n\t\xe2\x80\xaeInjected";
  service()->SetEnabled(true);
  DaoMcpServiceStatus connected;
  connected.state = DaoMcpStatus::kLeaseActive;
  connected.client = DaoMcpClientInfo{
      .name = std::string(kReportedName),
      .version = "1",
      .verified_pid = 12345,
  };
  service()->SetStatus(std::move(connected));
  web_ui()->ClearTrackedCalls();

  const base::DictValue& result = GetStatus();
  std::u16string expected =
      base::CollapseWhitespace(base::UTF8ToUTF16(kReportedName), true);
  base::i18n::SanitizeUserSuppliedString(&expected);
  EXPECT_EQ(base::UTF16ToUTF8(expected), *result.FindString("clientName"));
  EXPECT_EQ(std::string::npos, result.FindString("clientName")->find('\n'));
  EXPECT_EQ(std::string::npos, result.FindString("clientName")->find('\r'));
  EXPECT_EQ(std::string::npos, result.FindString("clientName")->find('\t'));
}

TEST_F(DaoMcpSettingsHandlerTest, WritesGlobalEnabledStateThroughService) {
  base::ListValue args;
  args.Append(true);
  handler()->HandleSetDaoMcpEnabled(args);

  EXPECT_TRUE(service()->IsEnabled());
  EXPECT_EQ(1, service()->set_enabled_call_count());
}

TEST_F(DaoMcpSettingsHandlerTest, StopsControlThroughService) {
  handler()->HandleStopDaoMcpControl(base::ListValue());

  EXPECT_EQ(1, service()->stop_control_call_count());
}

TEST_F(DaoMcpSettingsHandlerTest, ReturnsAndCopiesValidatedCliSetupContent) {
  ui::TestClipboard* clipboard = ui::TestClipboard::CreateForCurrentThread();

  base::ListValue preview_args;
  preview_args.Append("preview");
  preview_args.Append("codex");
  handler()->HandleGetDaoMcpSetupContent(preview_args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& preview_call =
      *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", preview_call.function_name());
  EXPECT_EQ("preview", preview_call.arg1()->GetString());
  EXPECT_TRUE(preview_call.arg2()->GetBool());
  const std::optional<std::string> expected_codex =
      BuildDaoMcpInstallCommandForBundle(base::apple::OuterBundlePath(),
                                         "codex");
  ASSERT_TRUE(expected_codex);
  EXPECT_EQ(*expected_codex, preview_call.arg3()->GetString());

  web_ui()->ClearTrackedCalls();
  base::ListValue copy_args;
  copy_args.Append("copy");
  copy_args.Append("claude-code");
  handler()->HandleCopyDaoMcpSetupContent(copy_args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& copy_call =
      *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", copy_call.function_name());
  EXPECT_EQ("copy", copy_call.arg1()->GetString());
  EXPECT_TRUE(copy_call.arg2()->GetBool());
  EXPECT_TRUE(copy_call.arg3()->GetBool());

  base::test::TestFuture<std::u16string> clipboard_text;
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      clipboard_text.GetCallback());
  const std::optional<std::string> expected_claude =
      BuildDaoMcpInstallCommandForBundle(base::apple::OuterBundlePath(),
                                         "claude-code");
  ASSERT_TRUE(expected_claude);
  EXPECT_EQ(base::UTF8ToUTF16(*expected_claude), clipboard_text.Get());

  ui::Clipboard::DestroyClipboardForCurrentThread();
}

TEST_F(DaoMcpSettingsHandlerTest,
       GenericMcpReturnsAndCopiesPrettyServiceConfiguration) {
  ui::TestClipboard* clipboard = ui::TestClipboard::CreateForCurrentThread();

  base::ListValue preview_args;
  preview_args.Append("preview");
  preview_args.Append("generic-mcp");
  handler()->HandleGetDaoMcpSetupContent(preview_args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& preview_call =
      *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", preview_call.function_name());
  EXPECT_EQ("preview", preview_call.arg1()->GetString());
  EXPECT_TRUE(preview_call.arg2()->GetBool());
  EXPECT_EQ(kPrettyTestMcpConfiguration, preview_call.arg3()->GetString());

  web_ui()->ClearTrackedCalls();
  base::ListValue copy_args;
  copy_args.Append("copy");
  copy_args.Append("generic-mcp");
  handler()->HandleCopyDaoMcpSetupContent(copy_args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& copy_call =
      *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", copy_call.function_name());
  EXPECT_EQ("copy", copy_call.arg1()->GetString());
  EXPECT_TRUE(copy_call.arg2()->GetBool());
  EXPECT_TRUE(copy_call.arg3()->GetBool());

  base::test::TestFuture<std::u16string> clipboard_text;
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      clipboard_text.GetCallback());
  EXPECT_EQ(base::UTF8ToUTF16(std::string_view(kPrettyTestMcpConfiguration)),
            clipboard_text.Get());
  EXPECT_EQ(kTestMcpConfiguration, service()->GetMcpConfiguration());

  ui::Clipboard::DestroyClipboardForCurrentThread();
}

TEST_F(DaoMcpSettingsHandlerTest,
       MalformedGenericMcpConfigurationLeavesClipboardUnchanged) {
  ui::TestClipboard* clipboard = ui::TestClipboard::CreateForCurrentThread();
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(u"sentinel");
  service()->SetMcpConfiguration("{invalid");

  base::ListValue preview_args;
  preview_args.Append("preview");
  preview_args.Append("generic-mcp");
  handler()->HandleGetDaoMcpSetupContent(preview_args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& preview_call =
      *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", preview_call.function_name());
  EXPECT_EQ("preview", preview_call.arg1()->GetString());
  EXPECT_TRUE(preview_call.arg2()->GetBool());
  EXPECT_EQ("", preview_call.arg3()->GetString());

  web_ui()->ClearTrackedCalls();
  base::ListValue copy_args;
  copy_args.Append("copy");
  copy_args.Append("generic-mcp");
  handler()->HandleCopyDaoMcpSetupContent(copy_args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& copy_call =
      *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", copy_call.function_name());
  EXPECT_EQ("copy", copy_call.arg1()->GetString());
  EXPECT_TRUE(copy_call.arg2()->GetBool());
  EXPECT_FALSE(copy_call.arg3()->GetBool());

  base::test::TestFuture<std::u16string> clipboard_text;
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      clipboard_text.GetCallback());
  EXPECT_EQ(u"sentinel", clipboard_text.Get());

  ui::Clipboard::DestroyClipboardForCurrentThread();
}

TEST_F(DaoMcpSettingsHandlerTest,
       InvalidSetupOptionLeavesClipboardUnchanged) {
  ui::TestClipboard* clipboard = ui::TestClipboard::CreateForCurrentThread();
  ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
      .WriteText(u"sentinel");

  base::ListValue args;
  args.Append("copy");
  args.Append("unknown");
  handler()->HandleCopyDaoMcpSetupContent(args);

  ASSERT_FALSE(web_ui()->call_data().empty());
  const content::TestWebUI::CallData& call = *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIResponse", call.function_name());
  EXPECT_EQ("copy", call.arg1()->GetString());
  EXPECT_TRUE(call.arg2()->GetBool());
  EXPECT_FALSE(call.arg3()->GetBool());

  base::test::TestFuture<std::u16string> clipboard_text;
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      clipboard_text.GetCallback());
  EXPECT_EQ(u"sentinel", clipboard_text.Get());

  ui::Clipboard::DestroyClipboardForCurrentThread();
}

TEST_F(DaoMcpSettingsHandlerTest, ObservesOnlyWhileJavascriptIsAllowed) {
  GetStatus();
  web_ui()->ClearTrackedCalls();

  DaoMcpServiceStatus connected;
  connected.state = DaoMcpStatus::kLeaseActive;
  connected.client = DaoMcpClientInfo{
      .name = "Codex",
      .version = "1",
      .verified_pid = 12345,
  };
  service()->SetStatus(std::move(connected));

  ASSERT_EQ(1u, web_ui()->call_data().size());
  const content::TestWebUI::CallData& event = *web_ui()->call_data().back();
  EXPECT_EQ("cr.webUIListenerCallback", event.function_name());
  EXPECT_EQ("dao-mcp-status-changed", event.arg1()->GetString());
  EXPECT_EQ("connected", *event.arg2()->GetDict().FindString("state"));

  web_ui()->ClearTrackedCalls();
  handler()->DisallowJavascript();
  service()->SetStatus(DaoMcpServiceStatus());
  EXPECT_TRUE(web_ui()->call_data().empty());
}

TEST(DaoMcpSettingsConfigurationTest, UsesProvidedApplicationBundlePath) {
  const base::FilePath bundle_path(
      FILE_PATH_LITERAL("/Volumes/Test/Dao Debug.app"));
  base::ScopedPathOverride user_data_override(
      chrome::DIR_USER_DATA,
      base::FilePath(FILE_PATH_LITERAL("/Users/Test/Dao Debug")), true);
  const std::string configuration =
      BuildDaoMcpConfigurationForBundle(bundle_path);
  const std::optional<base::Value> parsed =
      base::JSONReader::Read(configuration, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed.has_value());
  const std::string* command =
      parsed->GetDict().FindStringByDottedPath("mcpServers.dao.command");
  ASSERT_TRUE(command);
  EXPECT_EQ("/Volumes/Test/Dao Debug.app/Contents/Helpers/dao-mcp", *command);
}

TEST(DaoMcpInstallCommandTest, BuildsCodexAndClaudeCommands) {
  const base::FilePath bundle_path(
      FILE_PATH_LITERAL("/Volumes/Test Disk/Dao Debug.app"));
  base::ScopedPathOverride user_data_override(
      chrome::DIR_USER_DATA,
      base::FilePath(FILE_PATH_LITERAL("/Users/Test/Dao Debug")), true);

  const std::optional<std::string> codex =
      BuildDaoMcpInstallCommandForBundle(bundle_path, "codex");
  ASSERT_TRUE(codex);
  EXPECT_EQ(
      "codex mcp add dao -- "
      "'/Volumes/Test Disk/Dao Debug.app/Contents/Helpers/dao-mcp' "
      "'--user-data-dir=/Users/Test/Dao Debug'",
      *codex);

  const std::optional<std::string> claude =
      BuildDaoMcpInstallCommandForBundle(bundle_path, "claude-code");
  ASSERT_TRUE(claude);
  EXPECT_EQ(
      "claude mcp add --scope user dao -- "
      "'/Volumes/Test Disk/Dao Debug.app/Contents/Helpers/dao-mcp' "
      "'--user-data-dir=/Users/Test/Dao Debug'",
      *claude);
}

TEST(DaoMcpInstallCommandTest, IncludesActiveUserDataDir) {
  const base::FilePath bundle_path(
      FILE_PATH_LITERAL("/Volumes/Test Disk/Dao Debug.app"));
  const base::FilePath user_data_dir(
      FILE_PATH_LITERAL("/Users/Test User/Library/Application Support/"
                        "Dao Debug"));
  base::ScopedPathOverride user_data_override(
      chrome::DIR_USER_DATA, user_data_dir, true);

  const std::optional<std::string> codex =
      BuildDaoMcpInstallCommandForBundle(bundle_path, "codex");
  ASSERT_TRUE(codex);
  EXPECT_EQ(
      "codex mcp add dao -- "
      "'/Volumes/Test Disk/Dao Debug.app/Contents/Helpers/dao-mcp' "
      "'--user-data-dir=/Users/Test User/Library/Application Support/"
      "Dao Debug'",
      *codex);

  const std::optional<base::Value> generic = base::JSONReader::Read(
      BuildDaoMcpConfigurationForBundle(bundle_path), base::JSON_PARSE_RFC);
  ASSERT_TRUE(generic);
  const base::ListValue* args =
      generic->GetDict().FindListByDottedPath("mcpServers.dao.args");
  ASSERT_TRUE(args);
  ASSERT_EQ(1u, args->size());
  EXPECT_EQ(
      "--user-data-dir=/Users/Test User/Library/Application Support/Dao Debug",
      (*args)[0].GetString());
}

TEST(DaoMcpInstallCommandTest, QuotesEmbeddedSingleQuote) {
  const base::FilePath bundle_path(
      FILE_PATH_LITERAL("/Volumes/Dao's Builds/Dao.app"));
  base::ScopedPathOverride user_data_override(
      chrome::DIR_USER_DATA,
      base::FilePath(FILE_PATH_LITERAL("/Users/Test/Dao")), true);
  const std::optional<std::string> command =
      BuildDaoMcpInstallCommandForBundle(bundle_path, "codex");

  ASSERT_TRUE(command);
  EXPECT_EQ(
      "codex mcp add dao -- "
      "'/Volumes/Dao'\"'\"'s Builds/Dao.app/Contents/Helpers/dao-mcp' "
      "'--user-data-dir=/Users/Test/Dao'",
      *command);
}

TEST(DaoMcpInstallCommandTest, RejectsUnknownClient) {
  EXPECT_FALSE(BuildDaoMcpInstallCommandForBundle(
      base::FilePath(FILE_PATH_LITERAL("/Applications/Dao.app")), "other"));
}

class DaoMcpSettingsPageBrowserTest : public InProcessBrowserTest {};

IN_PROC_BROWSER_TEST_F(DaoMcpSettingsPageBrowserTest,
                       RendersAndEnablesMcpServer) {
  ui::TestClipboard* clipboard = ui::TestClipboard::CreateForCurrentThread();
  PrefService* local_state = g_browser_process->local_state();
  ASSERT_TRUE(local_state);
  DaoMcpService::Get()->SetEnabled(false);
  ASSERT_FALSE(local_state->GetBoolean(prefs::kDaoMcpServerEnabled));

  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("dao://settings/dao")));
  content::WebContents* contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(contents);

  ASSERT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      function findDeep(selector, root = document) {
        const match = root.querySelector(selector);
        if (match) {
          return match;
        }
        for (const element of root.querySelectorAll('*')) {
          if (element.shadowRoot) {
            const nested = findDeep(selector, element.shadowRoot);
            if (nested) {
              return nested;
            }
          }
        }
        return null;
      }

      const deadline = performance.now() + 10000;
      let page = null;
      while (!page && performance.now() < deadline) {
        page = findDeep('settings-dao-page');
        if (!page) {
          await new Promise(resolve => setTimeout(resolve, 25));
        }
      }
      if (!page || !page.shadowRoot) {
        return false;
      }
      window.daoMcpPageForTest = page;
      return true;
    })()
  )JS"));

  ASSERT_EQ(true, content::EvalJs(contents, R"JS(
    (() => {
      const page = window.daoMcpPageForTest;
      const toggle =
          page.shadowRoot.querySelector('#daoMcpServerEnabled');
      const status = page.shadowRoot.querySelector('#daoMcpStatusValue');
      return Boolean(
          toggle && !toggle.checked && status &&
          status.textContent.trim() &&
          page.shadowRoot.querySelector('#daoMcpConnectionSection') &&
          !page.shadowRoot.querySelector('.dao-mcp-title') &&
          !page.shadowRoot.querySelector('#daoMcpCopyConfig') &&
          !page.shadowRoot.querySelector('#daoMcpQuickSetup') &&
          !page.shadowRoot.querySelector('#daoMcpStopControl'));
    })()
  )JS"));

  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const page = window.daoMcpPageForTest;
      const toggle =
          page.shadowRoot.querySelector('#daoMcpServerEnabled');
      const status = page.shadowRoot.querySelector('#daoMcpStatusValue');
      const disabledStatus = status.textContent.trim();
      toggle.click();
      const deadline = performance.now() + 10000;
      while (performance.now() < deadline) {
        if (toggle.checked &&
            status.textContent.trim() !== disabledStatus) {
          return true;
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return false;
    })()
  )JS"));

  EXPECT_TRUE(local_state->GetBoolean(prefs::kDaoMcpServerEnabled));
  EXPECT_EQ(DaoMcpStatus::kListening, DaoMcpService::Get()->GetStatus().state);

  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const page = window.daoMcpPageForTest;
      const deadline = performance.now() + 10000;
      while (performance.now() < deadline) {
        const quickSetup =
            page.shadowRoot.querySelector('#daoMcpQuickSetup');
        const selector =
            page.shadowRoot.querySelector('#daoMcpClientSelector');
        const setupControls =
            page.shadowRoot.querySelector('#daoMcpSetupControls');
        const preview =
            page.shadowRoot.querySelector('#daoMcpSetupPreview');
        const copy = page.shadowRoot.querySelector('#daoMcpCopySetup');
        if (quickSetup && selector && setupControls && copy &&
            selector.value === 'codex' && preview &&
            preview.textContent.trim().startsWith('codex mcp add dao -- ')) {
          const selectorRect = selector.getBoundingClientRect();
          const copyRect = copy.getBoundingClientRect();
          const controlsRect = setupControls.getBoundingClientRect();
          const previewRect = preview.getBoundingClientRect();
          return Math.abs(selectorRect.bottom - copyRect.bottom) <= 1 &&
              Math.abs(previewRect.left - controlsRect.left) <= 1 &&
              Math.abs(previewRect.right - controlsRect.right) <= 1;
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return false;
    })()
  )JS"));

  const std::optional<std::string> expected_claude_command =
      BuildDaoMcpInstallCommandForBundle(base::apple::OuterBundlePath(),
                                         "claude-code");
  ASSERT_TRUE(expected_claude_command);
  EXPECT_EQ(*expected_claude_command,
            content::EvalJs(contents, R"JS(
    (async () => {
      const page = window.daoMcpPageForTest;
      const selector =
          page.shadowRoot.querySelector('#daoMcpClientSelector');
      selector.value = 'claude-code';
      selector.dispatchEvent(new Event('change', {bubbles: true}));

      const deadline = performance.now() + 10000;
      while (performance.now() < deadline) {
        const preview =
            page.shadowRoot.querySelector('#daoMcpSetupPreview');
        if (preview && preview.textContent.trim().startsWith(
                           'claude mcp add --scope user dao -- ')) {
          page.shadowRoot.querySelector('#daoMcpCopySetup').click();
          while (performance.now() < deadline) {
            const feedback = page.shadowRoot.querySelector(
                '#daoMcpSetupFeedback');
            if (feedback && feedback.textContent.trim()) {
              return preview.textContent.trim();
            }
            await new Promise(resolve => setTimeout(resolve, 25));
          }
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return '';
    })()
  )JS")
                .ExtractString());

  base::test::TestFuture<std::u16string> clipboard_text;
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      clipboard_text.GetCallback());
  EXPECT_EQ(base::UTF8ToUTF16(*expected_claude_command), clipboard_text.Get());

  const std::optional<base::Value> parsed_configuration =
      base::JSONReader::Read(DaoMcpService::Get()->GetMcpConfiguration(),
                             base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed_configuration);
  std::string expected_configuration;
  ASSERT_TRUE(base::JSONWriter::WriteWithOptions(
      *parsed_configuration, base::JSONWriter::OPTIONS_PRETTY_PRINT,
      &expected_configuration));
  EXPECT_EQ(expected_configuration,
            content::EvalJs(contents, R"JS(
    (async () => {
      const page = window.daoMcpPageForTest;
      const selector =
          page.shadowRoot.querySelector('#daoMcpClientSelector');
      selector.value = 'generic-mcp';
      selector.dispatchEvent(new Event('change', {bubbles: true}));

      const deadline = performance.now() + 10000;
      while (performance.now() < deadline) {
        const preview = page.shadowRoot.querySelector('#daoMcpSetupPreview');
        const copy = page.shadowRoot.querySelector('#daoMcpCopySetup');
        if (preview && preview.textContent.startsWith(
                           '{\n   "mcpServers":') &&
            copy && copy.textContent.trim()) {
          copy.click();
          while (performance.now() < deadline) {
            const feedback =
                page.shadowRoot.querySelector('#daoMcpSetupFeedback');
            if (feedback && feedback.textContent.trim()) {
              return preview.textContent;
            }
            await new Promise(resolve => setTimeout(resolve, 25));
          }
        }
        await new Promise(resolve => setTimeout(resolve, 25));
      }
      return '';
    })()
  )JS")
                .ExtractString());

  base::test::TestFuture<std::u16string> configuration_clipboard_text;
  clipboard->ReadText(ui::ClipboardBuffer::kCopyPaste, std::nullopt,
                      configuration_clipboard_text.GetCallback());
  EXPECT_EQ(base::UTF8ToUTF16(expected_configuration),
            configuration_clipboard_text.Get());

  EXPECT_EQ(
      R"([{"message":"stopDaoMcpControl","args":[]}])",
      content::EvalJs(contents, R"JS(
    (async () => {
      const page = window.daoMcpPageForTest;
      window.cr.webUIListenerCallback('dao-mcp-status-changed', {
        enabled: true,
        state: 'connected',
        clientName: 'Codex',
        clientPid: 12345,
        canStop: true,
      });
      await new Promise(resolve => setTimeout(resolve, 0));
      const client = page.shadowRoot.querySelector('#daoMcpClient');
      const stop = page.shadowRoot.querySelector('#daoMcpStopControl');
      if (!client || !client.textContent.includes('Codex') ||
          !client.textContent.includes('12345') ||
          page.shadowRoot.querySelector('#daoMcpCopyConfig') || !stop) {
        return 'missing connected controls';
      }

      const messages = [];
      const originalSend = window.chrome.send;
      window.chrome.send = (message, args = []) => {
        messages.push({message, args});
      };
      try {
        stop.click();
      } finally {
        window.chrome.send = originalSend;
      }
      return JSON.stringify(messages);
    })()
  )JS"));

  EXPECT_EQ(
      "ok",
      content::EvalJs(contents, R"JS(
    (async () => {
      const page = window.daoMcpPageForTest;

      window.cr.webUIListenerCallback('dao-mcp-status-changed', {
        enabled: true,
        state: 'connected',
        clientName: 'Codex for macOS',
        canStop: false,
      });
      await new Promise(resolve => setTimeout(resolve, 0));
      let client = page.shadowRoot.querySelector('#daoMcpClient');
      if (!client || client.textContent.trim() !== 'Codex for macOS') {
        return 'missing name-only client';
      }

      window.cr.webUIListenerCallback('dao-mcp-status-changed', {
        enabled: true,
        state: 'connected',
        canStop: false,
      });
      await new Promise(resolve => setTimeout(resolve, 0));
      if (page.shadowRoot.querySelector('#daoMcpClient')) {
        return 'rendered empty client';
      }

      const styleSheets = [
        ...Array.from(page.shadowRoot.querySelectorAll('style'))
            .map(style => style.sheet)
            .filter(Boolean),
        ...Array.from(page.shadowRoot.adoptedStyleSheets || []),
      ];
      const topLevelRules = styleSheets.flatMap(
          sheet => Array.from(sheet.cssRules));
      const mediaRule = topLevelRules.find(
          rule => rule instanceof CSSMediaRule &&
              rule.conditionText === '(max-width: 520px)');
      if (!mediaRule) {
        return 'missing narrow media rule';
      }
      const controlsRule = Array.from(mediaRule.cssRules).find(
          rule => rule instanceof CSSStyleRule &&
              rule.selectorText === '.dao-mcp-setup-controls');
      if (!controlsRule || controlsRule.style.flexDirection !== 'column') {
        return 'missing narrow controls layout';
      }

      const setupResolvers = [];
      const copyResolvers = [];
      page.daoMcpBrowserProxy_ = {
        getDaoMcpStatus: () => Promise.resolve({
          enabled: true,
          state: 'ready',
          canStop: false,
        }),
        setDaoMcpEnabled: () => {},
        getDaoMcpSetupContent: option => new Promise(resolve => {
          setupResolvers.push({option, resolve});
        }),
        copyDaoMcpSetupContent: option => new Promise(resolve => {
          copyResolvers.push({option, resolve});
        }),
        stopDaoMcpControl: () => {},
      };

      const selector =
          page.shadowRoot.querySelector('#daoMcpClientSelector');
      let preview = page.shadowRoot.querySelector('#daoMcpSetupPreview');
      let copy = page.shadowRoot.querySelector('#daoMcpCopySetup');
      selector.value = 'codex';
      selector.dispatchEvent(new Event('change', {bubbles: true}));
      if (preview.textContent !== '' || !copy.disabled) {
        return 'selection retained stale content';
      }

      selector.value = 'claude-code';
      selector.dispatchEvent(new Event('change', {bubbles: true}));
      if (setupResolvers.length !== 2) {
        return 'missing controlled setup requests';
      }
      setupResolvers[0].resolve('stale codex command');
      await new Promise(resolve => setTimeout(resolve, 0));
      if (preview.textContent !== '') {
        return 'rendered stale preview';
      }
      setupResolvers[1].resolve('fresh claude command');
      await new Promise(resolve => setTimeout(resolve, 0));
      if (preview.textContent !== 'fresh claude command' || copy.disabled) {
        return 'missing fresh preview';
      }

      copy.click();
      if (copyResolvers.length !== 1) {
        return 'missing controlled copy request';
      }
      window.cr.webUIListenerCallback('dao-mcp-status-changed', {
        enabled: false,
        state: 'disabled',
        canStop: false,
      });
      window.cr.webUIListenerCallback('dao-mcp-status-changed', {
        enabled: true,
        state: 'ready',
        canStop: false,
      });
      copyResolvers[0].resolve(true);
      await new Promise(resolve => setTimeout(resolve, 0));
      if (setupResolvers.length !== 3) {
        return 'missing re-enabled setup request';
      }
      setupResolvers[2].resolve('re-enabled claude command');
      await new Promise(resolve => setTimeout(resolve, 0));
      const feedback = page.shadowRoot.querySelector('#daoMcpSetupFeedback');
      preview = page.shadowRoot.querySelector('#daoMcpSetupPreview');
      copy = page.shadowRoot.querySelector('#daoMcpCopySetup');
      if (!feedback || feedback.textContent.trim() !== '' ||
          preview.textContent !== 're-enabled claude command' ||
          copy.disabled) {
        return 'restored stale copy feedback';
      }

      return 'ok';
    })()
  )JS")
          .ExtractString());

  ui::Clipboard::DestroyClipboardForCurrentThread();
}

}  // namespace
}  // namespace dao
