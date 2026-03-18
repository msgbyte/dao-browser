// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_

#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/webui_config.h"

namespace dao {

class DaoAgentUI;

class DaoAgentUIConfig : public content::WebUIConfig {
 public:
  DaoAgentUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

class DaoAgentUI : public content::WebUIController {
 public:
  explicit DaoAgentUI(content::WebUI* web_ui);
  ~DaoAgentUI() override;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_AGENT_UI_H_
