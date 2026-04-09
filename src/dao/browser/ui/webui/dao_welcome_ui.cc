// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_welcome_ui.h"

#include <string>

#include "base/logging.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/grit/dao_welcome_resources.h"
#include "chrome/grit/dao_welcome_resources_map.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/dao_pref_names.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace dao {

// ---- DaoWelcomeUIConfig ----

DaoWelcomeUIConfig::DaoWelcomeUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "welcome") {}

std::unique_ptr<content::WebUIController>
DaoWelcomeUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                           const GURL& url) {
  return std::make_unique<DaoWelcomeUI>(web_ui);
}

// ---- DaoWelcomeUIHandler ----

DaoWelcomeUIHandler::DaoWelcomeUIHandler() = default;

DaoWelcomeUIHandler::~DaoWelcomeUIHandler() = default;

void DaoWelcomeUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "markWelcomeShown",
      base::BindRepeating(&DaoWelcomeUIHandler::HandleMarkWelcomeShown,
                          base::Unretained(this)));
}

void DaoWelcomeUIHandler::HandleMarkWelcomeShown(
    const base::Value::List& args) {
  AllowJavascript();
  Profile* profile = Profile::FromWebUI(web_ui());
  profile->GetPrefs()->SetBoolean(prefs::kDaoWelcomeShown, true);
}

// ---- DaoWelcomeUI ----

DaoWelcomeUI::DaoWelcomeUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "welcome");

  source->AddResourcePaths(kDaoWelcomeResources);
  source->SetDefaultResource(IDR_DAO_WELCOME_WELCOME_HTML);

  // Allow innerHTML for Lit rendering.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop;");

  web_ui->AddMessageHandler(std::make_unique<DaoWelcomeUIHandler>());
}

DaoWelcomeUI::~DaoWelcomeUI() = default;

}  // namespace dao
