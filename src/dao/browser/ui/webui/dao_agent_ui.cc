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
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/url_constants.h"
#include "chrome/grit/dao_agent_resources.h"
#include "chrome/grit/dao_agent_resources_map.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_agent_skill_service.h"
#include "dao/browser/agent/dao_agent_skill_service_factory.h"
#include "dao/browser/dao_pref_names.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"

namespace dao {

namespace {

// Populate an ActionFeedback from a JS dict. Caller sets outcome separately.
ActionFeedback ParseActionFeedbackFromDict(const base::Value::Dict& d) {
  ActionFeedback feedback;
  if (auto* sid = d.FindString("scenarioId")) {
    feedback.scenario_id = *sid;
  }
  if (auto* label = d.FindString("actionLabel")) {
    feedback.action_label = *label;
  }
  if (auto* domain = d.FindString("domain")) {
    feedback.domain = *domain;
  }
  if (auto* url = d.FindString("url")) {
    feedback.url = *url;
  }
  if (auto* outcome = d.FindString("outcome")) {
    feedback.outcome = *outcome;
  }
  feedback.trigger_confidence = d.FindDouble("confidence").value_or(0.0);
  feedback.timestamp = base::Time::Now();
  return feedback;
}

void UnlockLockedTab(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  DaoAgentLockTabHelper::UnlockContents(contents);
}

// JS to inject highlight infrastructure into a page.
// Creates a Shadow DOM container so styles don't leak.
constexpr char kHighlightInjectScript[] = R"js(
(function() {
  if (window.__dao_agent__) return;
  const host = document.createElement('div');
  host.id = 'dao-agent-overlay-host';
  host.style.cssText = 'position:fixed;top:0;left:0;width:0;height:0;z-index:2147483647;pointer-events:none;';
  document.documentElement.appendChild(host);
  const shadow = host.attachShadow({mode:'closed'});
  const highlight = document.createElement('div');
  highlight.style.cssText = 'position:fixed;border:2px solid rgba(140,100,220,0.6);background:rgba(140,100,220,0.08);border-radius:0;pointer-events:none;opacity:0;transition:opacity 150ms ease;display:none;';
  shadow.appendChild(highlight);
  let rafId = 0;
  let currentEl = null;
  function updatePos() {
    if (!currentEl || !currentEl.isConnected) { highlight.style.display='none'; return; }
    const r = currentEl.getBoundingClientRect();
    const br = getComputedStyle(currentEl).borderRadius || '0';
    highlight.style.left = r.left + 'px';
    highlight.style.top = r.top + 'px';
    highlight.style.width = r.width + 'px';
    highlight.style.height = r.height + 'px';
    highlight.style.borderRadius = br;
    rafId = requestAnimationFrame(updatePos);
  }
  window.__dao_agent__ = {
    showHighlight: function(selector) {
      const el = document.querySelector(selector);
      if (!el) return false;
      currentEl = el;
      highlight.style.display = 'block';
      highlight.style.opacity = '1';
      cancelAnimationFrame(rafId);
      updatePos();
      return true;
    },
    clearHighlight: function() {
      highlight.style.opacity = '0';
      currentEl = null;
      cancelAnimationFrame(rafId);
      setTimeout(function(){ highlight.style.display='none'; }, 150);
    }
  };
})()
)js";

// JS to generate an accessibility tree representation of the current page.
// Walks the DOM, assigns data-dao-ref attributes to interactive elements,
// and returns a text tree suitable for LLM consumption.
constexpr char kAccessibilityTreeScript[] = R"js(
(function(filterMode) {
  var MAX_DEPTH = 15;
  var MAX_CHARS = 50000;
  var refCounter = 0;
  var output = '';
  var charCount = 0;
  var truncated = false;

  // Clear old refs.
  var oldRefs = document.querySelectorAll('[data-dao-ref]');
  for (var i = 0; i < oldRefs.length; i++) {
    oldRefs[i].removeAttribute('data-dao-ref');
  }

  var SKIP_TAGS = {
    SCRIPT:1, STYLE:1, NOSCRIPT:1, TEMPLATE:1, IFRAME:1,
    SVG:1, PATH:1, CIRCLE:1, RECT:1, LINE:1, POLYGON:1, POLYLINE:1,
    ELLIPSE:1, DEFS:1, CLIPPATH:1, G:1, USE:1, SYMBOL:1, MASK:1
  };

  function isVisible(el) {
    if (el.offsetWidth === 0 && el.offsetHeight === 0) return false;
    var style = getComputedStyle(el);
    if (style.display === 'none' || style.visibility === 'hidden') return false;
    return true;
  }

  function isInViewport(el) {
    var r = el.getBoundingClientRect();
    return r.bottom > 0 && r.top < window.innerHeight &&
           r.right > 0 && r.left < window.innerWidth;
  }

  function isInteractive(el) {
    var tag = el.tagName;
    if (tag === 'A' || tag === 'BUTTON' || tag === 'INPUT' ||
        tag === 'SELECT' || tag === 'TEXTAREA') return true;
    var role = el.getAttribute('role');
    if (role === 'button' || role === 'link' || role === 'tab' ||
        role === 'menuitem' || role === 'checkbox' || role === 'radio' ||
        role === 'switch' || role === 'option' || role === 'combobox' ||
        role === 'textbox' || role === 'searchbox' || role === 'slider') return true;
    if (el.hasAttribute('onclick') || el.hasAttribute('tabindex')) return true;
    if (el.contentEditable === 'true') return true;
    return false;
  }

  function getRole(el) {
    var role = el.getAttribute('role');
    if (role) return role;
    var tag = el.tagName.toLowerCase();
    var map = {
      a:'link', button:'button', input:'textbox', select:'combobox',
      textarea:'textbox', img:'image', nav:'navigation', main:'main',
      header:'banner', footer:'contentinfo', aside:'complementary',
      form:'form', table:'table', tr:'row', td:'cell', th:'columnheader',
      ul:'list', ol:'list', li:'listitem', h1:'heading', h2:'heading',
      h3:'heading', h4:'heading', h5:'heading', h6:'heading',
      details:'group', summary:'button', dialog:'dialog',
      section:'region', article:'article'
    };
    if (tag === 'input') {
      var t = (el.type || 'text').toLowerCase();
      if (t === 'checkbox') return 'checkbox';
      if (t === 'radio') return 'radio';
      if (t === 'submit' || t === 'button' || t === 'reset') return 'button';
      if (t === 'range') return 'slider';
      return 'textbox';
    }
    return map[tag] || 'generic';
  }

  function getName(el) {
    var name = el.getAttribute('aria-label') ||
               el.getAttribute('alt') ||
               el.getAttribute('title') ||
               el.getAttribute('placeholder') || '';
    if (!name && (el.tagName === 'A' || el.tagName === 'BUTTON' ||
                  el.tagName === 'LABEL')) {
      name = (el.textContent || '').trim();
    }
    if (!name && el.tagName === 'IMG') {
      name = el.getAttribute('src') || '';
      if (name.length > 40) name = '...' + name.slice(-37);
    }
    if (name.length > 80) name = name.substring(0, 77) + '...';
    name = name.replace(/[\n\r\t]+/g, ' ').trim();
    return name;
  }

  function getExtras(el) {
    var parts = [];
    var tag = el.tagName;
    if (tag === 'INPUT') {
      parts.push('type="' + (el.type || 'text') + '"');
      if (el.placeholder) parts.push('placeholder="' + el.placeholder + '"');
      if (el.value) parts.push('value="' + el.value.substring(0,40) + '"');
      if (el.checked) parts.push('checked');
      if (el.disabled) parts.push('disabled');
    }
    if (tag === 'A' && el.href) {
      var h = el.getAttribute('href') || '';
      if (h.length > 60) h = h.substring(0, 57) + '...';
      parts.push('href="' + h + '"');
    }
    if (tag === 'SELECT') {
      var sel = el.options && el.options[el.selectedIndex];
      if (sel) parts.push('selected="' + sel.text + '"');
    }
    if (/^H[1-6]$/.test(tag)) {
      parts.push('level=' + tag[1]);
    }
    if (el.disabled) parts.push('disabled');
    if (el.getAttribute('aria-expanded')) {
      parts.push('expanded=' + el.getAttribute('aria-expanded'));
    }
    return parts.join(' ');
  }

  function appendLine(depth, text) {
    if (truncated) return;
    var line = '  '.repeat(depth) + text + '\n';
    if (charCount + line.length > MAX_CHARS) {
      output += '  '.repeat(depth) + '... (truncated)\n';
      truncated = true;
      return;
    }
    output += line;
    charCount += line.length;
  }

  var elementCount = 0;
  var interactiveCount = 0;

  function walk(el, depth) {
    if (truncated || depth > MAX_DEPTH) return;
    if (SKIP_TAGS[el.tagName]) return;
    if (!isVisible(el)) return;
    if (filterMode === 'visible' && !isInViewport(el)) {
      // Still walk children - a container might be partially visible.
    }

    elementCount++;
    var interactive = isInteractive(el);
    var role = getRole(el);
    var name = getName(el);

    if (filterMode === 'interactive' && !interactive &&
        role === 'generic' && !name) {
      // Skip non-meaningful generic elements, but still walk children.
      for (var c = el.firstElementChild; c; c = c.nextElementSibling) {
        walk(c, depth);
      }
      return;
    }

    var refStr = '';
    if (interactive) {
      interactiveCount++;
      var refId = 'ref_' + (++refCounter);
      el.setAttribute('data-dao-ref', refId);
      refStr = ' [' + refId + ']';
    }

    var nameStr = name ? ' "' + name + '"' : '';
    var extras = getExtras(el);
    var extraStr = extras ? ' ' + extras : '';
    appendLine(depth, role + nameStr + refStr + extraStr);

    // For leaf text nodes in non-interactive elements, show text.
    if (!interactive && el.childElementCount === 0) {
      var text = (el.textContent || '').trim();
      if (text && text.length > 0 && role !== 'generic') {
        // Already shown via name for some roles.
      } else if (text && text.length > 0 && !name) {
        if (text.length > 120) text = text.substring(0, 117) + '...';
        text = text.replace(/[\n\r\t]+/g, ' ');
        if (text) appendLine(depth + 1, '"' + text + '"');
      }
    }

    for (var c = el.firstElementChild; c; c = c.nextElementSibling) {
      walk(c, depth + 1);
    }
  }

  var header = '[viewport: ' + window.innerWidth + 'x' + window.innerHeight +
               ', scroll: ' + Math.round(window.scrollY) + '/' +
               document.documentElement.scrollHeight + ']\n';
  output = header;
  charCount = header.length;

  walk(document.body || document.documentElement, 0);

  return JSON.stringify({
    tree: output,
    viewport: { width: window.innerWidth, height: window.innerHeight },
    scrollY: Math.round(window.scrollY),
    scrollHeight: document.documentElement.scrollHeight,
    elementCount: elementCount,
    interactiveCount: interactiveCount
  });
})
)js";

}  // namespace

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

void DaoAgentDevToolsClient::SetEventCallback(EventCallback callback) {
  event_callback_ = std::move(callback);
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
    // CDP event — dispatch to event callback if set.
    if (event_callback_) {
      auto* method = dict.FindString("method");
      auto* params = dict.FindDict("params");
      if (method) {
        base::Value::Dict empty;
        event_callback_.Run(*method, params ? *params : empty);
      }
    }
    return;
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
    : devtools_client_(std::make_unique<DaoAgentDevToolsClient>()) {
  devtools_client_->SetEventCallback(
      base::BindRepeating(&DaoAgentUIHandler::OnCDPEvent,
                          base::Unretained(this)));
}

DaoAgentUIHandler::~DaoAgentUIHandler() = default;

void DaoAgentUIHandler::RegisterMessages() {
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
  web_ui()->RegisterMessageCallback(
      "moveCursor",
      base::BindRepeating(&DaoAgentUIHandler::HandleMoveCursor,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "agentClick",
      base::BindRepeating(&DaoAgentUIHandler::HandleAgentClick,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "highlightElement",
      base::BindRepeating(&DaoAgentUIHandler::HandleHighlightElement,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearHighlight",
      base::BindRepeating(&DaoAgentUIHandler::HandleClearHighlight,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getAccessibilityTree",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetAccessibilityTree,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clickByRef",
      base::BindRepeating(&DaoAgentUIHandler::HandleClickByRef,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "captureScreenshot",
      base::BindRepeating(&DaoAgentUIHandler::HandleCaptureScreenshot,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "scrollPage",
      base::BindRepeating(&DaoAgentUIHandler::HandleScrollPage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "scrollToElement",
      base::BindRepeating(&DaoAgentUIHandler::HandleScrollToElement,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setExpectedDomain",
      base::BindRepeating(&DaoAgentUIHandler::HandleSetExpectedDomain,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "listTabs",
      base::BindRepeating(&DaoAgentUIHandler::HandleListTabs,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "switchTab",
      base::BindRepeating(&DaoAgentUIHandler::HandleSwitchTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openTab",
      base::BindRepeating(&DaoAgentUIHandler::HandleOpenTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "closeTab",
      base::BindRepeating(&DaoAgentUIHandler::HandleCloseTab,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "pressKeyChord",
      base::BindRepeating(&DaoAgentUIHandler::HandlePressKeyChord,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "typeText",
      base::BindRepeating(&DaoAgentUIHandler::HandleTypeText,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "enableNetworkTracking",
      base::BindRepeating(&DaoAgentUIHandler::HandleEnableNetworkTracking,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getNetworkRequests",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetNetworkRequests,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearNetworkRequests",
      base::BindRepeating(&DaoAgentUIHandler::HandleClearNetworkRequests,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "enableConsoleTracking",
      base::BindRepeating(&DaoAgentUIHandler::HandleEnableConsoleTracking,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getConsoleMessages",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetConsoleMessages,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "clearConsoleMessages",
      base::BindRepeating(&DaoAgentUIHandler::HandleClearConsoleMessages,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "closeSidebar",
      base::BindRepeating(&DaoAgentUIHandler::HandleCloseSidebar,
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

  // Domain security check.
  if (!expected_domain_.empty()) {
    std::string current_domain = contents->GetURL().host();
    if (current_domain != expected_domain_) {
      base::Value::Dict response;
      response.Set("error",
                   "Security: domain changed from " + expected_domain_ +
                   " to " + current_domain);
      ResolveJavascriptCallback(base::Value(callback_id), response);
      return;
    }
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

  DaoAgentLockTabHelper::LockContents(contents);
  base::Value::Dict params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id,
             content::WebContents* locked_contents,
             base::Value result) {
            UnlockLockedTab(locked_contents);
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
          weak_factory_.GetWeakPtr(), callback_id, contents));
}

void DaoAgentUIHandler::HandleExecuteScript(const base::Value::List& args) {
  AllowJavascript();

  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  std::string code;
  bool lock_tab = false;
  if (args[1].is_dict()) {
    auto* c = args[1].GetDict().FindString("code");
    if (c) {
      code = *c;
    }
    lock_tab = args[1].GetDict().FindBool("lockTab").value_or(false);
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || code.empty()) {
    base::Value::Dict response;
    response.Set("error", "No active tab or empty code");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Domain security check for locked (page-manipulating) scripts.
  if (lock_tab && !expected_domain_.empty()) {
    std::string current_domain = contents->GetURL().host();
    if (current_domain != expected_domain_) {
      base::Value::Dict response;
      response.Set("error",
                   "Security: domain changed from " + expected_domain_ +
                   " to " + current_domain);
      ResolveJavascriptCallback(base::Value(callback_id), response);
      return;
    }
  }

  if (lock_tab) {
    DaoAgentLockTabHelper::LockContents(contents);
  }
  base::Value::Dict params;
  params.Set("expression", code);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id,
             content::WebContents* locked_contents,
             base::Value result) {
            UnlockLockedTab(locked_contents);
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
          weak_factory_.GetWeakPtr(), callback_id,
          lock_tab ? contents : nullptr));
}

void DaoAgentUIHandler::HandleHighlightElement(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string selector;
  if (args[1].is_dict()) {
    auto* sel = args[1].GetDict().FindString("selector");
    if (sel) selector = *sel;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || selector.empty()) {
    base::Value::Dict r;
    r.Set("error", "No active tab or invalid selector");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  std::string escaped;
  for (char c : selector) {
    if (c == '\'') escaped += "\\'";
    else if (c == '\\') escaped += "\\\\";
    else escaped += c;
  }

  std::string inject_then_show =
      std::string(kHighlightInjectScript) +
      "; window.__dao_agent__.showHighlight('" + escaped + "')";

  base::Value::Dict params;
  params.Set("expression", inject_then_show);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::Value::Dict response;
            response.Set("success", true);
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleClearHighlight(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
    return;
  }

  base::Value::Dict params;
  params.Set("expression",
      "window.__dao_agent__ && window.__dao_agent__.clearHighlight()");
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value) {
            if (!handler) return;
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), base::Value(true));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleMoveCursor(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  double vx = 0, vy = 0;
  if (args[1].is_dict()) {
    vx = args[1].GetDict().FindDouble("x").value_or(0);
    vy = args[1].GetDict().FindDouble("y").value_or(0);
  }

  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  if (!browser) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value("No browser"));
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  auto* cursor_view = browser_view ? browser_view->dao_agent_cursor() : nullptr;
  if (!cursor_view) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value("No cursor view"));
    return;
  }

  // Convert viewport coords to cursor view local coords.
  // Viewport coords (from getBoundingClientRect) are relative to the web
  // content render area, so use the RenderWidgetHostView bounds.
  content::WebContents* wc =
      browser->tab_strip_model()->GetActiveWebContents();
  gfx::Rect viewport_screen;
  if (wc && wc->GetRenderWidgetHostView()) {
    viewport_screen = wc->GetRenderWidgetHostView()->GetViewBounds();
  } else {
    viewport_screen = browser_view->contents_container()->GetBoundsInScreen();
  }
  gfx::Rect cursor_bounds = cursor_view->GetBoundsInScreen();
  float view_x = static_cast<float>(viewport_screen.x() - cursor_bounds.x()) + vx;
  float view_y = static_cast<float>(viewport_screen.y() - cursor_bounds.y()) + vy;

  cursor_view->AnimateTo(view_x, view_y,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id) {
            if (!handler) return;
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), base::Value(true));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleAgentClick(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string selector;
  std::string description;
  if (args[1].is_dict()) {
    auto* sel = args[1].GetDict().FindString("selector");
    if (sel) selector = *sel;
    auto* desc = args[1].GetDict().FindString("description");
    if (desc) description = *desc;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || selector.empty()) {
    base::Value::Dict r;
    r.Set("error", "No active tab or invalid selector");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  // Domain security check.
  if (!expected_domain_.empty()) {
    std::string current_domain = contents->GetURL().host();
    if (current_domain != expected_domain_) {
      base::Value::Dict r;
      r.Set("error",
            "Security: domain changed from " + expected_domain_ +
            " to " + current_domain);
      ResolveJavascriptCallback(base::Value(callback_id), r);
      return;
    }
  }

  std::string escaped;
  for (char c : selector) {
    if (c == '\'') escaped += "\\'";
    else if (c == '\\') escaped += "\\\\";
    else escaped += c;
  }

  DaoAgentLockTabHelper::LockContents(contents);

  // Inject highlight + get element center coordinates.
  std::string script =
      std::string(kHighlightInjectScript) +
      "; (function() {"
      "  window.__dao_agent__.showHighlight('" + escaped + "');"
      "  var el = document.querySelector('" + escaped + "');"
      "  if (!el) return JSON.stringify({error:'element not found'});"
      "  var r = el.getBoundingClientRect();"
      "  return JSON.stringify({x: r.left + r.width/2, y: r.top + r.height/2});"
      "})()";

  base::Value::Dict params;
  params.Set("expression", script);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id,
             std::string escaped_selector,
             content::WebContents* locked_contents,
             base::Value result) {
            if (!handler) {
              UnlockLockedTab(locked_contents);
              return;
            }

            std::string json_str;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string())
                json_str = value->GetString();
            }

            auto parsed = base::JSONReader::Read(json_str);
            if (!parsed || !parsed->is_dict() ||
                parsed->GetDict().FindString("error")) {
              UnlockLockedTab(locked_contents);
              base::Value::Dict r;
              r.Set("error", json_str.empty() ? "element not found" : json_str);
              handler->ResolveJavascriptCallback(
                  base::Value(callback_id), r);
              return;
            }

            double vx = parsed->GetDict().FindDouble("x").value_or(0);
            double vy = parsed->GetDict().FindDouble("y").value_or(0);

            Browser* browser = BrowserList::GetInstance()->GetLastActive();
            BrowserView* bv = browser ?
                BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
            auto* cursor = bv ? bv->dao_agent_cursor() : nullptr;

            if (!cursor) {
              handler->PerformCDPClick(callback_id, escaped_selector,
                                       vx, vy, locked_contents);
              return;
            }

            // Use RenderWidgetHostView bounds for accurate viewport→screen
            // mapping (getBoundingClientRect coords are relative to this).
            gfx::Rect viewport_screen;
            if (locked_contents &&
                locked_contents->GetRenderWidgetHostView()) {
              viewport_screen =
                  locked_contents->GetRenderWidgetHostView()->GetViewBounds();
            } else {
              viewport_screen =
                  bv->contents_container()->GetBoundsInScreen();
            }
            gfx::Rect cursor_bounds = cursor->GetBoundsInScreen();
            float cx = static_cast<float>(
                viewport_screen.x() - cursor_bounds.x()) + vx;
            float cy = static_cast<float>(
                viewport_screen.y() - cursor_bounds.y()) + vy;

            if (!cursor->is_visible()) {
              cursor->ShowAtCenter();
            }

            cursor->AnimateTo(cx, cy,
                base::BindOnce(
                    [](base::WeakPtr<DaoAgentUIHandler> h,
                       std::string cb_id,
                       std::string esc_sel,
                       double vx, double vy,
                       content::WebContents* locked) {
                      if (!h) {
                        UnlockLockedTab(locked);
                        return;
                      }
                      Browser* br =
                          BrowserList::GetInstance()->GetLastActive();
                      BrowserView* view = br ?
                          BrowserView::GetBrowserViewForBrowser(br) : nullptr;
                      auto* cur = view ?
                          view->dao_agent_cursor() : nullptr;
                      if (cur) {
                        cur->PlayClickRipple();
                      }
                      h->PerformCDPClick(cb_id, esc_sel, vx, vy, locked);
                    },
                    handler, callback_id, escaped_selector,
                    vx, vy, locked_contents));
          },
          weak_factory_.GetWeakPtr(), callback_id, escaped,
          contents));
}

void DaoAgentUIHandler::PerformCDPClick(
    const std::string& callback_id,
    const std::string& escaped_selector,
    double viewport_x,
    double viewport_y,
    content::WebContents* locked_contents) {
  // CDP Input.dispatchMouseEvent needs BOTH `button` (the button that fired
  // this event) and `buttons` (bitmask of currently-pressed buttons). Many
  // page event paths — React's synthetic event system, any handler that
  // reads MouseEvent.buttons, and Blink's own click synthesis in strict
  // modes — silently drop events where `buttons` is 0 on a mousePressed.
  // We also dispatch a `mouseMoved` at the same coords first so hover-
  // gated UI (dropdown menus, :hover reveal buttons) has a chance to
  // settle before the press lands.
  base::Value::Dict move_params;
  move_params.Set("type", "mouseMoved");
  move_params.Set("x", static_cast<int>(viewport_x));
  move_params.Set("y", static_cast<int>(viewport_y));
  move_params.Set("button", "none");
  move_params.Set("buttons", 0);

  base::Value::Dict press_params;
  press_params.Set("type", "mousePressed");
  press_params.Set("x", static_cast<int>(viewport_x));
  press_params.Set("y", static_cast<int>(viewport_y));
  press_params.Set("button", "left");
  press_params.Set("buttons", 1);
  press_params.Set("clickCount", 1);

  devtools_client_->SendCommand(
      "Input.dispatchMouseEvent", std::move(move_params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id,
             std::string escaped_selector,
             double vx, double vy,
             content::WebContents* locked,
             base::Value::Dict press_params,
             base::Value) {
            if (!handler) {
              UnlockLockedTab(locked);
              return;
            }
            handler->DispatchPressAndRelease(
                callback_id, escaped_selector, vx, vy, locked,
                std::move(press_params));
          },
          weak_factory_.GetWeakPtr(), callback_id, escaped_selector,
          viewport_x, viewport_y, locked_contents,
          std::move(press_params)));
}

void DaoAgentUIHandler::DispatchPressAndRelease(
    const std::string& callback_id,
    const std::string& escaped_selector,
    double viewport_x,
    double viewport_y,
    content::WebContents* locked_contents,
    base::Value::Dict press_params) {
  devtools_client_->SendCommand(
      "Input.dispatchMouseEvent", std::move(press_params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id,
             double vx, double vy,
             content::WebContents* locked,
             base::Value) {
            if (!handler) {
              UnlockLockedTab(locked);
              return;
            }
            base::Value::Dict release_params;
            release_params.Set("type", "mouseReleased");
            release_params.Set("x", static_cast<int>(vx));
            release_params.Set("y", static_cast<int>(vy));
            release_params.Set("button", "left");
            release_params.Set("buttons", 0);
            release_params.Set("clickCount", 1);

            handler->devtools_client_->SendCommand(
                "Input.dispatchMouseEvent", std::move(release_params),
                base::BindOnce(
                    [](base::WeakPtr<DaoAgentUIHandler> h,
                       std::string cb_id,
                       content::WebContents* lk,
                       base::Value) {
                      if (h) {
                        base::Value::Dict clear_params;
                        clear_params.Set("expression",
                            "window.__dao_agent__ && "
                            "window.__dao_agent__.clearHighlight()");
                        clear_params.Set("returnByValue", true);
                        h->devtools_client_->SendCommand(
                            "Runtime.evaluate",
                            std::move(clear_params),
                            base::BindOnce(
                                [](base::WeakPtr<DaoAgentUIHandler> h2,
                                   std::string cb,
                                   content::WebContents* lk2,
                                   base::Value) {
                                  UnlockLockedTab(lk2);
                                  if (!h2) return;
                                  base::Value::Dict r;
                                  r.Set("success", true);
                                  h2->ResolveJavascriptCallback(
                                      base::Value(cb), std::move(r));
                                },
                                h, cb_id, lk));
                      } else {
                        UnlockLockedTab(lk);
                      }
                    },
                    handler, callback_id, locked));
          },
          weak_factory_.GetWeakPtr(), callback_id,
          viewport_x, viewport_y, locked_contents));
}

void DaoAgentUIHandler::HandleGetAccessibilityTree(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string filter = "interactive";
  if (args[1].is_dict()) {
    auto* f = args[1].GetDict().FindString("filter");
    if (f) filter = *f;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::Value::Dict response;
    response.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Escape filter for safe inclusion in the IIFE call.
  std::string js = std::string(kAccessibilityTreeScript) +
                   "('" + filter + "')";

  base::Value::Dict params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                auto parsed = base::JSONReader::Read(value->GetString());
                if (parsed && parsed->is_dict()) {
                  response = std::move(parsed->GetDict());
                } else {
                  response.Set("tree", value->GetString());
                }
              }
              auto* exception =
                  result.GetDict().FindByDottedPath("exceptionDetails");
              if (exception) {
                std::string json;
                base::JSONWriter::Write(*exception, &json);
                response.Set("error", json);
              }
            }
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleClickByRef(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string ref_id;
  if (args[1].is_dict()) {
    auto* r = args[1].GetDict().FindString("ref_id");
    if (r) ref_id = *r;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || ref_id.empty()) {
    base::Value::Dict response;
    response.Set("error", "No active tab or invalid ref_id");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Domain security check.
  if (!expected_domain_.empty()) {
    std::string current_domain = contents->GetURL().host();
    if (current_domain != expected_domain_) {
      base::Value::Dict response;
      response.Set("error",
                   "Security: domain changed from " + expected_domain_ +
                   " to " + current_domain);
      ResolveJavascriptCallback(base::Value(callback_id), response);
      return;
    }
  }

  // Escape ref_id for safe JS injection.
  std::string escaped_ref;
  for (char c : ref_id) {
    if (c == '"') escaped_ref += "\\\"";
    else if (c == '\\') escaped_ref += "\\\\";
    else escaped_ref += c;
  }

  DaoAgentLockTabHelper::LockContents(contents);

  // Inject highlight + get element bounds via ref_id.
  std::string script =
      std::string(kHighlightInjectScript) +
      "; (function() {"
      "  var el = document.querySelector('[data-dao-ref=\"" +
      escaped_ref + "\"]');"
      "  if (!el) return JSON.stringify({error:'Element not found'});"
      "  window.__dao_agent__.showHighlight('[data-dao-ref=\"" +
      escaped_ref + "\"]');"
      "  var r = el.getBoundingClientRect();"
      "  return JSON.stringify({x: r.left + r.width/2, y: r.top + r.height/2});"
      "})()";

  base::Value::Dict params;
  params.Set("expression", script);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id,
             std::string escaped_ref,
             content::WebContents* locked_contents,
             base::Value result) {
            if (!handler) {
              UnlockLockedTab(locked_contents);
              return;
            }

            std::string json_str;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string())
                json_str = value->GetString();
            }

            auto parsed = base::JSONReader::Read(json_str);
            if (!parsed || !parsed->is_dict() ||
                parsed->GetDict().FindString("error")) {
              UnlockLockedTab(locked_contents);
              base::Value::Dict r;
              r.Set("error",
                    json_str.empty() ? "element not found" : json_str);
              handler->ResolveJavascriptCallback(
                  base::Value(callback_id), r);
              return;
            }

            double vx = parsed->GetDict().FindDouble("x").value_or(0);
            double vy = parsed->GetDict().FindDouble("y").value_or(0);

            // Move cursor then click, same as HandleAgentClick.
            Browser* browser =
                BrowserList::GetInstance()->GetLastActive();
            BrowserView* bv = browser
                ? BrowserView::GetBrowserViewForBrowser(browser)
                : nullptr;
            auto* cursor = bv ? bv->dao_agent_cursor() : nullptr;

            std::string selector_for_click =
                "[data-dao-ref=\"" + escaped_ref + "\"]";

            if (!cursor) {
              handler->PerformCDPClick(callback_id, selector_for_click,
                                       vx, vy, locked_contents);
              return;
            }

            gfx::Rect viewport_screen;
            if (locked_contents &&
                locked_contents->GetRenderWidgetHostView()) {
              viewport_screen =
                  locked_contents->GetRenderWidgetHostView()
                      ->GetViewBounds();
            } else {
              viewport_screen =
                  bv->contents_container()->GetBoundsInScreen();
            }
            gfx::Rect cursor_bounds = cursor->GetBoundsInScreen();
            float cx = static_cast<float>(
                viewport_screen.x() - cursor_bounds.x()) + vx;
            float cy = static_cast<float>(
                viewport_screen.y() - cursor_bounds.y()) + vy;

            if (!cursor->is_visible()) {
              cursor->ShowAtCenter();
            }

            cursor->AnimateTo(cx, cy,
                base::BindOnce(
                    [](base::WeakPtr<DaoAgentUIHandler> h,
                       std::string cb_id,
                       std::string sel,
                       double vx, double vy,
                       content::WebContents* locked) {
                      if (!h) {
                        UnlockLockedTab(locked);
                        return;
                      }
                      Browser* br =
                          BrowserList::GetInstance()->GetLastActive();
                      BrowserView* view = br
                          ? BrowserView::GetBrowserViewForBrowser(br)
                          : nullptr;
                      auto* cur = view
                          ? view->dao_agent_cursor() : nullptr;
                      if (cur) {
                        cur->PlayClickRipple();
                      }
                      h->PerformCDPClick(cb_id, sel, vx, vy, locked);
                    },
                    handler, callback_id, selector_for_click,
                    vx, vy, locked_contents));
          },
          weak_factory_.GetWeakPtr(), callback_id, escaped_ref,
          contents));
}

void DaoAgentUIHandler::HandleCaptureScreenshot(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::Value::Dict response;
    response.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  base::Value::Dict params;
  params.Set("format", "jpeg");
  params.Set("quality", 60);

  devtools_client_->SendCommand(
      "Page.captureScreenshot", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* data = result.GetDict().FindString("data");
              if (data) {
                response.Set("data", *data);
                response.Set("format", "jpeg");
              } else {
                response.Set("error", "No screenshot data returned");
              }
            } else {
              response.Set("error", "Screenshot failed");
            }
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleScrollPage(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string direction = "down";
  int pixel_amount = 0;  // 0 means use viewport-based default.
  if (args[1].is_dict()) {
    auto* d = args[1].GetDict().FindString("direction");
    if (d) direction = *d;
    pixel_amount = static_cast<int>(
        args[1].GetDict().FindDouble("amount").value_or(0));
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::Value::Dict response;
    response.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // If pixel_amount is specified, use it directly; otherwise default to 80%
  // of viewport height.
  std::string amount_expr;
  if (pixel_amount > 0) {
    std::string sign = (direction == "up") ? "-" : "";
    amount_expr = sign + std::to_string(pixel_amount);
  } else {
    std::string sign = (direction == "up") ? "-" : "";
    amount_expr = sign + "Math.round(window.innerHeight * 0.8)";
  }
  std::string js =
      "(function() {"
      "  var amount = " + amount_expr + ";"
      "  window.scrollBy({top: amount, behavior: 'smooth'});"
      "  return JSON.stringify({"
      "    scrollY: Math.round(window.scrollY + amount),"
      "    scrollHeight: document.documentElement.scrollHeight,"
      "    viewportHeight: window.innerHeight"
      "  });"
      "})()";

  base::Value::Dict params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                auto parsed = base::JSONReader::Read(value->GetString());
                if (parsed && parsed->is_dict()) {
                  response = std::move(parsed->GetDict());
                }
              }
            }
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleScrollToElement(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string selector;
  std::string ref_id;
  if (args[1].is_dict()) {
    auto* s = args[1].GetDict().FindString("selector");
    if (s) selector = *s;
    auto* r = args[1].GetDict().FindString("ref_id");
    if (r) ref_id = *r;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || (selector.empty() && ref_id.empty())) {
    base::Value::Dict response;
    response.Set("error", "No active tab or no selector/ref_id");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Build the JS query — prefer ref_id, fallback to selector.
  std::string query;
  if (!ref_id.empty()) {
    std::string escaped;
    for (char c : ref_id) {
      if (c == '"') escaped += "\\\"";
      else if (c == '\\') escaped += "\\\\";
      else escaped += c;
    }
    query = "[data-dao-ref=\\\"" + escaped + "\\\"]";
  } else {
    std::string escaped;
    for (char c : selector) {
      if (c == '\'') escaped += "\\'";
      else if (c == '\\') escaped += "\\\\";
      else escaped += c;
    }
    query = escaped;
  }

  std::string js =
      "(function() {"
      "  var el = document.querySelector('" + query + "');"
      "  if (!el) return JSON.stringify({error: 'element not found'});"
      "  el.scrollIntoView({behavior: 'smooth', block: 'center'});"
      "  return JSON.stringify({scrolled: true});"
      "})()";

  base::Value::Dict params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                auto parsed = base::JSONReader::Read(value->GetString());
                if (parsed && parsed->is_dict()) {
                  response = std::move(parsed->GetDict());
                }
              }
            }
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleSetExpectedDomain(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  if (args[1].is_dict()) {
    auto* domain = args[1].GetDict().FindString("domain");
    if (domain) {
      expected_domain_ = *domain;
    }
  }

  base::Value::Dict response;
  response.Set("success", true);
  response.Set("domain", expected_domain_);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

// ---- Tab Management ----

void DaoAgentUIHandler::HandleListTabs(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  base::Value::List tabs_list;

  if (browser) {
    TabStripModel* model = browser->tab_strip_model();
    int active_index = model->active_index();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (!wc) continue;
      base::Value::Dict tab;
      tab.Set("index", i);
      tab.Set("url", wc->GetURL().spec());
      tab.Set("title", base::UTF16ToUTF8(wc->GetTitle()));
      tab.Set("active", i == active_index);
      tabs_list.Append(std::move(tab));
    }
  }

  base::Value::Dict response;
  response.Set("tabs", std::move(tabs_list));
  response.Set("count", static_cast<int>(tabs_list.size()));
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleSwitchTab(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  int index = -1;
  if (args[1].is_dict()) {
    index = args[1].GetDict().FindInt("index").value_or(-1);
  }

  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  base::Value::Dict response;

  if (!browser || index < 0 ||
      index >= browser->tab_strip_model()->count()) {
    response.Set("error", "Invalid tab index");
  } else {
    browser->tab_strip_model()->ActivateTabAt(index);
    // Re-attach devtools to the new active tab.
    content::WebContents* wc =
        browser->tab_strip_model()->GetWebContentsAt(index);
    response.Set("success", true);
    response.Set("url", wc ? wc->GetURL().spec() : "");
    response.Set("title",
                 wc ? base::UTF16ToUTF8(wc->GetTitle()) : "");
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleOpenTab(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string url_str;
  if (args[1].is_dict()) {
    auto* u = args[1].GetDict().FindString("url");
    if (u) url_str = *u;
  }

  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  base::Value::Dict response;

  if (!browser) {
    response.Set("error", "No active browser");
  } else {
    GURL url(url_str);
    if (!url.is_valid()) {
      url = GURL("about:blank");
    }
    NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    response.Set("success", true);
    response.Set("url", url.spec());
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleCloseTab(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  int index = -1;
  if (args[1].is_dict()) {
    index = args[1].GetDict().FindInt("index").value_or(-1);
  }

  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  base::Value::Dict response;

  if (!browser) {
    response.Set("error", "No active browser");
  } else {
    TabStripModel* model = browser->tab_strip_model();
    // Default to active tab.
    if (index < 0) {
      index = model->active_index();
    }
    if (index < 0 || index >= model->count()) {
      response.Set("error", "Invalid tab index");
    } else if (model->count() <= 1) {
      response.Set("error", "Cannot close the last tab");
    } else {
      model->CloseWebContentsAt(index,
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      response.Set("success", true);
    }
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

// ---- Keyboard Input ----

void DaoAgentUIHandler::HandlePressKeyChord(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string key_combo;
  if (args[1].is_dict()) {
    auto* k = args[1].GetDict().FindString("keys");
    if (k) key_combo = *k;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || key_combo.empty()) {
    base::Value::Dict r;
    r.Set("error", "No active tab or empty key combo");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  // Build JS to simulate key events via CDP-compatible dispatchKeyEvent.
  // Parse key combo: "ctrl+a", "cmd+c", "Enter", "Tab", "Escape", etc.
  // Use Input.dispatchKeyEvent CDP command for each key.
  std::string js =
      "(function() {"
      "  var combo = '" + key_combo + "'.toLowerCase();"
      "  var parts = combo.split('+');"
      "  var key = parts[parts.length - 1].trim();"
      "  var opts = {"
      "    bubbles: true, cancelable: true,"
      "    ctrlKey: combo.includes('ctrl'),"
      "    metaKey: combo.includes('cmd') || combo.includes('meta'),"
      "    shiftKey: combo.includes('shift'),"
      "    altKey: combo.includes('alt')"
      "  };"
      "  var keyMap = {"
      "    'enter':'Enter', 'tab':'Tab', 'escape':'Escape', 'esc':'Escape',"
      "    'backspace':'Backspace', 'delete':'Delete', 'space':' ',"
      "    'up':'ArrowUp', 'down':'ArrowDown', 'left':'ArrowLeft', 'right':'ArrowRight'"
      "  };"
      "  opts.key = keyMap[key] || key;"
      "  var el = document.activeElement || document.body;"
      "  el.dispatchEvent(new KeyboardEvent('keydown', opts));"
      "  el.dispatchEvent(new KeyboardEvent('keyup', opts));"
      "  if (opts.key.length === 1 && !opts.ctrlKey && !opts.metaKey) {"
      "    el.dispatchEvent(new InputEvent('input', {data: opts.key, inputType:'insertText', bubbles:true}));"
      "  }"
      "  return 'pressed: ' + combo;"
      "})()";

  base::Value::Dict params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::Value::Dict response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                response.Set("result", value->GetString());
              }
            }
            response.Set("success", true);
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleTypeText(const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string text;
  bool clear_first = false;
  if (args[1].is_dict()) {
    auto* t = args[1].GetDict().FindString("text");
    if (t) text = *t;
    clear_first = args[1].GetDict().FindBool("clear").value_or(false);
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || text.empty()) {
    base::Value::Dict r;
    r.Set("error", "No active tab or empty text");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  // Escape the text for safe JS string inclusion.
  std::string escaped_text;
  for (char c : text) {
    if (c == '\\') escaped_text += "\\\\";
    else if (c == '\'') escaped_text += "\\'";
    else if (c == '\n') escaped_text += "\\n";
    else if (c == '\r') escaped_text += "\\r";
    else if (c == '\t') escaped_text += "\\t";
    else escaped_text += c;
  }

  // Use CDP Input.insertText for reliable text input that works with
  // all input types including contentEditable and shadow DOM inputs.
  // First optionally clear, then insert text.
  if (clear_first) {
    // Select all then delete before typing.
    std::string clear_js =
        "(function() {"
        "  var el = document.activeElement;"
        "  if (el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA')) {"
        "    el.select();"
        "  } else {"
        "    document.execCommand('selectAll');"
        "  }"
        "  return 'selected';"
        "})()";
    base::Value::Dict clear_params;
    clear_params.Set("expression", clear_js);
    clear_params.Set("returnByValue", true);

    devtools_client_->SendCommand(
        "Runtime.evaluate", std::move(clear_params),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentUIHandler> handler,
               std::string callback_id,
               std::string escaped_text,
               base::Value) {
              if (!handler) return;
              // Now insert the text via CDP.
              base::Value::Dict insert_params;
              insert_params.Set("text", escaped_text);
              handler->devtools_client_->SendCommand(
                  "Input.insertText", std::move(insert_params),
                  base::BindOnce(
                      [](base::WeakPtr<DaoAgentUIHandler> h,
                         std::string cb_id,
                         std::string text,
                         base::Value) {
                        if (!h) return;
                        base::Value::Dict r;
                        r.Set("success", true);
                        r.Set("typed", text);
                        h->ResolveJavascriptCallback(
                            base::Value(cb_id), r);
                      },
                      handler, callback_id, escaped_text));
            },
            weak_factory_.GetWeakPtr(), callback_id, text));
  } else {
    // Direct insert without clearing.
    base::Value::Dict insert_params;
    insert_params.Set("text", text);
    devtools_client_->SendCommand(
        "Input.insertText", std::move(insert_params),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentUIHandler> handler,
               std::string callback_id,
               std::string text,
               base::Value) {
              if (!handler) return;
              base::Value::Dict r;
              r.Set("success", true);
              r.Set("typed", text);
              handler->ResolveJavascriptCallback(
                  base::Value(callback_id), r);
            },
            weak_factory_.GetWeakPtr(), callback_id, text));
  }
}

// ---- Network/Console Debugging ----

void DaoAgentUIHandler::OnCDPEvent(const std::string& method,
                                    const base::Value::Dict& params) {
  if (network_tracking_enabled_) {
    if (method == "Network.requestWillBeSent") {
      base::Value::Dict entry;
      auto* request = params.FindDict("request");
      if (request) {
        auto* url = request->FindString("url");
        auto* req_method = request->FindString("method");
        if (url) entry.Set("url", *url);
        if (req_method) entry.Set("method", *req_method);
      }
      auto* type = params.FindString("type");
      if (type) entry.Set("type", *type);
      entry.Set("timestamp", params.FindDouble("timestamp").value_or(0));
      entry.Set("phase", "request");
      // Cap stored requests at 200.
      if (network_requests_.size() < 200) {
        network_requests_.push_back(std::move(entry));
      }
    } else if (method == "Network.responseReceived") {
      base::Value::Dict entry;
      auto* response = params.FindDict("response");
      if (response) {
        auto* url = response->FindString("url");
        auto status = response->FindInt("status");
        auto* mime = response->FindString("mimeType");
        if (url) entry.Set("url", *url);
        if (status) entry.Set("status", *status);
        if (mime) entry.Set("mimeType", *mime);
      }
      auto* type = params.FindString("type");
      if (type) entry.Set("type", *type);
      entry.Set("phase", "response");
      if (network_requests_.size() < 200) {
        network_requests_.push_back(std::move(entry));
      }
    }
  }

  if (console_tracking_enabled_) {
    if (method == "Runtime.consoleAPICalled") {
      base::Value::Dict entry;
      auto* type = params.FindString("type");
      if (type) entry.Set("type", *type);
      auto* msg_args = params.FindList("args");
      if (msg_args && !msg_args->empty()) {
        std::string text;
        for (const auto& arg : *msg_args) {
          if (arg.is_dict()) {
            auto* val = arg.GetDict().FindString("value");
            auto* desc = arg.GetDict().FindString("description");
            if (val) {
              if (!text.empty()) text += " ";
              text += *val;
            } else if (desc) {
              if (!text.empty()) text += " ";
              text += *desc;
            }
          }
        }
        entry.Set("text", text);
      }
      entry.Set("timestamp", params.FindDouble("timestamp").value_or(0));
      if (console_messages_.size() < 500) {
        console_messages_.push_back(std::move(entry));
      }
    } else if (method == "Runtime.exceptionThrown") {
      base::Value::Dict entry;
      entry.Set("type", "error");
      auto* exception_details = params.FindDict("exceptionDetails");
      if (exception_details) {
        auto* text = exception_details->FindString("text");
        if (text) entry.Set("text", *text);
        auto line = exception_details->FindInt("lineNumber");
        if (line) entry.Set("line", *line);
        auto* url = exception_details->FindString("url");
        if (url) entry.Set("url", *url);
      }
      entry.Set("timestamp", params.FindDouble("timestamp").value_or(0));
      if (console_messages_.size() < 500) {
        console_messages_.push_back(std::move(entry));
      }
    }
  }
}

void DaoAgentUIHandler::HandleEnableNetworkTracking(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::Value::Dict r;
    r.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  network_tracking_enabled_ = true;
  network_requests_.clear();

  base::Value::Dict params;
  devtools_client_->SendCommand(
      "Network.enable", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value) {
            if (!handler) return;
            base::Value::Dict r;
            r.Set("success", true);
            r.Set("message", "Network tracking enabled");
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), r);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleGetNetworkRequests(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  base::Value::List list;
  for (const auto& entry : network_requests_) {
    list.Append(entry.Clone());
  }

  base::Value::Dict response;
  response.Set("requests", std::move(list));
  response.Set("count", static_cast<int>(network_requests_.size()));
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleClearNetworkRequests(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  network_requests_.clear();

  base::Value::Dict response;
  response.Set("success", true);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleEnableConsoleTracking(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::Value::Dict r;
    r.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  console_tracking_enabled_ = true;
  console_messages_.clear();

  base::Value::Dict params;
  devtools_client_->SendCommand(
      "Runtime.enable", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value) {
            if (!handler) return;
            base::Value::Dict r;
            r.Set("success", true);
            r.Set("message", "Console tracking enabled");
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), r);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleGetConsoleMessages(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string filter;
  if (args.size() >= 2 && args[1].is_dict()) {
    auto* f = args[1].GetDict().FindString("filter");
    if (f) filter = *f;
  }

  base::Value::List list;
  for (const auto& entry : console_messages_) {
    if (!filter.empty()) {
      auto* type = entry.FindString("type");
      if (type && *type != filter) continue;
    }
    list.Append(entry.Clone());
  }

  base::Value::Dict response;
  int count = static_cast<int>(list.size());
  response.Set("messages", std::move(list));
  response.Set("count", count);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleClearConsoleMessages(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  console_messages_.clear();

  base::Value::Dict response;
  response.Set("success", true);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleCloseSidebar(
    const base::Value::List& args) {
  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  if (!browser) {
    return;
  }
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->dao_agent_sidebar()) {
    return;
  }
  if (browser_view->dao_agent_sidebar()->is_expanded()) {
    browser_view->dao_agent_sidebar()->Toggle();
    browser_view->InvalidateLayout();
    // Sync the address bar chat button highlight.
    if (browser_view->dao_address_bar()) {
      browser_view->dao_address_bar()->SetChatButtonHighlighted(false);
    }
  }
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
  web_ui()->RegisterMessageCallback(
      "setProactiveEnabled",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleSetProactiveEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setConfidenceThreshold",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleSetConfidenceThreshold,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "recordActionFeedback",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleRecordActionFeedback,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "saveEpisode",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleSaveEpisode,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "saveSummary",
      base::BindRepeating(&DaoAgentMemoryHandler::HandleSaveSummary,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPageContentForScenario",
      base::BindRepeating(
          &DaoAgentMemoryHandler::HandleGetPageContentForScenario,
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

  // Scenario-based fields.
  dict.Set("actionType", static_cast<int>(suggestion.action_type));
  dict.Set("scenarioId", suggestion.scenario_id);
  dict.Set("scenarioName", suggestion.scenario_name);
  dict.Set("actionLabel", suggestion.action_label);
  dict.Set("actionPrompt", suggestion.action_prompt);
  dict.Set("requiresPageContent", suggestion.requires_page_content);
  dict.Set("tabId", suggestion.tab_id);

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

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  // If the arg is a dict, it's a structured scenario dismissal.
  if (args[1].is_dict()) {
    auto feedback = ParseActionFeedbackFromDict(args[1].GetDict());
    feedback.outcome = "dismissed";

    // Also bump scenario dismiss stats.
    if (!feedback.scenario_id.empty() && proactive_engine_) {
      service->UpdateScenarioStats(
          feedback.scenario_id, "times_dismissed",
          base::DoNothing());
    }

    service->RecordActionFeedback(
        std::move(feedback),
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
    return;
  }

  // Legacy: episode-based dismiss (lower confidence).
  int64_t episode_id = args[1].is_int() ? args[1].GetInt() : 0;
  if (episode_id == 0) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }
  service->UpdateEpisodeConfidence(
      episode_id, -1.0,
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

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  // If the arg is a dict, it's a structured scenario acceptance.
  if (args[1].is_dict()) {
    auto feedback = ParseActionFeedbackFromDict(args[1].GetDict());
    feedback.outcome = "clicked";

    // Bump scenario accepted stats.
    if (!feedback.scenario_id.empty() && proactive_engine_) {
      service->UpdateScenarioStats(
          feedback.scenario_id, "times_accepted",
          base::DoNothing());
    }

    service->RecordActionFeedback(
        std::move(feedback),
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
    return;
  }

  // Legacy: episode-based accept (boost confidence).
  int64_t episode_id = args[1].is_int() ? args[1].GetInt() : 0;
  if (episode_id == 0) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }
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

void DaoAgentMemoryHandler::HandleSetProactiveEnabled(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  bool enabled = args[1].GetBool();

  if (proactive_engine_) {
    if (enabled) {
      proactive_engine_->Start();
    } else {
      proactive_engine_->Stop();
    }
  }

  ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
}

void DaoAgentMemoryHandler::HandleSetConfidenceThreshold(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  double threshold = args[1].is_double()
                         ? args[1].GetDouble()
                         : (args[1].is_int() ? static_cast<double>(args[1].GetInt())
                                             : 0.7);

  if (proactive_engine_) {
    proactive_engine_->SetConfidenceThreshold(threshold);
  }

  ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
}

void DaoAgentMemoryHandler::HandleRecordActionFeedback(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const auto& d = args[1].GetDict();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  auto feedback = ParseActionFeedbackFromDict(d);

  service->RecordActionFeedback(
      std::move(feedback),
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

void DaoAgentMemoryHandler::HandleSaveEpisode(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const auto& d = args[1].GetDict();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  Episode episode;
  if (auto* v = d.FindString("domain")) episode.domain = *v;
  if (auto* v = d.FindString("pathTemplate")) episode.path_template = *v;
  if (auto* v = d.FindString("url")) episode.url = *v;
  if (auto* v = d.FindString("title")) episode.title = *v;
  if (auto* v = d.FindString("intent")) episode.intent = *v;
  if (auto* v = d.FindString("entities")) episode.entities = *v;
  if (auto* v = d.FindString("toolsUsed")) episode.tools_used = *v;
  if (auto* v = d.FindString("outcome")) episode.outcome = *v;
  episode.confidence = d.FindDouble("confidence").value_or(0.7);
  if (auto* v = d.FindString("userAction")) episode.user_action = *v;
  if (auto* v = d.FindString("actionResult")) episode.action_result = *v;
  episode.timestamp = base::Time::Now();

  service->SaveEpisode(
      std::move(episode),
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

void DaoAgentMemoryHandler::HandleSaveSummary(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const auto& d = args[1].GetDict();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  ConversationSummary summary;
  if (auto* v = d.FindString("sessionId")) summary.session_id = *v;
  if (auto* v = d.FindString("summary")) summary.summary = *v;
  summary.message_count = d.FindInt("messageCount").value_or(0);
  if (auto* v = d.FindString("primaryDomain")) summary.primary_domain = *v;
  summary.first_timestamp = base::Time::Now();
  summary.last_timestamp = base::Time::Now();

  service->SaveConversationSummary(
      std::move(summary),
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

void DaoAgentMemoryHandler::HandleGetPageContentForScenario(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  int tab_id = args[1].is_int() ? args[1].GetInt() : -1;

  // Find the tab by unique ID across all browsers.
  content::WebContents* target = nullptr;
  for (Browser* browser : *BrowserList::GetInstance()) {
    TabStripModel* model = browser->tab_strip_model();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (wc && wc->GetPrimaryMainFrame()->GetRoutingID() == tab_id) {
        target = wc;
        break;
      }
    }
    if (target) {
      break;
    }
  }

  if (!target) {
    base::Value::Dict error;
    error.Set("error", "Tab not found");
    ResolveJavascriptCallback(base::Value(callback_id), error);
    return;
  }

  // Extract text content via JS.
  static constexpr char kExtractScript[] = R"js(
    (function() {
      return document.body ? document.body.innerText : '';
    })()
  )js";

  content::RenderFrameHost* rfh = target->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    base::Value::Dict error;
    error.Set("error", "Frame not available");
    ResolveJavascriptCallback(base::Value(callback_id), error);
    return;
  }

  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string_view(kExtractScript)),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentMemoryHandler> handler,
             std::string cb_id, base::Value result) {
            if (!handler) {
              return;
            }
            base::Value::Dict response;
            if (result.is_string()) {
              response.Set("text", result.GetString());
            } else {
              response.Set("text", "");
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

// ---- DaoAgentSkillHandler ----

DaoAgentSkillHandler::DaoAgentSkillHandler() = default;

DaoAgentSkillHandler::~DaoAgentSkillHandler() = default;

DaoAgentSkillService* DaoAgentSkillHandler::GetSkillService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return DaoAgentSkillServiceFactory::GetForProfile(profile);
}

void DaoAgentSkillHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getSkillRegistry",
      base::BindRepeating(&DaoAgentSkillHandler::HandleGetSkillRegistry,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getSkillContent",
      base::BindRepeating(&DaoAgentSkillHandler::HandleGetSkillContent,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "saveUserSkill",
      base::BindRepeating(&DaoAgentSkillHandler::HandleSaveUserSkill,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "deleteUserSkill",
      base::BindRepeating(&DaoAgentSkillHandler::HandleDeleteUserSkill,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openSkillsDirectory",
      base::BindRepeating(&DaoAgentSkillHandler::HandleOpenSkillsDirectory,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openSkillManager",
      base::BindRepeating(&DaoAgentSkillHandler::HandleOpenSkillManager,
                          base::Unretained(this)));
}

void DaoAgentSkillHandler::HandleGetSkillRegistry(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetSkillService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::List()));
    return;
  }

  service->GetSkillRegistry(base::BindOnce(
      [](base::WeakPtr<DaoAgentSkillHandler> handler, std::string cb_id,
         std::vector<SkillRegistryEntry> entries) {
        if (!handler) {
          return;
        }
        base::Value::List list;
        for (const auto& entry : entries) {
          base::Value::Dict dict;
          dict.Set("id", entry.id);
          dict.Set("name", entry.name);
          dict.Set("description", entry.description);
          dict.Set("source", entry.source);
          dict.Set("requiresPageContent", entry.requires_page_content);

          base::Value::List hosts_list;
          for (const auto& host : entry.hosts) {
            hosts_list.Append(host);
          }
          dict.Set("hosts", std::move(hosts_list));

          list.Append(std::move(dict));
        }
        handler->ResolveJavascriptCallback(base::Value(cb_id), list);
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleGetSkillContent(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id =
      args[1].is_string() ? args[1].GetString() : "";

  auto* service = GetSkillService();
  if (!service || skill_id.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::Value::Dict()));
    return;
  }

  service->GetSkillContent(
      skill_id,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentSkillHandler> handler, std::string cb_id,
             std::optional<SkillContent> content) {
            if (!handler) {
              return;
            }
            if (!content.has_value()) {
              handler->ResolveJavascriptCallback(
                  base::Value(cb_id), base::Value(base::Value::Dict()));
              return;
            }
            base::Value::Dict result;
            result.Set("instructions", content->instructions);

            base::Value::Dict metadata;
            metadata.Set("id", content->metadata.id);
            metadata.Set("name", content->metadata.name);
            metadata.Set("description", content->metadata.description);
            metadata.Set("source", content->metadata.source);
            metadata.Set("requiresPageContent",
                         content->metadata.requires_page_content);

            base::Value::List hosts_list;
            for (const auto& host : content->metadata.hosts) {
              hosts_list.Append(host);
            }
            metadata.Set("hosts", std::move(hosts_list));

            result.Set("metadata", std::move(metadata));
            handler->ResolveJavascriptCallback(base::Value(cb_id), result);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleSaveUserSkill(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id =
      args[1].is_string() ? args[1].GetString() : "";
  const std::string skill_md =
      args[2].is_string() ? args[2].GetString() : "";
  const std::string host =
      args[3].is_string() ? args[3].GetString() : "";

  auto* service = GetSkillService();
  if (!service || skill_id.empty() || skill_md.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->SaveUserSkill(
      skill_id, skill_md, host,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentSkillHandler> handler, std::string cb_id,
             bool success) {
            if (!handler) {
              return;
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id),
                                               base::Value(success));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleDeleteUserSkill(
    const base::Value::List& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id =
      args[1].is_string() ? args[1].GetString() : "";

  auto* service = GetSkillService();
  if (!service || skill_id.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->DeleteUserSkill(
      skill_id,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentSkillHandler> handler, std::string cb_id,
             bool success) {
            if (!handler) {
              return;
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id),
                                               base::Value(success));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleOpenSkillsDirectory(
    const base::Value::List& args) {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::FilePath skills_path =
      profile->GetPath().AppendASCII("DaoAgentSkills");
  platform_util::OpenItem(profile, skills_path, platform_util::OPEN_FOLDER,
                          platform_util::OpenOperationCallback());
}

void DaoAgentSkillHandler::HandleOpenSkillManager(
    const base::Value::List& args) {
  Browser* browser = BrowserList::GetInstance()->GetLastActive();
  if (!browser) {
    return;
  }
  GURL skills_url(std::string(content::kChromeUIScheme) + "://skills");
  NavigateParams params(browser, skills_url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
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
      "trusted-types default lit-html-desktop lit-html;");

  // Register message handlers.
  web_ui->AddMessageHandler(std::make_unique<DaoAgentUIHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentMemoryHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentSkillHandler>());
}

DaoAgentUI::~DaoAgentUI() = default;

// ---- DaoSkillsUIConfig ----

DaoSkillsUIConfig::DaoSkillsUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "skills") {}

std::unique_ptr<content::WebUIController>
DaoSkillsUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                         const GURL& url) {
  return std::make_unique<DaoSkillsUI>(web_ui);
}

// ---- DaoSkillsUI ----

DaoSkillsUI::DaoSkillsUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "skills");

  // Reuse the agent resource bundle (skills.html is compiled there).
  source->AddResourcePaths(kDaoAgentResources);
  source->SetDefaultResource(IDR_DAO_AGENT_SKILLS_HTML);

  // Allow Lit HTML rendering.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop lit-html;");

  // Register skill message handler only.
  web_ui->AddMessageHandler(std::make_unique<DaoAgentSkillHandler>());
}

DaoSkillsUI::~DaoSkillsUI() = default;

}  // namespace dao
