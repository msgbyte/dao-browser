// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_WELCOME_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_WELCOME_UI_H_

#include <memory>

#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"

namespace dao {

// WebUI config for dao://welcome
class DaoWelcomeUIConfig : public content::WebUIConfig {
 public:
  DaoWelcomeUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// Message handler for the welcome WebUI.
class DaoWelcomeUIHandler : public content::WebUIMessageHandler {
 public:
  DaoWelcomeUIHandler();
  ~DaoWelcomeUIHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;

 private:
  void HandleMarkWelcomeShown(const base::ListValue& args);

  base::WeakPtrFactory<DaoWelcomeUIHandler> weak_factory_{this};
};

// WebUI controller for dao://welcome
class DaoWelcomeUI : public content::WebUIController {
 public:
  explicit DaoWelcomeUI(content::WebUI* web_ui);
  ~DaoWelcomeUI() override;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_WELCOME_UI_H_
