// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_SETTINGS_HANDLER_H_
#define DAO_BROWSER_MCP_DAO_MCP_SETTINGS_HANDLER_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/webui/settings/settings_page_ui_handler.h"
#include "dao/browser/mcp/dao_mcp_service.h"

namespace dao {

std::optional<std::string> BuildDaoMcpInstallCommandForBundle(
    const base::FilePath& bundle_path,
    std::string_view client_id);

class DaoMcpSettingsService {
 public:
  using StatusObserver =
      base::RepeatingCallback<void(const DaoMcpServiceStatus&)>;

  virtual ~DaoMcpSettingsService() = default;

  virtual bool IsEnabled() const = 0;
  virtual DaoMcpServiceStatus GetStatus() const = 0;
  virtual void SetEnabled(bool enabled) = 0;
  virtual void StopControl() = 0;
  virtual std::string GetMcpConfiguration() const = 0;
  virtual base::CallbackListSubscription AddObserver(
      StatusObserver observer) = 0;
};

class DaoMcpSettingsHandler : public settings::SettingsPageUIHandler {
 public:
  DaoMcpSettingsHandler();
  explicit DaoMcpSettingsHandler(DaoMcpSettingsService* service);

  DaoMcpSettingsHandler(const DaoMcpSettingsHandler&) = delete;
  DaoMcpSettingsHandler& operator=(const DaoMcpSettingsHandler&) = delete;

  ~DaoMcpSettingsHandler() override;

  // settings::SettingsPageUIHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

 protected:
  void HandleGetDaoMcpStatus(const base::ListValue& args);
  void HandleSetDaoMcpEnabled(const base::ListValue& args);
  void HandleGetDaoMcpSetupContent(const base::ListValue& args);
  void HandleCopyDaoMcpSetupContent(const base::ListValue& args);
  void HandleStopDaoMcpControl(const base::ListValue& args);

 private:
  std::optional<std::string> GetSetupContent(
      std::string_view option_id) const;
  base::DictValue CreateStatusValue() const;
  void OnStatusChanged(const DaoMcpServiceStatus&);

  std::unique_ptr<DaoMcpSettingsService> owned_service_;
  raw_ptr<DaoMcpSettingsService> service_;
  base::CallbackListSubscription status_subscription_;
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_SETTINGS_HANDLER_H_
