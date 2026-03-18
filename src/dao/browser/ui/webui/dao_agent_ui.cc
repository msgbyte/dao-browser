// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_agent_ui.h"

#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "chrome/grit/dao_agent_resources.h"
#include "chrome/grit/dao_agent_resources_map.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

namespace dao {

// ---- DaoAgentDevToolsClient ----

DaoAgentDevToolsClient::DaoAgentDevToolsClient() = default;

DaoAgentDevToolsClient::~DaoAgentDevToolsClient() {
  Detach();
}

bool DaoAgentDevToolsClient::AttachTo(content::WebContents* web_contents) {
  if (!web_contents) {
    return false;
  }

  scoped_refptr<content::DevToolsAgentHost> host =
      content::DevToolsAgentHost::GetOrCreateFor(web_contents);
  if (!host) {
    return false;
  }

  // If already attached to this host, reuse.
  if (agent_host_ == host) {
    return true;
  }

  // Detach from previous host.
  Detach();

  agent_host_ = host;
  if (!agent_host_->AttachClient(this)) {
    agent_host_ = nullptr;
    return false;
  }

  return true;
}

void DaoAgentDevToolsClient::Detach() {
  if (agent_host_) {
    agent_host_->DetachClient(this);
    agent_host_ = nullptr;
  }
  pending_callbacks_.clear();
}

void DaoAgentDevToolsClient::SendCommand(const std::string& method,
                                          base::Value::Dict params,
                                          ResponseCallback callback) {
  if (!agent_host_) {
    std::move(callback).Run(base::Value("Not attached"));
    return;
  }

  int id = next_command_id_++;
  pending_callbacks_[id] = std::move(callback);

  base::Value::Dict command;
  command.Set("id", id);
  command.Set("method", method);
  command.Set("params", std::move(params));

  std::string json;
  base::JSONWriter::Write(command, &json);

  agent_host_->DispatchProtocolMessage(
      this, base::as_byte_span(json));
}

void DaoAgentDevToolsClient::DispatchProtocolMessage(
    content::DevToolsAgentHost* agent_host,
    base::span<const uint8_t> message) {
  std::string json_str(reinterpret_cast<const char*>(message.data()),
                       message.size());

  auto parsed = base::JSONReader::Read(json_str);
  if (!parsed || !parsed->is_dict()) {
    return;
  }

  auto& dict = parsed->GetDict();
  auto id = dict.FindInt("id");
  if (!id) {
    return;  // Event or notification — ignore for now.
  }

  auto it = pending_callbacks_.find(*id);
  if (it == pending_callbacks_.end()) {
    return;
  }

  ResponseCallback callback = std::move(it->second);
  pending_callbacks_.erase(it);

  auto* result = dict.Find("result");
  auto* error = dict.Find("error");

  if (error) {
    std::move(callback).Run(error->Clone());
  } else if (result) {
    std::move(callback).Run(result->Clone());
  } else {
    std::move(callback).Run(base::Value());
  }
}

void DaoAgentDevToolsClient::AgentHostClosed(
    content::DevToolsAgentHost* agent_host) {
  agent_host_ = nullptr;
  pending_callbacks_.clear();
}

bool DaoAgentDevToolsClient::IsTrusted() {
  return true;
}

std::string DaoAgentDevToolsClient::GetTypeForMetrics() {
  return "Other";
}

// ---- DaoAgentUIHandler ----

DaoAgentUIHandler::DaoAgentUIHandler()
    : devtools_client_(std::make_unique<DaoAgentDevToolsClient>()) {}

DaoAgentUIHandler::~DaoAgentUIHandler() = default;

void DaoAgentUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getPageContent",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetPageContent,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPageInfo",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetPageInfo,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clickElement",
      base::BindRepeating(&DaoAgentUIHandler::HandleClickElement,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "executeScript",
      base::BindRepeating(&DaoAgentUIHandler::HandleExecuteScript,
                          base::Unretained(this)));
}

content::WebContents* DaoAgentUIHandler::EnsureAttached() {
  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  if (!browser) {
    return nullptr;
  }

  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return nullptr;
  }

  // Don't attach to the agent page itself.
  if (contents->GetURL().host() == "agent") {
    return nullptr;
  }

  if (!devtools_client_->AttachTo(contents)) {
    return nullptr;
  }

  return contents;
}

void DaoAgentUIHandler::HandleGetPageContent(const base::Value::List& args) {
  AllowJavascript();

  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::Dict()));
    return;
  }

  base::Value::Dict params;
  params.Set("expression",
             "document.body ? document.body.innerText : ''");
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) {
              return;
            }
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value = result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                response.Set("text", value->GetString());
              }
            }
            handler->ResolveJavascriptCallback(base::Value(callback_id),
                                               response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleGetPageInfo(const base::Value::List& args) {
  AllowJavascript();

  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  content::WebContents* contents = nullptr;
  if (browser) {
    contents = browser->tab_strip_model()->GetActiveWebContents();
  }

  base::Value::Dict response;
  if (contents) {
    response.Set("url", contents->GetURL().spec());
    response.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleClickElement(const base::Value::List& args) {
  AllowJavascript();

  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  std::string selector;
  if (args[1].is_dict()) {
    auto* sel = args[1].GetDict().FindString("selector");
    if (sel) {
      selector = *sel;
    }
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || selector.empty()) {
    base::Value::Dict response;
    response.Set("error", "No active tab or invalid selector");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Escape single quotes in selector for safe JS injection.
  std::string escaped_selector;
  for (char c : selector) {
    if (c == '\'') {
      escaped_selector += "\\'";
    } else if (c == '\\') {
      escaped_selector += "\\\\";
    } else {
      escaped_selector += c;
    }
  }

  std::string js = "(function() { var el = document.querySelector('" +
                   escaped_selector +
                   "'); if (el) { el.click(); return 'clicked'; } "
                   "return 'element not found'; })()";

  base::Value::Dict params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) {
              return;
            }
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value = result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                response.Set("result", value->GetString());
              }
            }
            handler->ResolveJavascriptCallback(base::Value(callback_id),
                                               response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleExecuteScript(const base::Value::List& args) {
  AllowJavascript();

  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  std::string code;
  if (args[1].is_dict()) {
    auto* c = args[1].GetDict().FindString("code");
    if (c) {
      code = *c;
    }
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || code.empty()) {
    base::Value::Dict response;
    response.Set("error", "No active tab or empty code");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  base::Value::Dict params;
  params.Set("expression", code);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) {
              return;
            }
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value = result.GetDict().FindByDottedPath("result.value");
              if (value) {
                if (value->is_string()) {
                  response.Set("result", value->GetString());
                } else {
                  std::string json;
                  base::JSONWriter::Write(*value, &json);
                  response.Set("result", json);
                }
              }
              auto* desc =
                  result.GetDict().FindByDottedPath("exceptionDetails");
              if (desc) {
                std::string json;
                base::JSONWriter::Write(*desc, &json);
                response.Set("error", json);
              }
            }
            handler->ResolveJavascriptCallback(base::Value(callback_id),
                                               response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

// ---- DaoAgentUIConfig ----

DaoAgentUIConfig::DaoAgentUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "agent") {}

std::unique_ptr<content::WebUIController>
DaoAgentUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                        const GURL& url) {
  return std::make_unique<DaoAgentUI>(web_ui);
}

// ---- DaoAgentUI ----

DaoAgentUI::DaoAgentUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "agent");

  // Serve resource files from the GRD-generated resource map.
  source->AddResourcePaths(kDaoAgentResources);
  source->SetDefaultResource(IDR_DAO_AGENT_AGENT_HTML);

  // Allow the page to fetch external APIs (OpenAI etc.)
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc,
      "connect-src * 'self';");

  // Register the message handler.
  web_ui->AddMessageHandler(std::make_unique<DaoAgentUIHandler>());
}

DaoAgentUI::~DaoAgentUI() = default;

}  // namespace dao
