// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_settings_handler.h"

#include <string_view>
#include <utility>

#include "base/apple/bundle_locations.h"
#include "base/check.h"
#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/common/chrome_paths.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui.h"
#include "dao/browser/dao_pref_names.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"

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
    return local_state &&
           local_state->GetBoolean(prefs::kDaoMcpServerEnabled);
  }

  DaoMcpServiceStatus GetStatus() const override {
    return DaoMcpService::Get()->GetStatus();
  }

  void SetEnabled(bool enabled) override {
    DaoMcpService::Get()->SetEnabled(enabled);
  }

  void StopControl() override { DaoMcpService::Get()->StopControl(); }

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
}

void DaoMcpSettingsHandler::OnJavascriptAllowed() {
  status_subscription_ = service_->AddObserver(base::BindRepeating(
      &DaoMcpSettingsHandler::OnStatusChanged, base::Unretained(this)));
}

void DaoMcpSettingsHandler::OnJavascriptDisallowed() {
  status_subscription_ = {};
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
  CHECK(args.empty());
  service_->StopControl();
}

std::optional<std::string> DaoMcpSettingsHandler::GetSetupContent(
    std::string_view option_id) const {
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
  return BuildDaoMcpInstallCommandForBundle(base::apple::OuterBundlePath(),
                                             option_id);
}

base::DictValue DaoMcpSettingsHandler::CreateStatusValue() const {
  const DaoMcpServiceStatus status = service_->GetStatus();
  base::DictValue result;
  result.Set("enabled", service_->IsEnabled());
  result.Set("state", StatusName(status.state));

  const bool can_stop = status.state == DaoMcpStatus::kLeaseActive;
  result.Set("canStop", can_stop);
  if (can_stop && status.client) {
    result.Set("clientName", SanitizeClientName(status.client->name));
    if (status.client->verified_pid) {
      result.Set("clientPid",
                 static_cast<int>(*status.client->verified_pid));
    }
  }
  return result;
}

void DaoMcpSettingsHandler::OnStatusChanged(const DaoMcpServiceStatus&) {
  FireWebUIListener(kStatusChangedEvent, CreateStatusValue());
}

}  // namespace dao
