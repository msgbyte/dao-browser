// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_settings_handler.h"

#include <string_view>
#include <utility>

#include "build/build_config.h"
#if BUILDFLAG(IS_MAC)
#include "base/apple/bundle_locations.h"
#include "dao/browser/mcp/dao_local_agent_integration.h"
#endif
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/window_metadata/window_metadata_controller.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"

namespace dao {
namespace {

constexpr char kStatusChangedEvent[] = "dao-mcp-status-changed";

std::string QuotePosixShellWord(std::string_view value) {
  std::string quoted("'");
  for (char character : value) {
    if (character == '\'') {
      quoted.append("'\"'\"'");
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string SanitizeClientName(std::string_view client_name) {
  std::u16string sanitized =
      base::CollapseWhitespace(base::UTF8ToUTF16(client_name), true);
  base::i18n::SanitizeUserSuppliedString(&sanitized);
  return base::UTF16ToUTF8(sanitized);
}

class DaoMcpSettingsServiceImpl final : public DaoMcpSettingsService {
 public:
  DaoMcpSettingsServiceImpl() = default;
  ~DaoMcpSettingsServiceImpl() override = default;

  bool IsEnabled() const override {
    PrefService* local_state = g_browser_process->local_state();
    return DaoMcpService::IsSupported() && local_state &&
           local_state->GetBoolean(prefs::kDaoMcpServerEnabled);
  }

  DaoMcpServiceStatus GetStatus() const override {
    return DaoMcpService::Get()->GetStatus();
  }

  void SetEnabled(bool enabled) override {
    DaoMcpService::Get()->SetEnabled(enabled);
  }

  std::vector<DaoMcpConnectionInfo> GetConnections() const override {
    return DaoMcpService::Get()->GetConnections();
  }

  void StopControl(std::string_view connection_id) override {
    DaoMcpService::Get()->StopControl(connection_id);
  }

  void RevokeTab(std::string_view connection_id,
                 std::string_view tab_id) override {
    DaoMcpService::Get()->RevokeTab(connection_id, tab_id);
  }

  std::string GetMcpConfiguration() const override {
    return DaoMcpService::Get()->GetMcpConfiguration();
  }

  base::CallbackListSubscription AddObserver(
      StatusObserver observer) override {
    return DaoMcpService::Get()->AddObserver(std::move(observer));
  }
};

std::string StatusName(DaoMcpStatus status) {
  switch (status) {
    case DaoMcpStatus::kDisabled:
      return "disabled";
    case DaoMcpStatus::kListening:
      return "ready";
    case DaoMcpStatus::kPendingApproval:
      return "approval_requested";
    case DaoMcpStatus::kLeaseActive:
      return "connected";
  }
  NOTREACHED();
}

}  // namespace

std::optional<std::string> BuildDaoMcpInstallCommandForBundle(
    const base::FilePath& bundle_path,
    std::string_view client_id) {
  if (client_id != "codex" && client_id != "claude-code") {
    return std::nullopt;
  }
  const std::string helper_path =
      bundle_path.Append(FILE_PATH_LITERAL("Contents"))
          .Append(FILE_PATH_LITERAL("Helpers"))
          .Append(FILE_PATH_LITERAL("dao-mcp"))
          .AsUTF8Unsafe();
  const std::string user_data_argument =
      base::StrCat({"--user-data-dir=",
                    base::PathService::CheckedGet(chrome::DIR_USER_DATA)
                        .AsUTF8Unsafe()});
  const std::string quoted_helper_path = QuotePosixShellWord(helper_path);
  const std::string quoted_user_data_argument =
      QuotePosixShellWord(user_data_argument);
  if (client_id == "codex") {
    return base::StrCat({"codex mcp add dao -- ", quoted_helper_path, " ",
                         quoted_user_data_argument});
  }
  return base::StrCat({"claude mcp add --scope user dao -- ",
                       quoted_helper_path, " ", quoted_user_data_argument});
}

DaoMcpSettingsHandler::DaoMcpSettingsHandler()
    : owned_service_(std::make_unique<DaoMcpSettingsServiceImpl>()),
      service_(owned_service_.get()) {}

DaoMcpSettingsHandler::DaoMcpSettingsHandler(
    DaoMcpSettingsService* service)
    : service_(service) {
  CHECK(service_);
}

DaoMcpSettingsHandler::~DaoMcpSettingsHandler() = default;

void DaoMcpSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getDaoMcpStatus",
      base::BindRepeating(&DaoMcpSettingsHandler::HandleGetDaoMcpStatus,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDaoMcpEnabled",
      base::BindRepeating(&DaoMcpSettingsHandler::HandleSetDaoMcpEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDaoMcpSetupContent",
      base::BindRepeating(
          &DaoMcpSettingsHandler::HandleGetDaoMcpSetupContent,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "copyDaoMcpSetupContent",
      base::BindRepeating(
          &DaoMcpSettingsHandler::HandleCopyDaoMcpSetupContent,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "stopDaoMcpControl",
      base::BindRepeating(&DaoMcpSettingsHandler::HandleStopDaoMcpControl,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "revokeDaoMcpTab",
      base::BindRepeating(&DaoMcpSettingsHandler::HandleRevokeDaoMcpTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDaoLocalAgents",
      base::BindRepeating(&DaoMcpSettingsHandler::HandleGetDaoLocalAgents,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDaoLocalAgentInstalled",
      base::BindRepeating(
          &DaoMcpSettingsHandler::HandleSetDaoLocalAgentInstalled,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "resetDaoMcpUsageStats",
      base::BindRepeating(
          &DaoMcpSettingsHandler::HandleResetDaoMcpUsageStats,
          base::Unretained(this)));
}

void DaoMcpSettingsHandler::OnJavascriptAllowed() {
  status_subscription_ = service_->AddObserver(base::BindRepeating(
      &DaoMcpSettingsHandler::OnStatusChanged, base::Unretained(this)));
  if (PrefService* prefs = GetPrefs()) {
    pref_change_registrar_.Init(prefs);
    pref_change_registrar_.Add(
        prefs::kDaoMcpUsageStats,
        base::BindRepeating(&DaoMcpSettingsHandler::OnUsageStatsChanged,
                            base::Unretained(this)));
  }
}

void DaoMcpSettingsHandler::OnJavascriptDisallowed() {
  status_subscription_ = {};
  pref_change_registrar_.Reset();
  weak_factory_.InvalidateWeakPtrs();
}

void DaoMcpSettingsHandler::HandleGetDaoMcpStatus(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_EQ(1u, args.size());
  ResolveJavascriptCallback(args[0], CreateStatusValue());
}

void DaoMcpSettingsHandler::HandleSetDaoMcpEnabled(
    const base::ListValue& args) {
  CHECK_EQ(1u, args.size());
  service_->SetEnabled(args[0].GetBool());
}

void DaoMcpSettingsHandler::HandleGetDaoMcpSetupContent(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_EQ(2u, args.size());
  const std::optional<std::string> content =
      GetSetupContent(args[1].GetString());
  ResolveJavascriptCallback(args[0], content.value_or(""));
}

void DaoMcpSettingsHandler::HandleCopyDaoMcpSetupContent(
    const base::ListValue& args) {
  AllowJavascript();
  CHECK_EQ(2u, args.size());
  const std::optional<std::string> content =
      GetSetupContent(args[1].GetString());
  if (!content) {
    ResolveJavascriptCallback(args[0], false);
    return;
  }

  ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
  writer.WriteText(base::UTF8ToUTF16(*content));
  ResolveJavascriptCallback(args[0], true);
}

void DaoMcpSettingsHandler::HandleStopDaoMcpControl(
    const base::ListValue& args) {
  if (args.size() != 1 || !args[0].is_string()) {
    return;
  }
  for (const auto& connection : service_->GetConnections()) {
    if (connection.connection_id == args[0].GetString() &&
        IsConnectionInCurrentProfile(connection)) {
      service_->StopControl(connection.connection_id);
      return;
    }
  }
}

void DaoMcpSettingsHandler::HandleRevokeDaoMcpTab(const base::ListValue& args) {
  if (args.size() != 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  for (const auto& connection : service_->GetConnections()) {
    if (connection.connection_id == args[0].GetString() &&
        IsConnectionInCurrentProfile(connection)) {
      service_->RevokeTab(connection.connection_id, args[1].GetString());
      return;
    }
  }
}

bool DaoMcpSettingsHandler::IsConnectionInCurrentProfile(
    const DaoMcpConnectionInfo& connection) {
  auto* contents = web_ui()->GetWebContents();
  auto* browser = connection.browser_window.get();
  return contents && browser &&
         browser->GetProfile() ==
             Profile::FromBrowserContext(contents->GetBrowserContext());
}

void DaoMcpSettingsHandler::HandleResetDaoMcpUsageStats(
    const base::ListValue& args) {
  CHECK(args.empty());
  ResetDaoMcpUsageStats(GetPrefs(), base::Time::Now());
}

void DaoMcpSettingsHandler::HandleGetDaoLocalAgents(
    const base::ListValue& args) {
  CHECK_EQ(1u, args.size());
  AllowJavascript();
  std::string callback_id = args[0].GetString();
#if BUILDFLAG(IS_MAC)
  RequestDaoLocalAgentIntegrationStatus(
      base::BindOnce(&DaoMcpSettingsHandler::OnLocalAgentStatus,
                     weak_factory_.GetWeakPtr(), std::move(callback_id)));
#else
  OnLocalAgentStatus(std::move(callback_id),
                     base::DictValue()
                         .Set("agents", base::ListValue())
                         .Set("cliLinked", false)
                         .Set("cliNeedsUpdate", false));
#endif
}

void DaoMcpSettingsHandler::HandleSetDaoLocalAgentInstalled(
    const base::ListValue& args) {
  CHECK_EQ(3u, args.size());
  const std::string& id = args[1].GetString();
  CHECK(id == "shared" || id == "claude" || id == "cli");
  CHECK(args[2].is_bool());
  AllowJavascript();
  std::string callback_id = args[0].GetString();
#if BUILDFLAG(IS_MAC)
  RequestDaoLocalAgentIntegrationUpdate(
      id, args[2].GetBool(),
      base::BindOnce(&DaoMcpSettingsHandler::OnLocalAgentStatus,
                     weak_factory_.GetWeakPtr(), std::move(callback_id)));
#else
  OnLocalAgentStatus(std::move(callback_id),
                     base::DictValue()
                         .Set("agents", base::ListValue())
                         .Set("cliLinked", false)
                         .Set("cliNeedsUpdate", false)
                         .Set("success", false));
#endif
}

void DaoMcpSettingsHandler::OnLocalAgentStatus(std::string callback_id,
                                               base::DictValue status) {
  if (IsJavascriptAllowed()) {
    ResolveJavascriptCallback(base::Value(callback_id), status);
  }
}

PrefService* DaoMcpSettingsHandler::GetPrefs() {
  content::WebContents* web_contents = web_ui()->GetWebContents();
  Profile* profile =
      web_contents
          ? Profile::FromBrowserContext(web_contents->GetBrowserContext())
          : nullptr;
  return profile ? profile->GetOriginalProfile()->GetPrefs() : nullptr;
}

std::optional<std::string> DaoMcpSettingsHandler::GetSetupContent(
    std::string_view option_id) const {
  if (!DaoMcpService::IsSupported()) {
    return std::nullopt;
  }
  if (option_id == "generic-mcp") {
    std::optional<base::Value> configuration = base::JSONReader::Read(
        service_->GetMcpConfiguration(), base::JSON_PARSE_RFC);
    if (!configuration) {
      return std::nullopt;
    }
    std::string pretty_configuration;
    if (!base::JSONWriter::WriteWithOptions(
            *configuration, base::JSONWriter::OPTIONS_PRETTY_PRINT,
            &pretty_configuration)) {
      return std::nullopt;
    }
    return pretty_configuration;
  }
#if BUILDFLAG(IS_MAC)
  return BuildDaoMcpInstallCommandForBundle(base::apple::OuterBundlePath(),
                                             option_id);
#else
  return std::nullopt;
#endif
}

base::DictValue DaoMcpSettingsHandler::CreateStatusValue() {
  const DaoMcpServiceStatus status = service_->GetStatus();
  base::DictValue result;
  result.Set("enabled", service_->IsEnabled());
  result.Set("state", StatusName(status.state));

  result.Set("usageStats", BuildDaoMcpUsageStats(GetPrefs()));
  base::ListValue connections;
  for (const auto& connection : service_->GetConnections()) {
    if (!IsConnectionInCurrentProfile(connection)) {
      continue;
    }
    base::DictValue item;
    item.Set("connectionId", connection.connection_id);
    item.Set("clientName", SanitizeClientName(connection.client.name));
    item.Set("clientVersion", SanitizeClientName(connection.client.version));
    if (connection.client.verified_pid) {
      item.Set("clientPid", static_cast<int>(*connection.client.verified_pid));
    }
    auto* browser = connection.browser_window.get();
    item.Set("windowLabel", l10n_util::GetStringFUTF16(
                                IDS_DAO_MCP_APPROVAL_WINDOW,
                                WindowMetadataController::From(browser)
                                    ->GetWindowTitleForCurrentTab(false)));
    item.Set("canCreateTabs", connection.can_create_tabs);
    item.Set("pageCountLabel",
             l10n_util::GetStringFUTF16(
                 IDS_DAO_MCP_CONTROL_TAB_COUNT,
                 base::NumberToString16(connection.tabs.size())));
    base::ListValue tabs;
    for (const auto& tab : connection.tabs) {
      base::DictValue page;
      page.Set("tabId", tab.target.tab_id);
      page.Set("title", tab.title);
      page.Set("url", tab.target.url);
      tabs.Append(std::move(page));
    }
    item.Set("tabs", std::move(tabs));
    connections.Append(std::move(item));
  }
  result.Set("canStop", !connections.empty());
  result.Set("connections", std::move(connections));
  return result;
}

void DaoMcpSettingsHandler::OnStatusChanged(const DaoMcpServiceStatus&) {
  FireWebUIListener(kStatusChangedEvent, CreateStatusValue());
}

void DaoMcpSettingsHandler::OnUsageStatsChanged() {
  if (IsJavascriptAllowed()) {
    FireWebUIListener(kStatusChangedEvent, CreateStatusValue());
  }
}

}  // namespace dao
