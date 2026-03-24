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
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/dao_pref_names.h"
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

// ---- DaoAgentMemoryHandler ----

DaoAgentMemoryHandler::DaoAgentMemoryHandler() = default;

DaoAgentMemoryHandler::~DaoAgentMemoryHandler() {
  if (proactive_engine_) {
    proactive_engine_->Stop();
  }
}

DaoAgentMemoryService* DaoAgentMemoryHandler::GetMemoryService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return DaoAgentMemoryServiceFactory::GetForProfile(profile);
}

void DaoAgentMemoryHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getMemoryContext",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleGetMemoryContext,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "endSession",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleEndSession,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "loadConversations",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleLoadConversations,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPreferences",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleGetPreferences,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "updatePreference",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleUpdatePreference,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteMemory",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleDeleteMemory,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getEpisodes",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleGetEpisodes,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearAllMemory",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleClearAllMemory,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getStorageStats",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleGetStorageStats,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "dismissSuggestion",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleDismissSuggestion,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "acceptSuggestion",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleAcceptSuggestion,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getMemoryEnabled",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleGetMemoryEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setMemoryEnabled",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleSetMemoryEnabled,
                          base::Unretained(this)));
}

void DaoAgentMemoryHandler::OnJavascriptAllowed() {
  auto* service = GetMemoryService();
  if (!service) {
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  proactive_engine_ =
      std::make_unique<DaoAgentProactiveEngine>(service, profile);
  proactive_engine_->SetDelegate(this);
  proactive_engine_->Start();
}

void DaoAgentMemoryHandler::OnJavascriptDisallowed() {
  if (proactive_engine_) {
    proactive_engine_->Stop();
    proactive_engine_.reset();
  }
}

void DaoAgentMemoryHandler::OnProactiveSuggestion(
    const ProactiveSuggestion& suggestion) {
  base::Value::Dict dict;
  dict.Set("episodeId", static_cast<int>(suggestion.episode_id));
  dict.Set("text", suggestion.text);
  dict.Set("confidence", suggestion.confidence);
  dict.Set("type", suggestion.type);
  FireWebUIListener("proactiveSuggestion", dict);
}

void DaoAgentMemoryHandler::HandleGetMemoryContext(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string url = args[1].is_string() ? args[1].GetString() : "";
  const std::string domain = args[2].is_string() ? args[2].GetString() : "";
  const std::string session_id = args[3].is_string() ? args[3].GetString() : "";

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::Dict()));
    return;
  }

  service->GetMemoryContext(
      url, domain, session_id,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, MemoryContext ctx) {
            if (!handler) {
              return;
            }
            base::Value::Dict result;

            // Preferences
            base::Value::List prefs;
            for (const auto& p : ctx.preferences) {
              base::Value::Dict pref;
              pref.Set("key", p.key);
              pref.Set("value", p.value);
              pref.Set("confidence", p.confidence);
              prefs.Append(std::move(pref));
            }
            result.Set("preferences", std::move(prefs));

            // Episodes
            base::Value::List eps;
            for (const auto& e : ctx.episodes) {
              base::Value::Dict ep;
              ep.Set("intent", e.intent);
              ep.Set("outcome", e.outcome);
              ep.Set("timestamp",
                     static_cast<double>(
                         e.timestamp.ToDeltaSinceWindowsEpoch()
                             .InMicroseconds()));
              eps.Append(std::move(ep));
            }
            result.Set("episodes", std::move(eps));

            // Recent messages
            base::Value::List msgs;
            for (const auto& m : ctx.recent_messages) {
              base::Value::Dict msg;
              msg.Set("role", m.role);
              msg.Set("content", m.content);
              msgs.Append(std::move(msg));
            }
            result.Set("recentMessages", std::move(msgs));

            handler->ResolveJavascriptCallback(base::Value(cb_id), result);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleEndSession(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string session_id =
      args[1].is_string() ? args[1].GetString() : "";

  auto* service = GetMemoryService();
  if (!service || session_id.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  // Parse messages from the JS array.
  std::vector<ConversationMessage> messages;
  if (args[2].is_list()) {
    for (const auto& item : args[2].GetList()) {
      if (!item.is_dict()) {
        continue;
      }
      const auto& d = item.GetDict();
      ConversationMessage msg;
      msg.session_id = session_id;
      if (auto* role = d.FindString("role")) {
        msg.role = *role;
      }
      if (auto* content = d.FindString("content")) {
        msg.content = *content;
      }
      msg.timestamp = base::Time::Now();
      if (auto* url = d.FindString("pageUrl")) {
        msg.page_url = *url;
      }
      if (auto* title = d.FindString("pageTitle")) {
        msg.page_title = *title;
      }
      messages.push_back(std::move(msg));
    }
  }

  service->SaveConversationMessages(
      session_id, std::move(messages),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, bool success) {
            if (!handler) {
              return;
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id),
                                               base::Value(success));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleLoadConversations(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  int limit = args[1].is_int() ? args[1].GetInt() : 20;

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::List()));
    return;
  }

  service->LoadRecentMessages(
      limit,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id,
             std::vector<ConversationMessage> messages) {
            if (!handler) {
              return;
            }
            base::Value::List list;
            for (const auto& m : messages) {
              base::Value::Dict msg;
              msg.Set("sessionId", m.session_id);
              msg.Set("role", m.role);
              msg.Set("content", m.content);
              msg.Set("timestamp",
                      static_cast<double>(
                          m.timestamp.ToDeltaSinceWindowsEpoch()
                              .InMicroseconds()));
              list.Append(std::move(msg));
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), list);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleGetPreferences(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::List()));
    return;
  }

  service->GetPreferences(
      100, 0.0,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, std::vector<Preference> prefs) {
            if (!handler) {
              return;
            }
            base::Value::List list;
            for (const auto& p : prefs) {
              base::Value::Dict pref;
              pref.Set("id", static_cast<int>(p.id));
              pref.Set("key", p.key);
              pref.Set("value", p.value);
              pref.Set("confidence", p.confidence);
              pref.Set("evidenceCount", p.evidence_count);
              list.Append(std::move(pref));
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), list);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleUpdatePreference(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string key = args[1].is_string() ? args[1].GetString() : "";
  const std::string value = args[2].is_string() ? args[2].GetString() : "";
  double confidence = args[3].is_double() ? args[3].GetDouble() : 0.5;

  auto* service = GetMemoryService();
  if (!service || key.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->MergePreference(
      key, value, confidence,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, bool success) {
            if (!handler) {
              return;
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id),
                                               base::Value(success));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleDeleteMemory(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string type = args[1].is_string() ? args[1].GetString() : "";
  // id can be int or string (session_id for conversations)

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  if (type == "preference" && args[2].is_int()) {
    service->DeletePreference(
        args[2].GetInt(),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentMemoryHandler> handler,
               std::string cb_id, bool success) {
              if (!handler) {
                return;
              }
              handler->ResolveJavascriptCallback(base::Value(cb_id),
                                                 base::Value(success));
            },
            weak_factory_.GetWeakPtr(), callback_id));
  } else if (type == "episode" && args[2].is_int()) {
    service->DeleteEpisode(
        args[2].GetInt(),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentMemoryHandler> handler,
               std::string cb_id, bool success) {
              if (!handler) {
                return;
              }
              handler->ResolveJavascriptCallback(base::Value(cb_id),
                                                 base::Value(success));
            },
            weak_factory_.GetWeakPtr(), callback_id));
  } else if (type == "conversation" && args[2].is_string()) {
    service->DeleteConversation(
        args[2].GetString(),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentMemoryHandler> handler,
               std::string cb_id, bool success) {
              if (!handler) {
                return;
              }
              handler->ResolveJavascriptCallback(base::Value(cb_id),
                                                 base::Value(success));
            },
            weak_factory_.GetWeakPtr(), callback_id));
  } else {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
  }
}

void DaoAgentMemoryHandler::HandleGetEpisodes(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string domain = args[1].is_string() ? args[1].GetString() : "";
  int limit = args[2].is_int() ? args[2].GetInt() : 50;

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::List()));
    return;
  }

  service->GetEpisodesByDomain(
      domain, limit,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, std::vector<Episode> episodes) {
            if (!handler) {
              return;
            }
            base::Value::List list;
            for (const auto& e : episodes) {
              base::Value::Dict ep;
              ep.Set("id", static_cast<int>(e.id));
              ep.Set("domain", e.domain);
              ep.Set("url", e.url);
              ep.Set("title", e.title);
              ep.Set("intent", e.intent);
              ep.Set("outcome", e.outcome);
              ep.Set("toolsUsed", e.tools_used);
              ep.Set("timestamp",
                     static_cast<double>(
                         e.timestamp.ToDeltaSinceWindowsEpoch()
                             .InMicroseconds()));
              ep.Set("confidence", e.confidence);
              list.Append(std::move(ep));
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), list);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleClearAllMemory(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->ClearAll(base::BindOnce(
      [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
         bool success) {
        if (!handler) {
          return;
        }
        handler->ResolveJavascriptCallback(base::Value(cb_id),
                                           base::Value(success));
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleGetStorageStats(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::Dict()));
    return;
  }

  service->GetStorageStats(base::BindOnce(
      [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
         StorageStats stats) {
        if (!handler) {
          return;
        }
        base::Value::Dict result;
        result.Set("totalSize", static_cast<int>(stats.total_size_bytes));
        result.Set("conversationCount", stats.conversation_count);
        result.Set("episodeCount", stats.episode_count);
        result.Set("preferenceCount", stats.preference_count);
        handler->ResolveJavascriptCallback(base::Value(cb_id), result);
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleDismissSuggestion(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  int64_t episode_id = args[1].is_int() ? args[1].GetInt() : 0;

  auto* service = GetMemoryService();
  if (!service || episode_id == 0) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  // Lower confidence by 0.1 on dismiss.
  service->UpdateEpisodeConfidence(
      episode_id, -1.0,  // Will be clamped in a smarter way if needed.
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, bool success) {
            if (!handler) {
              return;
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id),
                                               base::Value(success));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleAcceptSuggestion(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  int64_t episode_id = args[1].is_int() ? args[1].GetInt() : 0;

  auto* service = GetMemoryService();
  if (!service || episode_id == 0) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  // Boost confidence by updating to max.
  service->UpdateEpisodeConfidence(
      episode_id, 1.0,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, bool success) {
            if (!handler) {
              return;
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id),
                                               base::Value(success));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleGetMemoryEnabled(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  bool enabled = profile->GetPrefs()->GetBoolean(prefs::kDaoAgentMemoryEnabled);
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(enabled));
}

void DaoAgentMemoryHandler::HandleSetMemoryEnabled(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  bool enabled = args[1].GetBool();
  Profile* profile = Profile::FromWebUI(web_ui());
  profile->GetPrefs()->SetBoolean(prefs::kDaoAgentMemoryEnabled, enabled);
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
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

  // Allow innerHTML usage (streaming markdown rendering).
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default;");

  // Register message handlers.
  web_ui->AddMessageHandler(std::make_unique<DaoAgentUIHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentMemoryHandler>());
}

DaoAgentUI::~DaoAgentUI() = default;

}  // namespace dao
