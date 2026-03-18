// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_agent_ui.h"

#include "base/memory/ref_counted_memory.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"

namespace dao {

namespace {

const char kAgentPageHTML[] = R"(<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <style>
    body {
      margin: 0; padding: 24px;
      font-family: system-ui, sans-serif;
      background: rgb(40, 32, 48);
      color: rgba(255,255,255,0.87);
    }
    h1 { font-size: 16px; font-weight: 600; }
    p  { font-size: 13px; color: rgba(255,255,255,0.59); }
  </style>
</head>
<body>
  <h1>Dao Agent</h1>
  <p>This is the Dao Agent sidebar.</p>
</body>
</html>
)";

}  // namespace

// DaoAgentUIConfig

DaoAgentUIConfig::DaoAgentUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "agent") {}

std::unique_ptr<content::WebUIController>
DaoAgentUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                        const GURL& url) {
  return std::make_unique<DaoAgentUI>(web_ui);
}

// DaoAgentUI

DaoAgentUI::DaoAgentUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "agent");

  source->SetRequestFilter(
      base::BindRepeating([](const std::string&) { return true; }),
      base::BindRepeating(
          [](const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            std::move(callback).Run(
                base::MakeRefCounted<base::RefCountedString>(
                    std::string(kAgentPageHTML)));
          }));
}

DaoAgentUI::~DaoAgentUI() = default;

}  // namespace dao
