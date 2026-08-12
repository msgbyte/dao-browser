// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_IMPORT_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_IMPORT_UI_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "dao/browser/import/dao_migration_types.h"

namespace dao::import {
class DaoMigrationService;
}

namespace dao {

class DaoImportUIConfig : public content::WebUIConfig {
public:
  DaoImportUIConfig();
  std::unique_ptr<content::WebUIController>
  CreateWebUIController(content::WebUI *web_ui, const GURL &url) override;
};

class DaoImportUIHandler : public content::WebUIMessageHandler {
public:
  explicit DaoImportUIHandler(import::DaoMigrationService *service);
  ~DaoImportUIHandler() override;

  void RegisterMessages() override;

private:
  void HandleDetectSources(const base::ListValue &args);
  void HandleCountSourceItems(const base::ListValue &args);
  void HandleGetState(const base::ListValue &args);
  void HandleStart(const base::ListValue &args);
  void HandleCancel(const base::ListValue &args);
  void HandleRetry(const base::ListValue &args);
  void ReplyWithSources(std::string callback_id,
                        std::vector<import::SourceProfile> sources);
  void ReplyWithCount(std::string callback_id, std::optional<uint64_t> count);
  void OnStateChanged(const import::JobState &state);

  raw_ptr<import::DaoMigrationService> service_;
  base::CallbackListSubscription state_subscription_;
  base::WeakPtrFactory<DaoImportUIHandler> weak_ptr_factory_{this};
};

class DaoImportUI : public content::WebUIController {
public:
  explicit DaoImportUI(content::WebUI *web_ui);
  ~DaoImportUI() override;
};

} // namespace dao

#endif // DAO_BROWSER_UI_WEBUI_DAO_IMPORT_UI_H_
