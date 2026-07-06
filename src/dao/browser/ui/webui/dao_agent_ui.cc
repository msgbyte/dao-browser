// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_agent_ui.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/files/file_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
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
#include "components/pdf/browser/pdf_document_helper.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_agent_skill_service.h"
#include "dao/browser/agent/dao_agent_skill_service_factory.h"
#include "dao/browser/agent/dao_agent_workspace_service.h"
#include "dao/browser/agent/dao_agent_workspace_service_factory.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/agent/dao_dream_service_factory.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/workspace_quota.h"
#include "dao/browser/dao_pref_names.h"
#include "content/public/browser/storage_partition.h"
#include "net/base/load_flags.h"
#include "net/cookies/site_for_cookies.h"
#include "net/http/http_response_headers.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "pdf/mojom/pdf.mojom.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "services/network/public/mojom/fetch_api.mojom.h"
#include "services/network/public/mojom/url_response_head.mojom.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/window_open_disposition.h"
#include "url/origin.h"

namespace dao {

namespace {

// Populate an ActionFeedback from a JS dict. Callers may normalize outcome.
ActionFeedback ParseActionFeedbackFromDict(const base::DictValue& d) {
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

base::DictValue DreamReportToDict(const DreamReport& report) {
  base::DictValue dict;
  dict.Set("id", static_cast<double>(report.id));
  dict.Set("dreamDate", report.dream_date);
  dict.Set("reportMarkdown", report.report_markdown);
  dict.Set("habitCandidates", report.habit_candidates);
  dict.Set("materialStats", report.material_stats);
  dict.Set("debugMaterialJson", report.debug_material_json);
  dict.Set("triggerKind", report.trigger_kind);
  if (!report.created_at.is_null()) {
    dict.Set("createdAt",
             static_cast<double>(
                 report.created_at.InMillisecondsSinceUnixEpoch()));
  }
  return dict;
}

base::ListValue DreamReportsToList(const std::vector<DreamReport>& reports) {
  base::ListValue list;
  for (const DreamReport& report : reports) {
    list.Append(DreamReportToDict(report));
  }
  return list;
}

base::ListValue DreamExcludedDomainsToList(Profile* profile) {
  base::ListValue list;
  for (const std::string& domain : LoadDreamExcludedDomains(profile)) {
    list.Append(domain);
  }
  return list;
}

void SaveDreamExcludedDomains(Profile* profile,
                              const std::set<std::string>& domains) {
  base::ListValue list;
  for (const std::string& domain : domains) {
    list.Append(domain);
  }
  profile->GetPrefs()->SetList(prefs::kDaoDreamExcludedDomains,
                               std::move(list));
}

std::string ReadDomainArgument(const base::ListValue& args) {
  if (args.size() < 2) {
    return std::string();
  }
  if (args[1].is_string()) {
    return args[1].GetString();
  }
  if (args[1].is_dict()) {
    if (const std::string* domain = args[1].GetDict().FindString("domain")) {
      return *domain;
    }
  }
  return std::string();
}

base::DictValue MemorySqlQueryResultToDict(
    const MemorySqlQueryResult& result) {
  base::DictValue dict;
  dict.Set("ok", result.ok);
  dict.Set("error", result.error);
  dict.Set("truncated", result.truncated);

  base::ListValue columns;
  for (const std::string& column : result.columns) {
    columns.Append(column);
  }
  dict.Set("columns", std::move(columns));

  base::ListValue rows;
  for (const auto& source_row : result.rows) {
    base::ListValue row;
    for (const MemorySqlCell& source_cell : source_row) {
      base::DictValue cell;
      cell.Set("type", source_cell.type);
      cell.Set("value", source_cell.value);
      row.Append(std::move(cell));
    }
    rows.Append(std::move(row));
  }
  dict.Set("rows", std::move(rows));

  return dict;
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
  highlight.style.cssText = 'position:fixed;border:2px solid rgba(70,120,190,0.6);background:rgba(70,120,190,0.08);border-radius:0;pointer-events:none;opacity:0;transition:opacity 150ms ease;display:none;';
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

bool ShouldCountProactiveOutcomeAsDismissedForScenarioStats(
    const std::string& outcome) {
  return outcome == "not_now" || outcome == "not_helpful";
}

base::DictValue SerializeMemoryContextForAgentUi(
    const MemoryContext& context) {
  base::DictValue result;

  base::ListValue prefs;
  for (const auto& p : context.preferences) {
    base::DictValue pref;
    pref.Set("key", p.key);
    pref.Set("value", p.value);
    pref.Set("confidence", p.confidence);
    prefs.Append(std::move(pref));
  }
  result.Set("preferences", std::move(prefs));

  base::ListValue eps;
  for (const auto& e : context.episodes) {
    base::DictValue ep;
    ep.Set("intent", e.intent);
    ep.Set("outcome", e.outcome);
    ep.Set("timestamp",
           static_cast<double>(
               e.timestamp.ToDeltaSinceWindowsEpoch().InMicroseconds()));
    eps.Append(std::move(ep));
  }
  result.Set("episodes", std::move(eps));

  if (context.relevant_summary.has_value()) {
    const ConversationSummary& summary = *context.relevant_summary;
    base::DictValue relevant_summary;
    relevant_summary.Set("summary", summary.summary);
    relevant_summary.Set("messageCount", summary.message_count);
    relevant_summary.Set(
        "firstTimestamp",
        static_cast<double>(
            summary.first_timestamp.ToDeltaSinceWindowsEpoch()
                .InMicroseconds()));
    relevant_summary.Set(
        "lastTimestamp",
        static_cast<double>(
            summary.last_timestamp.ToDeltaSinceWindowsEpoch()
                .InMicroseconds()));
    relevant_summary.Set("primaryDomain", summary.primary_domain);
    result.Set("relevantSummary", std::move(relevant_summary));
  }

  return result;
}

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
                                          base::DictValue params,
                                          ResponseCallback callback) {
  if (!agent_host_) {
    std::move(callback).Run(base::Value("Not attached"));
    return;
  }

  int id = next_command_id_++;
  pending_callbacks_[id] = std::move(callback);

  base::DictValue command;
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

  auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
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
        base::DictValue empty;
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
      "beginAgentTurn",
      base::BindRepeating(&DaoAgentUIHandler::HandleBeginAgentTurn,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "endAgentTurn",
      base::BindRepeating(&DaoAgentUIHandler::HandleEndAgentTurn,
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
  web_ui()->RegisterMessageCallback(
      "getPdfText",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetPdfText,
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
  web_ui()->RegisterMessageCallback(
      "focusAgentSidebar",
      base::BindRepeating(&DaoAgentUIHandler::HandleFocusAgentSidebar,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPageHtml",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetPageHtml,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "listPageResources",
      base::BindRepeating(&DaoAgentUIHandler::HandleListPageResources,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getResourceContent",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetResourceContent,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getNetworkBody",
      base::BindRepeating(&DaoAgentUIHandler::HandleGetNetworkBody,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "nativeFetch",
      base::BindRepeating(&DaoAgentUIHandler::HandleNativeFetch,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceDownload",
      base::BindRepeating(&DaoAgentUIHandler::HandleWorkspaceDownload,
                          base::Unretained(this)));
}

content::WebContents* DaoAgentUIHandler::GetActivePageContents() {
  Browser* browser = chrome::FindLastActive();
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

  return contents;
}

content::WebContents* DaoAgentUIHandler::ResolveTargetContents() {
  if (agent_turn_target_) {
    content::WebContents* contents = agent_turn_target_.get();
    if (contents && contents->GetURL().host() != "agent") {
      return contents;
    }
    agent_turn_target_.reset();
  }

  return GetActivePageContents();
}

content::WebContents* DaoAgentUIHandler::EnsureAttached() {
  content::WebContents* contents = ResolveTargetContents();
  if (!contents) {
    return nullptr;
  }

  if (!devtools_client_->AttachTo(contents)) {
    return nullptr;
  }

  return contents;
}

void DaoAgentUIHandler::HandleBeginAgentTurn(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = GetActivePageContents();
  base::DictValue response;
  if (!contents) {
    agent_turn_target_.reset();
    response.Set("error", "No active tab");
  } else {
    agent_turn_target_ = contents->GetWeakPtr();
    response.Set("success", true);
    response.Set("url", contents->GetVisibleURL().spec());
    response.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleEndAgentTurn(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  agent_turn_target_.reset();
  base::DictValue response;
  response.Set("success", true);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleGetPageInfo(const base::ListValue& args) {
  AllowJavascript();

  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = ResolveTargetContents();

  base::DictValue response;
  if (contents) {
    response.Set("url", contents->GetURL().spec());
    response.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleClickElement(const base::ListValue& args) {
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
    base::DictValue response;
    response.Set("error", "No active tab or invalid selector");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Domain security check.
  if (!expected_domain_.empty()) {
    std::string current_domain = std::string(contents->GetURL().host());
    if (current_domain != expected_domain_) {
      base::DictValue response;
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

  // Do NOT Lock the tab for click-class tools: WebContents::IgnoreInputEvents
  // filters BOTH real user input AND CDP-synthesized Input.dispatchMouseEvent,
  // which would silently drop the click we are about to dispatch.
  base::DictValue params;
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
            base::DictValue response;
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

void DaoAgentUIHandler::HandleExecuteScript(const base::ListValue& args) {
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
    base::DictValue response;
    response.Set("error", "No active tab or empty code");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Domain security check for locked (page-manipulating) scripts.
  if (lock_tab && !expected_domain_.empty()) {
    std::string current_domain = std::string(contents->GetURL().host());
    if (current_domain != expected_domain_) {
      base::DictValue response;
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
  base::DictValue params;
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
            base::DictValue response;
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

DaoAgentUIHandler::PdfCaptureState::PdfCaptureState() = default;
DaoAgentUIHandler::PdfCaptureState::~PdfCaptureState() = default;

void DaoAgentUIHandler::HandleGetPdfText(const base::ListValue& args) {
  AllowJavascript();

  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolvePdfCaptureNotPdf(callback_id);
    return;
  }

  pdf::PDFDocumentHelper* helper =
      pdf::PDFDocumentHelper::MaybeGetForWebContents(contents);
  if (!helper) {
    ResolvePdfCaptureNotPdf(callback_id);
    return;
  }

  auto state = std::make_unique<PdfCaptureState>();
  state->callback_id = callback_id;
  state->initial_url = contents->GetVisibleURL();
  state->title = contents->GetTitle();

  if (helper->IsDocumentLoadComplete()) {
    StartPdfCapture(std::move(state), helper);
    return;
  }

  // Wait for load. RegisterForDocumentLoadComplete has no built-in
  // timeout; we layer a 5-second safety timer on top. Whichever fires
  // first resolves the callback; the other becomes a no-op since
  // ResolveJavascriptCallback on an already-resolved id is silently
  // dropped by Chromium's WebUI.
  GURL initial_url_copy = state->initial_url;
  helper->RegisterForDocumentLoadComplete(base::BindOnce(
      [](base::WeakPtr<DaoAgentUIHandler> handler,
         std::unique_ptr<PdfCaptureState> s) {
        if (!handler) {
          return;
        }
        content::WebContents* c = handler->EnsureAttached();
        if (!c) {
          handler->ResolvePdfCaptureError(*s, "WebContents went away");
          return;
        }
        pdf::PDFDocumentHelper* h =
            pdf::PDFDocumentHelper::MaybeGetForWebContents(c);
        if (!h) {
          handler->ResolvePdfCaptureError(*s, "PDF helper went away");
          return;
        }
        handler->StartPdfCapture(std::move(s), h);
      },
      weak_factory_.GetWeakPtr(), std::move(state)));

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string cb_id, GURL initial_url) {
            if (!handler) {
              return;
            }
            PdfCaptureState tmp;
            tmp.callback_id = cb_id;
            tmp.initial_url = initial_url;
            handler->ResolvePdfCaptureError(tmp, "PDF still loading");
          },
          weak_factory_.GetWeakPtr(), callback_id, initial_url_copy),
      base::Seconds(5));
}

void DaoAgentUIHandler::StartPdfCapture(
    std::unique_ptr<PdfCaptureState> state,
    pdf::PDFDocumentHelper* helper) {
  helper->GetPdfBytes(
      /*size_limit=*/0,
      base::BindOnce(&DaoAgentUIHandler::OnPdfBytesReceived,
                     weak_factory_.GetWeakPtr(), std::move(state)));
}

void DaoAgentUIHandler::OnPdfBytesReceived(
    std::unique_ptr<PdfCaptureState> state,
    pdf::mojom::PdfListener_GetPdfBytesStatus status,
    const std::vector<uint8_t>& /*bytes*/,
    uint32_t page_count) {
  // We pass size_limit=0 to avoid copying PDF bytes; we only need
  // page_count. A status of kSizeLimitExceeded is therefore expected
  // and benign — page_count is still populated. Only kFailed is a real
  // error here.
  if (status == pdf::mojom::PdfListener::GetPdfBytesStatus::kFailed ||
      page_count == 0) {
    ResolvePdfCaptureError(*state, "Failed to read PDF");
    return;
  }
  state->page_count = static_cast<int32_t>(page_count);
  state->next_page = 0;
  FetchNextPdfPage(std::move(state));
}

void DaoAgentUIHandler::FetchNextPdfPage(
    std::unique_ptr<PdfCaptureState> state) {
  if (state->next_page >= state->page_count) {
    ResolvePdfCapture(*state, /*truncated=*/false, std::nullopt);
    return;
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolvePdfCaptureError(*state, "WebContents went away");
    return;
  }
  if (contents->GetVisibleURL() != state->initial_url) {
    ResolvePdfCaptureError(*state, "Navigation occurred during capture");
    return;
  }
  pdf::PDFDocumentHelper* helper =
      pdf::PDFDocumentHelper::MaybeGetForWebContents(contents);
  if (!helper) {
    ResolvePdfCaptureError(*state, "PDF helper went away");
    return;
  }

  int32_t page_index = state->next_page;
  helper->GetPageText(
      page_index,
      base::BindOnce(&DaoAgentUIHandler::OnPdfPageText,
                     weak_factory_.GetWeakPtr(), std::move(state)));
}

void DaoAgentUIHandler::OnPdfPageText(
    std::unique_ptr<PdfCaptureState> state,
    const std::u16string& page_text) {
  int32_t one_based = state->next_page + 1;
  state->text += "\n\n--- Page ";
  state->text += base::NumberToString(one_based);
  state->text += " ---\n\n";
  state->text += base::UTF16ToUTF8(page_text);

  if (state->text.size() >= PdfCaptureState::kBudgetBytes) {
    state->text += "\n\n[... truncated. Total ";
    state->text += base::NumberToString(state->page_count);
    state->text += " pages, captured first ";
    state->text += base::NumberToString(one_based);
    state->text += " pages.]";
    ResolvePdfCapture(*state, /*truncated=*/true, one_based);
    return;
  }

  state->next_page++;
  FetchNextPdfPage(std::move(state));
}

void DaoAgentUIHandler::ResolvePdfCapture(
    const PdfCaptureState& state,
    bool truncated,
    std::optional<int32_t> truncated_at_page) {
  base::DictValue response;
  response.Set("isPdf", true);
  response.Set("url", state.initial_url.spec());
  response.Set("title", base::UTF16ToUTF8(state.title));
  response.Set("pageCount", state.page_count);
  response.Set("text", state.text);
  response.Set("truncated", truncated);
  if (truncated_at_page.has_value()) {
    response.Set("truncatedAtPage", *truncated_at_page);
  }
  ResolveJavascriptCallback(base::Value(state.callback_id), response);
}

void DaoAgentUIHandler::ResolvePdfCaptureError(
    const PdfCaptureState& state,
    const std::string& error_message) {
  base::DictValue response;
  response.Set("isPdf", true);
  response.Set("error", error_message);
  ResolveJavascriptCallback(base::Value(state.callback_id), response);
}

void DaoAgentUIHandler::ResolvePdfCaptureNotPdf(
    const std::string& callback_id) {
  base::DictValue response;
  response.Set("isPdf", false);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleHighlightElement(
    const base::ListValue& args) {
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
    base::DictValue r;
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

  base::DictValue params;
  params.Set("expression", inject_then_show);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
            response.Set("success", true);
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleClearHighlight(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
    return;
  }

  base::DictValue params;
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

void DaoAgentUIHandler::HandleMoveCursor(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  double vx = 0, vy = 0;
  if (args[1].is_dict()) {
    vx = args[1].GetDict().FindDouble("x").value_or(0);
    vy = args[1].GetDict().FindDouble("y").value_or(0);
  }

  content::WebContents* wc = ResolveTargetContents();
  Browser* browser = wc ? chrome::FindBrowserWithTab(wc) : nullptr;
  if (!browser) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value("No target browser"));
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

void DaoAgentUIHandler::HandleAgentClick(const base::ListValue& args) {
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
    base::DictValue r;
    r.Set("error", "No active tab or invalid selector");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  // Domain security check.
  if (!expected_domain_.empty()) {
    std::string current_domain = std::string(contents->GetURL().host());
    if (current_domain != expected_domain_) {
      base::DictValue r;
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

  // Do NOT Lock the tab for click-class tools: WebContents::IgnoreInputEvents
  // filters BOTH real user input AND CDP-synthesized Input.dispatchMouseEvent,
  // which would silently drop the click we are about to dispatch.

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

  base::DictValue params;
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

            auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict() ||
                parsed->GetDict().FindString("error")) {
              UnlockLockedTab(locked_contents);
              base::DictValue r;
              r.Set("error", json_str.empty() ? "element not found" : json_str);
              handler->ResolveJavascriptCallback(
                  base::Value(callback_id), r);
              return;
            }

            double vx = parsed->GetDict().FindDouble("x").value_or(0);
            double vy = parsed->GetDict().FindDouble("y").value_or(0);

            Browser* browser = chrome::FindBrowserWithTab(locked_contents);
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
                      Browser* br = chrome::FindBrowserWithTab(locked);
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
  base::DictValue move_params;
  move_params.Set("type", "mouseMoved");
  move_params.Set("x", static_cast<int>(viewport_x));
  move_params.Set("y", static_cast<int>(viewport_y));
  move_params.Set("button", "none");
  move_params.Set("buttons", 0);

  base::DictValue press_params;
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
             base::DictValue press_params,
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
    base::DictValue press_params) {
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
            base::DictValue release_params;
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
                        base::DictValue clear_params;
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
                                  base::DictValue r;
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
    const base::ListValue& args) {
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
    base::DictValue response;
    response.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Escape filter for safe inclusion in the IIFE call.
  std::string js = std::string(kAccessibilityTreeScript) +
                   "('" + filter + "')";

  base::DictValue params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                auto parsed = base::JSONReader::Read(value->GetString(), base::JSON_PARSE_RFC);
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

void DaoAgentUIHandler::HandleClickByRef(const base::ListValue& args) {
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
    base::DictValue response;
    response.Set("error", "No active tab or invalid ref_id");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  // Domain security check.
  if (!expected_domain_.empty()) {
    std::string current_domain = std::string(contents->GetURL().host());
    if (current_domain != expected_domain_) {
      base::DictValue response;
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

  // Do NOT Lock the tab for click-class tools: WebContents::IgnoreInputEvents
  // filters BOTH real user input AND CDP-synthesized Input.dispatchMouseEvent,
  // which would silently drop the click we are about to dispatch.

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

  base::DictValue params;
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

            auto parsed = base::JSONReader::Read(json_str, base::JSON_PARSE_RFC);
            if (!parsed || !parsed->is_dict() ||
                parsed->GetDict().FindString("error")) {
              UnlockLockedTab(locked_contents);
              base::DictValue r;
              r.Set("error",
                    json_str.empty() ? "element not found" : json_str);
              handler->ResolveJavascriptCallback(
                  base::Value(callback_id), r);
              return;
            }

            double vx = parsed->GetDict().FindDouble("x").value_or(0);
            double vy = parsed->GetDict().FindDouble("y").value_or(0);

            // Move cursor then click, same as HandleAgentClick.
            Browser* browser = chrome::FindBrowserWithTab(locked_contents);
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
                      Browser* br = chrome::FindBrowserWithTab(locked);
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::DictValue response;
    response.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  base::DictValue params;
  params.Set("format", "jpeg");
  params.Set("quality", 60);
  if (args.size() >= 2 && args[1].is_dict()) {
    const base::DictValue& request = args[1].GetDict();
    const base::DictValue* clip = request.FindDict("clip");
    if (clip) {
      const std::optional<double> x = clip->FindDouble("x");
      const std::optional<double> y = clip->FindDouble("y");
      const std::optional<double> width = clip->FindDouble("width");
      const std::optional<double> height = clip->FindDouble("height");
      const double scale = clip->FindDouble("scale").value_or(1.0);
      if (!x || !y || !width || !height || *width <= 0 || *height <= 0 ||
          scale <= 0) {
        base::DictValue response;
        response.Set("error", "Invalid screenshot clip");
        ResolveJavascriptCallback(base::Value(callback_id), response);
        return;
      }
      base::DictValue cdp_clip;
      cdp_clip.Set("x", *x);
      cdp_clip.Set("y", *y);
      cdp_clip.Set("width", *width);
      cdp_clip.Set("height", *height);
      cdp_clip.Set("scale", scale);
      params.Set("clip", std::move(cdp_clip));
    }
  }

  devtools_client_->SendCommand(
      "Page.captureScreenshot", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
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

void DaoAgentUIHandler::HandleScrollPage(const base::ListValue& args) {
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
    base::DictValue response;
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

  base::DictValue params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                auto parsed = base::JSONReader::Read(value->GetString(), base::JSON_PARSE_RFC);
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
    const base::ListValue& args) {
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
    base::DictValue response;
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

  base::DictValue params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                auto parsed = base::JSONReader::Read(value->GetString(), base::JSON_PARSE_RFC);
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  if (args[1].is_dict()) {
    auto* domain = args[1].GetDict().FindString("domain");
    if (domain) {
      expected_domain_ = *domain;
    }
  }

  base::DictValue response;
  response.Set("success", true);
  response.Set("domain", expected_domain_);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

// ---- Tab Management ----

void DaoAgentUIHandler::HandleListTabs(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* target_contents = ResolveTargetContents();
  Browser* browser =
      target_contents ? chrome::FindBrowserWithTab(target_contents) : nullptr;
  if (!browser) {
    browser = chrome::FindLastActive();
  }
  base::ListValue tabs_list;

  if (browser) {
    TabStripModel* model = browser->tab_strip_model();
    int active_index = model->active_index();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (!wc) continue;
      base::DictValue tab;
      tab.Set("index", i);
      tab.Set("url", wc->GetURL().spec());
      tab.Set("title", base::UTF16ToUTF8(wc->GetTitle()));
      tab.Set("active", i == active_index);
      tabs_list.Append(std::move(tab));
    }
  }

  base::DictValue response;
  response.Set("tabs", std::move(tabs_list));
  response.Set("count", static_cast<int>(tabs_list.size()));
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleSwitchTab(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  int index = -1;
  if (args[1].is_dict()) {
    index = args[1].GetDict().FindInt("index").value_or(-1);
  }

  content::WebContents* target_contents = ResolveTargetContents();
  Browser* browser =
      target_contents ? chrome::FindBrowserWithTab(target_contents) : nullptr;
  if (!browser) {
    browser = chrome::FindLastActive();
  }
  base::DictValue response;

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
    if (wc) {
      agent_turn_target_ = wc->GetWeakPtr();
    }
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleOpenTab(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string url_str;
  if (args[1].is_dict()) {
    auto* u = args[1].GetDict().FindString("url");
    if (u) url_str = *u;
  }

  content::WebContents* target_contents = ResolveTargetContents();
  Browser* browser =
      target_contents ? chrome::FindBrowserWithTab(target_contents) : nullptr;
  if (!browser) {
    browser = chrome::FindLastActive();
  }
  base::DictValue response;

  if (!browser) {
    response.Set("error", "No active browser");
  } else {
    GURL url(url_str);
    if (!url.is_valid()) {
      url = GURL("about:blank");
    }
    NavigateParams params(browser, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    const int active_index = browser->tab_strip_model()->active_index();
    if (active_index >= 0) {
      params.tabstrip_index = active_index + 1;
    }
    Navigate(&params);
    if (params.navigated_or_inserted_contents) {
      agent_turn_target_ = params.navigated_or_inserted_contents->GetWeakPtr();
    }
    response.Set("success", true);
    response.Set("url", url.spec());
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleCloseTab(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  int index = -1;
  if (args[1].is_dict()) {
    index = args[1].GetDict().FindInt("index").value_or(-1);
  }

  content::WebContents* target_contents = ResolveTargetContents();
  Browser* browser =
      target_contents ? chrome::FindBrowserWithTab(target_contents) : nullptr;
  if (!browser) {
    browser = chrome::FindLastActive();
  }
  base::DictValue response;

  if (!browser) {
    response.Set("error", "No active browser");
  } else {
    TabStripModel* model = browser->tab_strip_model();
    // Default to active tab.
    if (index < 0) {
      index = target_contents ? model->GetIndexOfWebContents(target_contents)
                              : model->active_index();
    }
    if (index < 0 || index >= model->count()) {
      response.Set("error", "Invalid tab index");
    } else if (model->count() <= 1) {
      response.Set("error", "Cannot close the last tab");
    } else {
      content::WebContents* closing_contents = model->GetWebContentsAt(index);
      model->CloseWebContentsAt(index,
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      if (closing_contents == target_contents) {
        content::WebContents* next_target = model->GetActiveWebContents();
        if (next_target) {
          agent_turn_target_ = next_target->GetWeakPtr();
        } else {
          agent_turn_target_.reset();
        }
      }
      response.Set("success", true);
    }
  }

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

// ---- Keyboard Input ----

void DaoAgentUIHandler::HandlePressKeyChord(
    const base::ListValue& args) {
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
    base::DictValue r;
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

  base::DictValue params;
  params.Set("expression", js);
  params.Set("returnByValue", true);

  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
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

void DaoAgentUIHandler::HandleTypeText(const base::ListValue& args) {
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
    base::DictValue r;
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
    base::DictValue clear_params;
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
              base::DictValue insert_params;
              insert_params.Set("text", escaped_text);
              handler->devtools_client_->SendCommand(
                  "Input.insertText", std::move(insert_params),
                  base::BindOnce(
                      [](base::WeakPtr<DaoAgentUIHandler> h,
                         std::string cb_id,
                         std::string text,
                         base::Value) {
                        if (!h) return;
                        base::DictValue r;
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
    base::DictValue insert_params;
    insert_params.Set("text", text);
    devtools_client_->SendCommand(
        "Input.insertText", std::move(insert_params),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentUIHandler> handler,
               std::string callback_id,
               std::string text,
               base::Value) {
              if (!handler) return;
              base::DictValue r;
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
                                    const base::DictValue& params) {
  if (network_tracking_enabled_) {
    if (method == "Network.requestWillBeSent") {
      base::DictValue entry;
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
      base::DictValue entry;
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
      base::DictValue entry;
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
      base::DictValue entry;
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::DictValue r;
    r.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  network_tracking_enabled_ = true;
  network_requests_.clear();

  base::DictValue params;
  devtools_client_->SendCommand(
      "Network.enable", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value) {
            if (!handler) return;
            base::DictValue r;
            r.Set("success", true);
            r.Set("message", "Network tracking enabled");
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), r);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleGetNetworkRequests(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  base::ListValue list;
  for (const auto& entry : network_requests_) {
    list.Append(entry.Clone());
  }

  base::DictValue response;
  response.Set("requests", std::move(list));
  response.Set("count", static_cast<int>(network_requests_.size()));
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleClearNetworkRequests(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  network_requests_.clear();

  base::DictValue response;
  response.Set("success", true);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleEnableConsoleTracking(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::DictValue r;
    r.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  console_tracking_enabled_ = true;
  console_messages_.clear();

  base::DictValue params;
  devtools_client_->SendCommand(
      "Runtime.enable", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string callback_id, base::Value) {
            if (!handler) return;
            base::DictValue r;
            r.Set("success", true);
            r.Set("message", "Console tracking enabled");
            handler->ResolveJavascriptCallback(
                base::Value(callback_id), r);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentUIHandler::HandleGetConsoleMessages(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string filter;
  if (args.size() >= 2 && args[1].is_dict()) {
    auto* f = args[1].GetDict().FindString("filter");
    if (f) filter = *f;
  }

  base::ListValue list;
  for (const auto& entry : console_messages_) {
    if (!filter.empty()) {
      auto* type = entry.FindString("type");
      if (type && *type != filter) continue;
    }
    list.Append(entry.Clone());
  }

  base::DictValue response;
  int count = static_cast<int>(list.size());
  response.Set("messages", std::move(list));
  response.Set("count", count);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleClearConsoleMessages(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  console_messages_.clear();

  base::DictValue response;
  response.Set("success", true);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

namespace {

// Max bytes of source content we ship back to the WebUI per call. LLM
// context is expensive and DevTools itself also paginates large files;
// 512 KiB is enough to see most real-world bundles while keeping a
// single tool result under roughly 150 K tokens.
constexpr size_t kMaxSourceBytes = 512 * 1024;

// Truncate |s| in place to kMaxSourceBytes and return whether a cut
// happened. For base64 payloads we keep the whole buffer (callers care
// about decoding the result), so the helper is only used for text.
bool TruncateText(std::string* s) {
  if (s->size() <= kMaxSourceBytes) {
    return false;
  }
  s->resize(kMaxSourceBytes);
  s->append("\n...[truncated]");
  return true;
}

// Walk a Page.getResourceTree frameTree dict, flattening all resources
// into |out| with their owning frame id. Recurses into childFrames.
void FlattenResourceTree(const base::DictValue& frame_tree,
                         const std::string& type_filter,
                         base::ListValue* out) {
  const auto* frame = frame_tree.FindDict("frame");
  std::string frame_id;
  if (frame) {
    const auto* id = frame->FindString("id");
    if (id) {
      frame_id = *id;
    }
  }
  const auto* resources = frame_tree.FindList("resources");
  if (resources) {
    for (const auto& r : *resources) {
      if (!r.is_dict()) continue;
      const auto& rd = r.GetDict();
      const auto* url = rd.FindString("url");
      const auto* type = rd.FindString("type");
      if (!url || !type) continue;
      if (!type_filter.empty() && type_filter != "all" && *type != type_filter) {
        continue;
      }
      base::DictValue entry;
      entry.Set("url", *url);
      entry.Set("type", *type);
      if (const auto* mime = rd.FindString("mimeType")) {
        entry.Set("mimeType", *mime);
      }
      entry.Set("frameId", frame_id);
      out->Append(std::move(entry));
    }
  }
  const auto* children = frame_tree.FindList("childFrames");
  if (children) {
    for (const auto& child : *children) {
      if (child.is_dict()) {
        FlattenResourceTree(child.GetDict(), type_filter, out);
      }
    }
  }
}

// Find the frame id for the frame that owns |url| in a Page.getResourceTree
// payload. Falls back to the root frame id when the url isn't listed (the
// caller typically wants the main document in that case).
std::string FindFrameIdForUrl(const base::DictValue& frame_tree,
                              const std::string& url) {
  std::string root_id;
  if (const auto* frame = frame_tree.FindDict("frame")) {
    if (const auto* id = frame->FindString("id")) {
      root_id = *id;
    }
    if (const auto* frame_url = frame->FindString("url")) {
      if (*frame_url == url) {
        return root_id;
      }
    }
  }
  if (const auto* resources = frame_tree.FindList("resources")) {
    for (const auto& r : *resources) {
      if (!r.is_dict()) continue;
      const auto* u = r.GetDict().FindString("url");
      if (u && *u == url) {
        return root_id;
      }
    }
  }
  if (const auto* children = frame_tree.FindList("childFrames")) {
    for (const auto& child : *children) {
      if (!child.is_dict()) continue;
      std::string found = FindFrameIdForUrl(child.GetDict(), url);
      if (!found.empty()) {
        return found;
      }
    }
  }
  return root_id;
}

}  // namespace

void DaoAgentUIHandler::HandleGetPageHtml(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::DictValue r;
    r.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  const std::string url = contents->GetURL().spec();
  const std::string title = base::UTF16ToUTF8(contents->GetTitle());

  base::DictValue params;
  params.Set("expression", "document.documentElement.outerHTML");
  params.Set("returnByValue", true);
  devtools_client_->SendCommand(
      "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler,
             std::string cb_id, std::string url, std::string title,
             base::Value result) {
            if (!handler) return;
            base::DictValue response;
            response.Set("url", url);
            response.Set("title", title);
            if (result.is_dict()) {
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (value && value->is_string()) {
                std::string html = value->GetString();
                bool truncated = TruncateText(&html);
                response.Set("html", std::move(html));
                if (truncated) response.Set("truncated", true);
              } else {
                response.Set("error", "Failed to read outerHTML");
              }
            } else {
              response.Set("error", "CDP call failed");
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id, url, title));
}

void DaoAgentUIHandler::HandleListPageResources(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string type_filter;
  if (args.size() >= 2 && args[1].is_dict()) {
    if (const auto* t = args[1].GetDict().FindString("type_filter")) {
      type_filter = *t;
    }
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents) {
    base::DictValue r;
    r.Set("error", "No active tab");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  // Page.enable is idempotent — just call it then fetch the tree.
  // Flattened into named methods because nested lambdas + BindOnce capture
  // chains tripped confusing clang diagnostics.
  base::DictValue enable_params;
  devtools_client_->SendCommand(
      "Page.enable", std::move(enable_params),
      base::BindOnce(&DaoAgentUIHandler::OnPageEnableForResourceList,
                     weak_factory_.GetWeakPtr(), callback_id, type_filter));
}

void DaoAgentUIHandler::OnPageEnableForResourceList(
    std::string callback_id,
    std::string type_filter,
    base::Value /*unused Page.enable result*/) {
  base::DictValue p;
  devtools_client_->SendCommand(
      "Page.getResourceTree", std::move(p),
      base::BindOnce(&DaoAgentUIHandler::OnResourceTreeForResourceList,
                     weak_factory_.GetWeakPtr(), callback_id, type_filter));
}

void DaoAgentUIHandler::OnResourceTreeForResourceList(
    std::string callback_id,
    std::string type_filter,
    base::Value result) {
  base::DictValue response;
  base::ListValue resources;
  std::string main_frame_id;
  if (result.is_dict()) {
    const base::DictValue* tree = result.GetDict().FindDict("frameTree");
    if (tree) {
      FlattenResourceTree(*tree, type_filter, &resources);
      const base::DictValue* frame = tree->FindDict("frame");
      if (frame) {
        const std::string* fid = frame->FindString("id");
        if (fid) {
          main_frame_id = *fid;
        }
      }
    }
  }
  int count = static_cast<int>(resources.size());
  response.Set("resources", std::move(resources));
  response.Set("count", count);
  response.Set("mainFrameId", main_frame_id);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleGetResourceContent(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string url;
  std::string frame_id;
  if (args[1].is_dict()) {
    if (const auto* u = args[1].GetDict().FindString("url")) url = *u;
    if (const auto* f = args[1].GetDict().FindString("frame_id")) {
      frame_id = *f;
    }
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || url.empty()) {
    base::DictValue r;
    r.Set("error", "No active tab or missing url");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  if (!frame_id.empty()) {
    FetchResourceContentAndReply(callback_id, url, frame_id);
    return;
  }

  // No frame_id given — look it up via Page.getResourceTree, then chain.
  base::DictValue enable_params;
  devtools_client_->SendCommand(
      "Page.enable", std::move(enable_params),
      base::BindOnce(&DaoAgentUIHandler::OnPageEnableForResourceFetch,
                     weak_factory_.GetWeakPtr(), callback_id, url));
}

void DaoAgentUIHandler::OnPageEnableForResourceFetch(
    std::string callback_id,
    std::string url,
    base::Value /*unused Page.enable result*/) {
  base::DictValue p;
  devtools_client_->SendCommand(
      "Page.getResourceTree", std::move(p),
      base::BindOnce(&DaoAgentUIHandler::OnResourceTreeForResourceFetch,
                     weak_factory_.GetWeakPtr(), callback_id, url));
}

void DaoAgentUIHandler::OnResourceTreeForResourceFetch(
    std::string callback_id,
    std::string url,
    base::Value result) {
  std::string frame_id;
  if (result.is_dict()) {
    const base::DictValue* tree = result.GetDict().FindDict("frameTree");
    if (tree) {
      frame_id = FindFrameIdForUrl(*tree, url);
    }
  }
  if (frame_id.empty()) {
    base::DictValue err;
    err.Set("url", url);
    err.Set("error", "Could not resolve frame id for url");
    ResolveJavascriptCallback(base::Value(callback_id), err);
    return;
  }
  FetchResourceContentAndReply(callback_id, url, frame_id);
}

void DaoAgentUIHandler::FetchResourceContentAndReply(
    const std::string& callback_id,
    const std::string& url,
    const std::string& frame_id) {
  base::DictValue p;
  p.Set("frameId", frame_id);
  p.Set("url", url);
  devtools_client_->SendCommand(
      "Page.getResourceContent", std::move(p),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> h, std::string id,
             std::string url, base::Value result) {
            if (!h) return;
            base::DictValue response;
            response.Set("url", url);
            if (result.is_dict()) {
              const auto& rd = result.GetDict();
              const auto* content = rd.FindString("content");
              bool b64 = rd.FindBool("base64Encoded").value_or(false);
              if (content) {
                if (b64) {
                  // Don't truncate base64 payloads — the caller needs
                  // the full buffer to decode.
                  response.Set("content", *content);
                  response.Set("base64_encoded", true);
                } else {
                  std::string c = *content;
                  bool truncated = TruncateText(&c);
                  response.Set("content", std::move(c));
                  response.Set("base64_encoded", false);
                  if (truncated) response.Set("truncated", true);
                }
              } else {
                response.Set("error", "No content for url");
              }
            } else {
              response.Set("error", "CDP call failed");
            }
            h->ResolveJavascriptCallback(base::Value(id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id, url));
}

void DaoAgentUIHandler::HandleGetNetworkBody(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) return;
  const std::string callback_id = args[0].GetString();

  std::string request_id;
  if (args[1].is_dict()) {
    if (const auto* r = args[1].GetDict().FindString("request_id")) {
      request_id = *r;
    }
  }

  content::WebContents* contents = EnsureAttached();
  if (!contents || request_id.empty()) {
    base::DictValue r;
    r.Set("error", "No active tab or missing request_id");
    ResolveJavascriptCallback(base::Value(callback_id), r);
    return;
  }

  base::DictValue params;
  params.Set("requestId", request_id);
  devtools_client_->SendCommand(
      "Network.getResponseBody", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> handler, std::string cb_id,
             std::string req_id, base::Value result) {
            if (!handler) return;
            base::DictValue response;
            response.Set("request_id", req_id);
            if (result.is_dict()) {
              const auto& rd = result.GetDict();
              const auto* body = rd.FindString("body");
              bool b64 = rd.FindBool("base64Encoded").value_or(false);
              if (body) {
                if (b64) {
                  response.Set("body", *body);
                  response.Set("base64_encoded", true);
                } else {
                  std::string b = *body;
                  bool truncated = TruncateText(&b);
                  response.Set("body", std::move(b));
                  response.Set("base64_encoded", false);
                  if (truncated) response.Set("truncated", true);
                }
              } else {
                response.Set("error", "No body returned (request may not have completed)");
              }
            } else {
              response.Set("error", "CDP call failed");
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), response);
          },
          weak_factory_.GetWeakPtr(), callback_id, request_id));
}

void DaoAgentUIHandler::HandleCloseSidebar(
    const base::ListValue& args) {
  Browser* browser = chrome::FindLastActive();
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

void DaoAgentUIHandler::HandleFocusAgentSidebar(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  Browser* browser = chrome::FindLastActive();
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  dao::DaoAgentSidebarView* sidebar =
      browser_view ? browser_view->dao_agent_sidebar() : nullptr;
  const bool success = sidebar && sidebar->RequestWebViewFocus();

  base::DictValue response;
  response.Set("success", success);
  if (!success) {
    response.Set("error", "Agent sidebar unavailable");
  }
  ResolveJavascriptCallback(base::Value(callback_id), response);
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
  RefreshPersonalScenarios();
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
  base::DictValue dict;
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
  dict.Set("reason", suggestion.reason);
  dict.Set("expectedOutcome", suggestion.expected_outcome);
  dict.Set("contextDisclosure", suggestion.context_disclosure);
  dict.Set("suppressionReason", suggestion.suppression_reason);
  dict.Set("scoreDebugJson", suggestion.score_debug_json);
  dict.Set("url", suggestion.url);
  dict.Set("domain", suggestion.domain);
  dict.Set("requiresPageContent", suggestion.requires_page_content);
  dict.Set("tabId", suggestion.tab_id);

  FireWebUIListener("proactiveSuggestion", dict);
}

void DaoAgentMemoryHandler::RefreshPersonalScenarios() {
  if (!proactive_engine_) {
    return;
  }

  auto* service = GetMemoryService();
  if (!service) {
    return;
  }

  service->GetPersonalScenarios(base::BindOnce(
      [](base::WeakPtr<DaoAgentMemoryHandler> handler,
         std::vector<ScenarioDefinition> scenarios) {
        if (!handler || !handler->proactive_engine_) {
          return;
        }
        handler->proactive_engine_->scenario_registry().SetPersonalScenarios(
            std::move(scenarios));
      },
      weak_factory_.GetWeakPtr()));
}

void DaoAgentMemoryHandler::HandleGetMemoryContext(
    const base::ListValue& args) {
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
                              base::Value(base::DictValue()));
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
            handler->ResolveJavascriptCallback(
                base::Value(cb_id), SerializeMemoryContextForAgentUi(ctx));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleEndSession(
    const base::ListValue& args) {
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  int limit = args[1].is_int() ? args[1].GetInt() : 20;

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::ListValue()));
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
            base::ListValue list;
            for (const auto& m : messages) {
              base::DictValue msg;
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::ListValue()));
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
            base::ListValue list;
            for (const auto& p : prefs) {
              base::DictValue pref;
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
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
                              base::Value(base::ListValue()));
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
            base::ListValue list;
            for (const auto& e : episodes) {
              base::DictValue ep;
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetMemoryService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::DictValue()));
    return;
  }

  service->GetStorageStats(base::BindOnce(
      [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
         StorageStats stats) {
        if (!handler) {
          return;
        }
        base::DictValue result;
        result.Set("totalSize", static_cast<int>(stats.total_size_bytes));
        result.Set("conversationCount", stats.conversation_count);
        result.Set("episodeCount", stats.episode_count);
        result.Set("preferenceCount", stats.preference_count);
        handler->ResolveJavascriptCallback(base::Value(cb_id), result);
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleDismissSuggestion(
    const base::ListValue& args) {
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
    feedback.outcome =
        feedback.outcome == "never_here" ? "never_here" : "dismissed";

    // Also bump scenario dismiss stats.
    if (!feedback.scenario_id.empty()) {
      service->UpdateScenarioStats(
          feedback.scenario_id, "times_dismissed",
          base::BindOnce(
              [](base::WeakPtr<DaoAgentMemoryHandler> handler, bool success) {
                if (success && handler) {
                  handler->RefreshPersonalScenarios();
                }
              },
              weak_factory_.GetWeakPtr()));
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
    const base::ListValue& args) {
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
    feedback.outcome = "accepted";

    // Bump scenario accepted stats.
    if (!feedback.scenario_id.empty()) {
      service->UpdateScenarioStats(
          feedback.scenario_id, "times_accepted",
          base::BindOnce(
              [](base::WeakPtr<DaoAgentMemoryHandler> handler, bool success) {
                if (success && handler) {
                  handler->RefreshPersonalScenarios();
                }
              },
              weak_factory_.GetWeakPtr()));
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  double threshold = args[1].is_double()
                         ? args[1].GetDouble()
                         : (args[1].is_int() ? static_cast<double>(args[1].GetInt())
                                             : 0.75);

  if (proactive_engine_) {
    proactive_engine_->SetConfidenceThreshold(threshold);
  }

  ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
}

void DaoAgentMemoryHandler::HandleRecordActionFeedback(
    const base::ListValue& args) {
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
  if (feedback.outcome == "shown" && !feedback.scenario_id.empty()) {
    if (proactive_engine_) {
      proactive_engine_->RecordShownScenarioForFeedback(
          feedback.url, feedback.domain, feedback.action_label,
          feedback.scenario_id, feedback.timestamp);
    }
    service->UpdateScenarioStats(
        feedback.scenario_id, "times_triggered",
        base::BindOnce(
            [](base::WeakPtr<DaoAgentMemoryHandler> handler, bool success) {
              if (success && handler) {
                handler->RefreshPersonalScenarios();
              }
            },
            weak_factory_.GetWeakPtr()));
  }
  if (ShouldCountProactiveOutcomeAsDismissedForScenarioStats(
          feedback.outcome) &&
      !feedback.scenario_id.empty()) {
    service->UpdateScenarioStats(
        feedback.scenario_id, "times_dismissed",
        base::BindOnce(
            [](base::WeakPtr<DaoAgentMemoryHandler> handler, bool success) {
              if (success && handler) {
                handler->RefreshPersonalScenarios();
              }
            },
            weak_factory_.GetWeakPtr()));
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
}

void DaoAgentMemoryHandler::HandleSaveEpisode(
    const base::ListValue& args) {
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  int tab_id = args[1].is_int() ? args[1].GetInt() : -1;

  // Find the tab by unique ID across all browsers.
  content::WebContents* target = nullptr;
  for (Browser* browser : chrome::FindAllBrowsersWithProfile(
           Profile::FromWebUI(web_ui()))) {
    TabStripModel* model = browser->tab_strip_model();
    for (int i = 0; i < model->count(); ++i) {
      content::WebContents* wc = model->GetWebContentsAt(i);
      if (wc && sessions::SessionTabHelper::IdForTab(wc).id() == tab_id) {
        target = wc;
        break;
      }
    }
    if (target) {
      break;
    }
  }

  if (!target) {
    base::DictValue error;
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
    base::DictValue error;
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
            base::DictValue response;
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

// ---- DaoDreamReportHandler ----

DaoDreamReportHandler::DaoDreamReportHandler() = default;

DaoDreamReportHandler::~DaoDreamReportHandler() = default;

void DaoDreamReportHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getDreamReport",
      base::BindRepeating(&DaoDreamReportHandler::HandleGetDreamReport,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDreamReports",
      base::BindRepeating(&DaoDreamReportHandler::HandleGetDreamReports,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getTodayDreamReport",
      base::BindRepeating(&DaoDreamReportHandler::HandleGetTodayDreamReport,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "markDreamReportViewed",
      base::BindRepeating(&DaoDreamReportHandler::HandleMarkDreamReportViewed,
                          base::Unretained(this)));
}

void DaoDreamReportHandler::HandleGetDreamReport(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }

  std::string date;
  if (args.size() >= 2 && args[1].is_dict()) {
    if (const std::string* d = args[1].GetDict().FindString("date")) {
      date = *d;
    }
  }

  auto reply = base::BindOnce(
      [](base::WeakPtr<DaoDreamReportHandler> self, std::string callback_id,
         std::optional<DreamReport> report) {
        if (!self) {
          return;
        }
        if (!report) {
          self->ResolveJavascriptCallback(base::Value(callback_id),
                                          base::Value());
          return;
        }
        self->ResolveJavascriptCallback(base::Value(callback_id),
                                        DreamReportToDict(*report));
      },
      weak_factory_.GetWeakPtr(), callback_id);

  if (!date.empty()) {
    memory->GetDreamReportByDate(date, std::move(reply));
  } else {
    memory->GetLatestDreamReport(std::move(reply));
  }
}

void DaoDreamReportHandler::HandleGetDreamReports(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    ResolveJavascriptCallback(base::Value(callback_id), base::ListValue());
    return;
  }

  int limit = 30;
  if (args.size() >= 2 && args[1].is_dict()) {
    limit = args[1].GetDict().FindInt("limit").value_or(limit);
  }
  memory->GetDreamReports(
      limit,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamReportHandler> self,
             std::string callback_id, std::vector<DreamReport> reports) {
            if (!self) {
              return;
            }
            self->ResolveJavascriptCallback(base::Value(callback_id),
                                            DreamReportsToList(reports));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoDreamReportHandler::HandleGetTodayDreamReport(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }

  const std::string dream_date =
      DaoDreamService::DreamDateFor(base::Time::Now());
  memory->GetDreamReportByDate(
      dream_date,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamReportHandler> self,
             std::string callback_id, std::optional<DreamReport> report) {
            if (!self) {
              return;
            }
            if (!report) {
              self->ResolveJavascriptCallback(base::Value(callback_id),
                                              base::Value());
              return;
            }
            self->ResolveJavascriptCallback(base::Value(callback_id),
                                            DreamReportToDict(*report));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoDreamReportHandler::HandleMarkDreamReportViewed(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() ||
      (!args[1].is_double() && !args[1].is_int())) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (memory) {
    memory->MarkDreamReportViewed(static_cast<int64_t>(args[1].GetDouble()),
                                  base::DoNothing());
  }
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
}

// ---- DaoMemoryBrowserHandler ----

DaoMemoryBrowserHandler::DaoMemoryBrowserHandler() = default;

DaoMemoryBrowserHandler::~DaoMemoryBrowserHandler() = default;

DaoAgentMemoryService* DaoMemoryBrowserHandler::GetMemoryService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return DaoAgentMemoryServiceFactory::GetForProfile(profile);
}

void DaoMemoryBrowserHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "memoryGetTables",
      base::BindRepeating(&DaoMemoryBrowserHandler::HandleGetTables,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "memoryExecuteSql",
      base::BindRepeating(&DaoMemoryBrowserHandler::HandleExecuteSql,
                          base::Unretained(this)));
}

void DaoMemoryBrowserHandler::HandleGetTables(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }

  const std::string callback_id = args[0].GetString();
  DaoAgentMemoryService* service = GetMemoryService();
  if (!service) {
    MemorySqlQueryResult result;
    result.error = "Agent memory is disabled";
    ResolveJavascriptCallback(base::Value(callback_id),
                              MemorySqlQueryResultToDict(result));
    return;
  }

  service->ExecuteReadOnlySqlForDebug(
      "SELECT name, type FROM sqlite_schema "
      "WHERE type IN ('table', 'view') "
      "AND name NOT LIKE 'sqlite_%' "
      "ORDER BY type, name",
      200,
      base::BindOnce(
          [](base::WeakPtr<DaoMemoryBrowserHandler> self,
             std::string callback_id, MemorySqlQueryResult result) {
            if (!self) {
              return;
            }
            self->ResolveJavascriptCallback(base::Value(callback_id),
                                            MemorySqlQueryResultToDict(result));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoMemoryBrowserHandler::HandleExecuteSql(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }

  const std::string callback_id = args[0].GetString();
  std::string sql;
  int max_rows = 100;
  if (args.size() >= 2 && args[1].is_dict()) {
    const base::DictValue& params = args[1].GetDict();
    if (const std::string* s = params.FindString("sql")) {
      sql = *s;
    }
    max_rows = params.FindInt("maxRows").value_or(max_rows);
  }
  max_rows = std::clamp(max_rows, 1, 500);

  DaoAgentMemoryService* service = GetMemoryService();
  if (!service) {
    MemorySqlQueryResult result;
    result.error = "Agent memory is disabled";
    ResolveJavascriptCallback(base::Value(callback_id),
                              MemorySqlQueryResultToDict(result));
    return;
  }

  service->ExecuteReadOnlySqlForDebug(
      sql, max_rows,
      base::BindOnce(
          [](base::WeakPtr<DaoMemoryBrowserHandler> self,
             std::string callback_id, MemorySqlQueryResult result) {
            if (!self) {
              return;
            }
            self->ResolveJavascriptCallback(base::Value(callback_id),
                                            MemorySqlQueryResultToDict(result));
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

// ---- DaoDreamRunnerHandler ----

DaoDreamRunnerHandler::DaoDreamRunnerHandler() = default;

DaoDreamRunnerHandler::~DaoDreamRunnerHandler() {
  if (DaoDreamService* service = GetDreamService()) {
    service->ClearRunner(this);
  }
}

DaoDreamService* DaoDreamRunnerHandler::GetDreamService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return DaoDreamServiceFactory::GetForProfile(profile);
}

void DaoDreamRunnerHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "dreamComplete",
      base::BindRepeating(&DaoDreamRunnerHandler::HandleDreamComplete,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "dreamFailed",
      base::BindRepeating(&DaoDreamRunnerHandler::HandleDreamFailed,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDreamExcludedDomains",
      base::BindRepeating(
          &DaoDreamRunnerHandler::HandleGetDreamExcludedDomains,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "addDreamExcludedDomain",
      base::BindRepeating(&DaoDreamRunnerHandler::HandleAddDreamExcludedDomain,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "removeDreamExcludedDomain",
      base::BindRepeating(
          &DaoDreamRunnerHandler::HandleRemoveDreamExcludedDomain,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startManualDream",
      base::BindRepeating(&DaoDreamRunnerHandler::HandleStartManualDream,
                          base::Unretained(this)));
}

void DaoDreamRunnerHandler::OnJavascriptAllowed() {
  if (DaoDreamService* service = GetDreamService()) {
    service->SetRunner(this);
  }
}

void DaoDreamRunnerHandler::OnJavascriptDisallowed() {
  if (DaoDreamService* service = GetDreamService()) {
    service->ClearRunner(this);
  }
}

void DaoDreamRunnerHandler::RunDream(const std::string& dream_date,
                                     const base::DictValue& material) {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::DictValue payload;
  payload.Set("dreamDate", dream_date);
  payload.Set("material", material.Clone());
  payload.Set("debug",
              profile->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug));
  FireWebUIListener("dream-run", payload);
}

void DaoDreamRunnerHandler::HandleDreamComplete(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  if (DaoDreamService* service = GetDreamService()) {
    service->OnDreamResult(args[0].GetString(), args[1].GetDict().Clone());
  }
}

void DaoDreamRunnerHandler::HandleDreamFailed(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  if (DaoDreamService* service = GetDreamService()) {
    service->OnDreamFailed(args[0].GetString(), args[1].GetString());
  }
}

void DaoDreamRunnerHandler::HandleGetDreamExcludedDomains(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            DreamExcludedDomainsToList(profile));
}

void DaoDreamRunnerHandler::HandleAddDreamExcludedDomain(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string normalized =
      NormalizeDreamExcludedDomain(ReadDomainArgument(args));
  if (normalized.empty()) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("invalid domain"));
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  std::set<std::string> domains = LoadDreamExcludedDomains(profile);
  domains.insert(normalized);
  SaveDreamExcludedDomains(profile, domains);

  base::DictValue response;
  response.Set("domain", normalized);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoDreamRunnerHandler::HandleRemoveDreamExcludedDomain(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string normalized =
      NormalizeDreamExcludedDomain(ReadDomainArgument(args));
  if (normalized.empty()) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("invalid domain"));
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  std::set<std::string> domains = LoadDreamExcludedDomains(profile);
  domains.erase(normalized);
  SaveDreamExcludedDomains(profile, domains);
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
}

void DaoDreamRunnerHandler::HandleStartManualDream(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  DaoDreamService* service = GetDreamService();
  if (!service) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("dream service unavailable"));
    return;
  }
  service->SetRunner(this);
  auto callback = base::BindOnce(
      [](base::WeakPtr<DaoDreamRunnerHandler> self, std::string callback_id,
         bool success, const std::string& error) {
        if (!self) {
          return;
        }
        if (success) {
          self->ResolveJavascriptCallback(base::Value(callback_id),
                                          base::Value(true));
        } else {
          self->RejectJavascriptCallback(base::Value(callback_id),
                                         base::Value(
                                             error.empty()
                                                 ? "dream run failed"
                                                 : error));
        }
      },
      weak_factory_.GetWeakPtr(), callback_id);

  std::string dream_date;
  if (args.size() >= 2 && args[1].is_dict()) {
    if (const std::string* date = args[1].GetDict().FindString("date")) {
      dream_date = *date;
    }
  }
  if (!dream_date.empty()) {
    service->StartManualDreamForDate(dream_date, std::move(callback));
  } else {
    service->StartManualDream(std::move(callback));
  }
}

// ---- DaoAgentDreamHandler ----

DaoAgentDreamHandler::DaoAgentDreamHandler() = default;

DaoAgentDreamHandler::~DaoAgentDreamHandler() = default;

void DaoAgentDreamHandler::RegisterMessages() {
  DaoDreamRunnerHandler::RegisterMessages();
  web_ui()->RegisterMessageCallback(
      "getDreamEnabled",
      base::BindRepeating(&DaoAgentDreamHandler::HandleGetDreamEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDreamEnabled",
      base::BindRepeating(&DaoAgentDreamHandler::HandleSetDreamEnabled,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getDreamDebug",
      base::BindRepeating(&DaoAgentDreamHandler::HandleGetDreamDebug,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDreamDebug",
      base::BindRepeating(&DaoAgentDreamHandler::HandleSetDreamDebug,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getUnviewedDreamReport",
      base::BindRepeating(
          &DaoAgentDreamHandler::HandleGetUnviewedDreamReport,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "markDreamReportViewed",
      base::BindRepeating(
          &DaoAgentDreamHandler::HandleMarkDreamReportViewed,
          base::Unretained(this)));
}

void DaoAgentDreamHandler::HandleGetDreamEnabled(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  ResolveJavascriptCallback(
      base::Value(args[0].GetString()),
      base::Value(profile->GetPrefs()->GetBoolean(prefs::kDaoDreamEnabled)));
}

void DaoAgentDreamHandler::HandleSetDreamEnabled(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, args[1].GetBool());
  // Instantiate the service (and register us as runner) when enabling.
  if (args[1].GetBool()) {
    if (DaoDreamService* service = GetDreamService()) {
      service->SetRunner(this);
    }
  }
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
}

void DaoAgentDreamHandler::HandleGetDreamDebug(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  ResolveJavascriptCallback(
      base::Value(args[0].GetString()),
      base::Value(profile->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug)));
}

void DaoAgentDreamHandler::HandleSetDreamDebug(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_bool()) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamDebug, args[1].GetBool());
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
}

void DaoAgentDreamHandler::HandleGetUnviewedDreamReport(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value());
    return;
  }
  memory->GetLatestUnviewedDreamReport(base::BindOnce(
      [](base::WeakPtr<DaoAgentDreamHandler> self, std::string callback_id,
         std::optional<DreamReport> report) {
        if (!self) {
          return;
        }
        if (!report) {
          self->ResolveJavascriptCallback(base::Value(callback_id),
                                          base::Value());
          return;
        }
        base::DictValue dict;
        dict.Set("id", static_cast<double>(report->id));
        dict.Set("dreamDate", report->dream_date);
        dict.Set("reportMarkdown", report->report_markdown);
        dict.Set("habitCandidates", report->habit_candidates);
        dict.Set("materialStats", report->material_stats);
        dict.Set("debugMaterialJson", report->debug_material_json);
        dict.Set("triggerKind", report->trigger_kind);
        self->ResolveJavascriptCallback(base::Value(callback_id), dict);
      },
      weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentDreamHandler::HandleMarkDreamReportViewed(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() ||
      (!args[1].is_double() && !args[1].is_int())) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (memory) {
    memory->MarkDreamReportViewed(static_cast<int64_t>(args[1].GetDouble()),
                                  base::DoNothing());
  }
  ResolveJavascriptCallback(base::Value(args[0].GetString()),
                            base::Value(true));
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
      "setSkillDisabled",
      base::BindRepeating(&DaoAgentSkillHandler::HandleSetSkillDisabled,
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
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  auto* service = GetSkillService();
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::Value(base::ListValue()));
    return;
  }

  service->GetSkillRegistry(base::BindOnce(
      [](base::WeakPtr<DaoAgentSkillHandler> handler, std::string cb_id,
         std::vector<SkillRegistryEntry> entries) {
        if (!handler) {
          return;
        }
        base::ListValue list;
        for (const auto& entry : entries) {
          base::DictValue dict;
          dict.Set("id", entry.id);
          dict.Set("name", entry.name);
          dict.Set("description", entry.description);
          dict.Set("source", entry.source);
          dict.Set("requiresPageContent", entry.requires_page_content);
          dict.Set("disabled", entry.disabled);

          base::ListValue hosts_list;
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
    const base::ListValue& args) {
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
                              base::Value(base::DictValue()));
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
                  base::Value(cb_id), base::Value(base::DictValue()));
              return;
            }
            base::DictValue result;
            result.Set("instructions", content->instructions);

            base::DictValue metadata;
            metadata.Set("id", content->metadata.id);
            metadata.Set("name", content->metadata.name);
            metadata.Set("description", content->metadata.description);
            metadata.Set("source", content->metadata.source);
            metadata.Set("requiresPageContent",
                         content->metadata.requires_page_content);
            metadata.Set("disabled", content->metadata.disabled);

            base::ListValue hosts_list;
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
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
    const base::ListValue& args) {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::FilePath skills_path =
      profile->GetPath().AppendASCII("DaoAgentSkills");
  platform_util::OpenItem(profile, skills_path, platform_util::OPEN_FOLDER,
                          platform_util::OpenOperationCallback());
}

void DaoAgentSkillHandler::HandleSetSkillDisabled(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id =
      args[1].is_string() ? args[1].GetString() : "";
  const bool disabled = args[2].is_bool() ? args[2].GetBool() : false;

  auto* service = GetSkillService();
  if (!service || skill_id.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->SetSkillDisabled(
      skill_id, disabled,
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

void DaoAgentSkillHandler::HandleOpenSkillManager(
    const base::ListValue& args) {
  Browser* browser = chrome::FindLastActive();
  if (!browser) {
    return;
  }
  GURL skills_url(std::string(content::kChromeUIScheme) + "://skills");
  NavigateParams params(browser, skills_url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

// ---- DaoAgentWorkspaceHandler ----

DaoAgentWorkspaceHandler::DaoAgentWorkspaceHandler() = default;
DaoAgentWorkspaceHandler::~DaoAgentWorkspaceHandler() = default;

DaoAgentWorkspaceService*
DaoAgentWorkspaceHandler::GetWorkspaceService() {
  Profile* profile = Profile::FromWebUI(web_ui());
  return DaoAgentWorkspaceServiceFactory::GetForProfile(profile);
}

void DaoAgentWorkspaceHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "workspaceRead",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceRead,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceWrite",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceWrite,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceEdit",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceEdit,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceApplyPatch",
      base::BindRepeating(
          &DaoAgentWorkspaceHandler::HandleWorkspaceApplyPatch,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceList",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceOpenFolder",
      base::BindRepeating(
          &DaoAgentWorkspaceHandler::HandleWorkspaceOpenFolder,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceGetRecentActivity",
      base::BindRepeating(
          &DaoAgentWorkspaceHandler::HandleWorkspaceGetRecentActivity,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceGetInfo",
      base::BindRepeating(
          &DaoAgentWorkspaceHandler::HandleWorkspaceGetInfo,
          base::Unretained(this)));
}

namespace {

const char* WorkspaceErrorCode(WorkspaceError e) {
  switch (e) {
    case WorkspaceError::kInvalidPath:
      return "invalid_path";
    case WorkspaceError::kNotFound:
      return "not_found";
    case WorkspaceError::kAlreadyExists:
      return "already_exists";
    case WorkspaceError::kQuotaExceeded:
      return "quota_exceeded";
    case WorkspaceError::kBinaryRejected:
      return "binary_rejected";
    case WorkspaceError::kPatchParseError:
      return "patch_parse_error";
    case WorkspaceError::kPatchContextMismatch:
      return "patch_context_mismatch";
    case WorkspaceError::kEditNotUnique:
      return "edit_not_unique";
    case WorkspaceError::kIoError:
      return "io_error";
    case WorkspaceError::kOk:
      return "io_error";
  }
  return "io_error";
}

}  // namespace

void DaoAgentWorkspaceHandler::ReplyOk(const std::string& cb_id,
                                       base::DictValue body) {
  body.Set("ok", true);
  ResolveJavascriptCallback(base::Value(cb_id), base::Value(std::move(body)));
}

void DaoAgentWorkspaceHandler::ReplyError(const std::string& cb_id,
                                          WorkspaceError err) {
  base::DictValue body;
  body.Set("ok", false);
  body.Set("code", WorkspaceErrorCode(err));
  ResolveJavascriptCallback(base::Value(cb_id), base::Value(std::move(body)));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceRead(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  const base::DictValue& dict = args[1].GetDict();
  const std::string* path = dict.FindString("path");
  if (!path) {
    ReplyError(cb_id, WorkspaceError::kInvalidPath);
    return;
  }
  int offset = dict.FindInt("offset").value_or(0);
  int limit = dict.FindInt("limit").value_or(500);

  GetWorkspaceService()->Read(
      *path, offset, limit,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
             base::expected<ReadResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ReplyError(cb_id, result.error());
              return;
            }
            base::DictValue body;
            body.Set("content", result->content);
            body.Set("total_lines", result->total_lines);
            body.Set("returned_lines", result->returned_lines);
            body.Set("truncated", result->truncated);
            self->ReplyOk(cb_id, std::move(body));
          },
          weak_factory_.GetWeakPtr(), cb_id));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceWrite(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  const base::DictValue& dict = args[1].GetDict();
  const std::string* path = dict.FindString("path");
  const std::string* content = dict.FindString("content");
  if (!path || !content) {
    ReplyError(cb_id, WorkspaceError::kInvalidPath);
    return;
  }
  GetWorkspaceService()->Write(
      *path, *content,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
             base::expected<WriteResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ReplyError(cb_id, result.error());
              return;
            }
            base::DictValue body;
            body.Set("bytes_written",
                     static_cast<int>(result->bytes_written));
            body.Set("created", result->created);
            self->ReplyOk(cb_id, std::move(body));
          },
          weak_factory_.GetWeakPtr(), cb_id));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceEdit(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  const base::DictValue& dict = args[1].GetDict();
  const std::string* path = dict.FindString("path");
  const std::string* old_str = dict.FindString("old_str");
  const std::string* new_str = dict.FindString("new_str");
  if (!path || !old_str || !new_str) {
    ReplyError(cb_id, WorkspaceError::kInvalidPath);
    return;
  }
  GetWorkspaceService()->Edit(
      *path, *old_str, *new_str,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
             base::expected<WriteResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ReplyError(cb_id, result.error());
              return;
            }
            base::DictValue body;
            body.Set("bytes_written",
                     static_cast<int>(result->bytes_written));
            self->ReplyOk(cb_id, std::move(body));
          },
          weak_factory_.GetWeakPtr(), cb_id));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceApplyPatch(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  const std::string* patch = args[1].GetDict().FindString("patch");
  if (!patch) {
    ReplyError(cb_id, WorkspaceError::kPatchParseError);
    return;
  }
  GetWorkspaceService()->ApplyPatch(
      *patch,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
             base::expected<PatchResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ReplyError(cb_id, result.error());
              return;
            }
            base::DictValue body;
            base::ListValue added;
            base::ListValue updated;
            base::ListValue deleted;
            base::ListValue moved;
            for (const auto& p : result->added) {
              added.Append(p);
            }
            for (const auto& p : result->updated) {
              updated.Append(p);
            }
            for (const auto& p : result->deleted) {
              deleted.Append(p);
            }
            for (const auto& [from, to] : result->moved) {
              base::DictValue m;
              m.Set("from", from);
              m.Set("to", to);
              moved.Append(std::move(m));
            }
            body.Set("added", std::move(added));
            body.Set("updated", std::move(updated));
            body.Set("deleted", std::move(deleted));
            body.Set("moved", std::move(moved));
            self->ReplyOk(cb_id, std::move(body));
          },
          weak_factory_.GetWeakPtr(), cb_id));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceList(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  const base::DictValue& dict = args[1].GetDict();
  // `path` is optional; absent / empty means the workspace root.
  std::string path;
  if (const std::string* p = dict.FindString("path")) {
    path = *p;
  }
  bool recursive = dict.FindBool("recursive").value_or(false);

  GetWorkspaceService()->List(
      path, recursive,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
             base::expected<ListResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ReplyError(cb_id, result.error());
              return;
            }
            base::DictValue body;
            base::ListValue entries;
            for (const auto& e : result->entries) {
              base::DictValue item;
              item.Set("path", e.path);
              item.Set("is_dir", e.is_dir);
              // size_bytes can exceed int range; serialize as double so the
              // WebUI receives it as a JS number.
              item.Set("size_bytes", static_cast<double>(e.size_bytes));
              item.Set("mtime", e.mtime);
              entries.Append(std::move(item));
            }
            body.Set("entries", std::move(entries));
            body.Set("total", static_cast<int>(result->total));
            body.Set("truncated", result->truncated);
            self->ReplyOk(cb_id, std::move(body));
          },
          weak_factory_.GetWeakPtr(), cb_id));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceOpenFolder(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  GetWorkspaceService()->OpenInFileManager();
  ReplyOk(cb_id, base::DictValue());
}

void DaoAgentWorkspaceHandler::HandleWorkspaceGetRecentActivity(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  GetWorkspaceService()->GetRecentAuditAsync(base::BindOnce(
      [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
         std::vector<AuditEntry> entries) {
        if (!self) {
          return;
        }
        base::DictValue body;
        base::ListValue list;
        for (const auto& e : entries) {
          base::DictValue d;
          d.Set("ts", e.ts);
          d.Set("op", e.op);
          d.Set("path", e.path);
          list.Append(std::move(d));
        }
        body.Set("entries", std::move(list));
        self->ReplyOk(cb_id, std::move(body));
      },
      weak_factory_.GetWeakPtr(), cb_id));
}

void DaoAgentWorkspaceHandler::HandleWorkspaceGetInfo(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  std::string cb_id = args[0].GetString();
  GetWorkspaceService()->GetUsageInfo(base::BindOnce(
      [](base::WeakPtr<DaoAgentWorkspaceHandler> self, std::string cb_id,
         DaoAgentWorkspaceService::UsageSnapshot snap) {
        if (!self) {
          return;
        }
        base::DictValue body;
        body.Set("root", snap.root.AsUTF8Unsafe());
        body.Set("used_bytes", static_cast<double>(snap.used_bytes));
        body.Set("cap_bytes", static_cast<double>(snap.cap_bytes));
        body.Set("file_count", static_cast<int>(snap.file_count));
        body.Set("file_count_cap", static_cast<int>(snap.file_count_cap));
        self->ReplyOk(cb_id, std::move(body));
      },
      weak_factory_.GetWeakPtr(), cb_id));
}

// ---- DaoIndexUIConfig ----

DaoIndexUIConfig::DaoIndexUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "index") {}

std::unique_ptr<content::WebUIController>
DaoIndexUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                        const GURL& url) {
  return std::make_unique<DaoIndexUI>(web_ui);
}

// ---- DaoIndexUI ----

DaoIndexUI::DaoIndexUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "index");

  source->AddResourcePaths(kDaoAgentResources);
  source->SetDefaultResource(IDR_DAO_AGENT_INDEX_HTML);

  source->AddString("dao_app_locale",
                    g_browser_process->GetApplicationLocale());
  source->UseStringsJs();

  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop lit-html;");
}

DaoIndexUI::~DaoIndexUI() = default;

// ---- DaoMemoryUIConfig ----

DaoMemoryUIConfig::DaoMemoryUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "memory") {}

std::unique_ptr<content::WebUIController>
DaoMemoryUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                         const GURL& url) {
  return std::make_unique<DaoMemoryUI>(web_ui);
}

// ---- DaoMemoryUI ----

DaoMemoryUI::DaoMemoryUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "memory");

  source->AddResourcePaths(kDaoAgentResources);
  source->SetDefaultResource(IDR_DAO_AGENT_MEMORY_HTML);

  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop lit-html;");

  web_ui->AddMessageHandler(std::make_unique<DaoMemoryBrowserHandler>());
}

DaoMemoryUI::~DaoMemoryUI() = default;

// ---- DaoDreamUIConfig ----

DaoDreamUIConfig::DaoDreamUIConfig()
    : WebUIConfig(content::kChromeUIScheme, "dream") {}

std::unique_ptr<content::WebUIController>
DaoDreamUIConfig::CreateWebUIController(content::WebUI* web_ui,
                                        const GURL& url) {
  return std::make_unique<DaoDreamUI>(web_ui);
}

// ---- DaoDreamUI ----

DaoDreamUI::DaoDreamUI(content::WebUI* web_ui)
    : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "dream");

  source->AddResourcePaths(kDaoAgentResources);
  source->SetDefaultResource(IDR_DAO_AGENT_DREAM_HTML);

  source->AddString("dao_app_locale",
                    g_browser_process->GetApplicationLocale());
  source->UseStringsJs();

  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop lit-html;");

  web_ui->AddMessageHandler(std::make_unique<DaoDreamReportHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoDreamRunnerHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentMemoryHandler>());
}

DaoDreamUI::~DaoDreamUI() = default;

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

  // Expose the active application locale to the WebUI via strings.m.js so the
  // Dao i18n module (resources/agent/i18n/i18n.ts) can pick the matching
  // locale dictionary. Keeping the WebUI in step with the C++ pak that grit
  // selected for the browser process — without it, `navigator.language` and
  // the chrome locale can drift apart.
  source->AddString("dao_app_locale",
                    g_browser_process->GetApplicationLocale());
  source->UseStringsJs();

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
  web_ui->AddMessageHandler(std::make_unique<DaoAgentDreamHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentSkillHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentWorkspaceHandler>());
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

  // Expose the application locale; see DaoAgentUI for the rationale.
  source->AddString("dao_app_locale",
                    g_browser_process->GetApplicationLocale());
  source->UseStringsJs();

  // Allow Lit HTML rendering.
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop lit-html;");

  // Register skill message handler only.
  web_ui->AddMessageHandler(std::make_unique<DaoAgentSkillHandler>());
  web_ui->AddMessageHandler(std::make_unique<DaoAgentWorkspaceHandler>());
}

DaoSkillsUI::~DaoSkillsUI() = default;

DaoAgentUIHandler::NativeFetchRequest::NativeFetchRequest() = default;
DaoAgentUIHandler::NativeFetchRequest::NativeFetchRequest(
    NativeFetchRequest&&) noexcept = default;
DaoAgentUIHandler::NativeFetchRequest&
DaoAgentUIHandler::NativeFetchRequest::operator=(
    NativeFetchRequest&&) noexcept = default;
DaoAgentUIHandler::NativeFetchRequest::~NativeFetchRequest() = default;

DaoAgentUIHandler::WorkspaceDownloadRequest::WorkspaceDownloadRequest() =
    default;
DaoAgentUIHandler::WorkspaceDownloadRequest::WorkspaceDownloadRequest(
    WorkspaceDownloadRequest&&) noexcept = default;
DaoAgentUIHandler::WorkspaceDownloadRequest&
DaoAgentUIHandler::WorkspaceDownloadRequest::operator=(
    WorkspaceDownloadRequest&&) noexcept = default;
DaoAgentUIHandler::WorkspaceDownloadRequest::~WorkspaceDownloadRequest() =
    default;

void DaoAgentUIHandler::HandleNativeFetch(const base::ListValue& args) {
  AllowJavascript();

  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const auto& params = args[1].GetDict();

  auto reply_error = [&](const std::string& error) {
    base::DictValue response;
    response.Set("ok", false);
    response.Set("status", 0);
    response.Set("finalUrl", "");
    response.Set("body", "");
    response.Set("error", error);
    ResolveJavascriptCallback(base::Value(callback_id), response);
  };

  const std::string* url_p = params.FindString("url");
  if (!url_p || url_p->empty()) {
    reply_error("Missing url");
    return;
  }

  GURL gurl(*url_p);
  if (!gurl.is_valid() ||
      (gurl.scheme() != "https" && gurl.scheme() != "http")) {
    reply_error("Invalid URL scheme");
    return;
  }

  const std::string* method_p = params.FindString("method");
  std::string method = method_p ? *method_p : "GET";

  // Decide credentials mode. Default: kOmit (existing behavior — used by
  // DDG search and Jina Reader, both third-party endpoints that must
  // never see the user's cookies). Opt-in: when JS passes
  // credentials="include_if_same_origin_active_tab", we attach cookies
  // ONLY if (a) the method is GET and (b) the target URL is same-origin
  // with the currently active tab. The GET-only restriction is a
  // defense-in-depth guarantee: cookie-bearing POST/PUT/DELETE on behalf
  // of the agent would let a model trivially perform CSRF-style actions
  // on the user's session. Reads are the only safe surface here.
  network::mojom::CredentialsMode credentials_mode =
      network::mojom::CredentialsMode::kOmit;
  url::Origin active_tab_origin;
  if (const std::string* cred_p = params.FindString("credentials");
      cred_p && *cred_p == "include_if_same_origin_active_tab" &&
      method == "GET") {
    content::WebContents* active_contents = ResolveTargetContents();
    if (active_contents) {
      const GURL active_url = active_contents->GetLastCommittedURL();
      const url::Origin candidate = url::Origin::Create(active_url);
      const url::Origin target_origin = url::Origin::Create(gurl);
      if (!candidate.opaque() && !target_origin.opaque() &&
          candidate.IsSameOriginWith(target_origin)) {
        credentials_mode = network::mojom::CredentialsMode::kInclude;
        active_tab_origin = candidate;
      }
    }
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = gurl;
  request->method = method;
  request->credentials_mode = credentials_mode;
  request->load_flags = net::LOAD_BYPASS_CACHE;
  if (credentials_mode == network::mojom::CredentialsMode::kInclude) {
    // Pretend the request is initiated from the active tab so SameSite
    // cookie rules treat it as a same-site fetch and the user's session
    // cookies are actually attached.
    request->site_for_cookies = net::SiteForCookies::FromOrigin(
        active_tab_origin);
    request->request_initiator = active_tab_origin;
  }

  // Capture Content-Type early so we can pass it to AttachStringForUpload.
  std::string content_type = "application/octet-stream";
  if (const auto* headers_dict = params.FindDict("headers")) {
    for (const auto kv : *headers_dict) {
      if (!kv.second.is_string()) {
        continue;
      }
      const std::string& name = kv.first;
      const std::string& value = kv.second.GetString();
      // Content-Type goes through SimpleURLLoader::AttachStringForUpload
      // for POST bodies; setting it via headers can confuse the loader.
      if (base::EqualsCaseInsensitiveASCII(name, "Content-Type")) {
        content_type = value;
        continue;
      }
      request->headers.SetHeader(name, value);
    }
  }

  static const net::NetworkTrafficAnnotationTag annotation =
      net::DefineNetworkTrafficAnnotation("dao_agent_web_search", R"(
        semantics {
          sender: "Dao Agent Web Search"
          description:
            "Fetches search results from DuckDuckGo HTML and article "
            "content from Jina Reader on behalf of the agent's "
            "web_search and fetch_url tools. Also used by fetch_url "
            "to fetch a URL same-origin with the active tab so the "
            "agent can read authenticated content the user is "
            "currently logged into."
          trigger:
            "User asks the agent to search the web or read a URL."
          data:
            "The user's search query (sent to DuckDuckGo), the URL "
            "the agent wants to read (sent to Jina Reader), or — only "
            "when the target URL is same-origin with the active tab — "
            "the user's cookies for that origin so the request can "
            "see authenticated content."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting: "Disable the Web tools group in agent settings."
          policy_exception_justification:
            "User-initiated agent action, like a user typing the URL "
            "into the address bar themselves. Cookies are attached "
            "only when the target URL is same-origin with the active "
            "tab; cross-origin and third-party endpoints (DuckDuckGo, "
            "Jina Reader) are always sent with credentials omitted."
        })");

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), annotation);
  loader->SetTimeoutDuration(base::Seconds(30));

  if (method != "GET" && method != "HEAD") {
    const std::string* body_p = params.FindString("body");
    if (body_p && !body_p->empty()) {
      loader->AttachStringForUpload(*body_p, content_type);
    }
  }

  network::SimpleURLLoader* loader_ptr = loader.get();

  network::mojom::URLLoaderFactory* factory =
      Profile::FromWebUI(web_ui())
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get();

  NativeFetchRequest entry;
  entry.loader = std::move(loader);
  entry.callback_id = callback_id;
  native_fetch_inflight_[loader_ptr] = std::move(entry);

  loader_ptr->DownloadToString(
      factory,
      base::BindOnce(&DaoAgentUIHandler::OnNativeFetchComplete,
                     weak_factory_.GetWeakPtr(), loader_ptr),
      /*max_body_size=*/5 * 1024 * 1024);
}

void DaoAgentUIHandler::OnNativeFetchComplete(
    network::SimpleURLLoader* loader_ptr,
    std::optional<std::string> body) {
  auto it = native_fetch_inflight_.find(loader_ptr);
  if (it == native_fetch_inflight_.end()) {
    return;
  }
  const std::string callback_id = it->second.callback_id;
  network::SimpleURLLoader* loader = it->second.loader.get();

  base::DictValue response;
  int status_code = 0;
  std::string final_url;
  const network::mojom::URLResponseHead* response_info = loader->ResponseInfo();
  if (response_info && response_info->headers) {
    status_code = response_info->headers->response_code();
  }
  if (loader->GetFinalURL().is_valid()) {
    final_url = loader->GetFinalURL().spec();
  }

  response.Set("status", status_code);
  response.Set("finalUrl", final_url);
  if (body.has_value()) {
    response.Set("body", *body);
    response.Set("ok", status_code >= 200 && status_code < 300);
    if (status_code < 200 || status_code >= 300) {
      response.Set("error",
                   "http " + base::NumberToString(status_code));
    }
  } else {
    response.Set("body", "");
    response.Set("ok", false);
    int net_error = loader->NetError();
    response.Set("error",
                 "net error " + base::NumberToString(net_error));
  }

  // Erase BEFORE resolve so the loader is freed promptly.
  native_fetch_inflight_.erase(it);

  ResolveJavascriptCallback(base::Value(callback_id), response);
}

namespace {

// Reply helpers reused by HandleWorkspaceDownload. The workspace tool
// family uses the {ok:false, code:"..."} wire format; HandleNativeFetch
// uses a different shape, so we open-code the workspace shape here to
// keep both flavors of error consistent for the TS dispatcher.
base::DictValue DownloadErrorDict(const std::string& code) {
  base::DictValue body;
  body.Set("ok", false);
  body.Set("code", code);
  return body;
}

const char* WorkspaceErrorCodeForDownload(WorkspaceError e) {
  switch (e) {
    case WorkspaceError::kInvalidPath:
      return "invalid_path";
    case WorkspaceError::kNotFound:
      return "not_found";
    case WorkspaceError::kAlreadyExists:
      return "already_exists";
    case WorkspaceError::kQuotaExceeded:
      return "quota_exceeded";
    case WorkspaceError::kBinaryRejected:
      return "binary_rejected";
    case WorkspaceError::kPatchParseError:
      return "patch_parse_error";
    case WorkspaceError::kPatchContextMismatch:
      return "patch_context_mismatch";
    case WorkspaceError::kEditNotUnique:
      return "edit_not_unique";
    case WorkspaceError::kIoError:
    case WorkspaceError::kOk:
      return "io_error";
  }
  return "io_error";
}

}  // namespace

void DaoAgentUIHandler::WriteDownloadedAndReply(
    const std::string& callback_id,
    const std::string& path,
    const std::string& source_url,
    std::string body,
    bool truncated) {
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentWorkspaceService* service =
      DaoAgentWorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("io_error"));
    return;
  }
  service->Write(
      path, body,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> self,
             std::string cb_id, std::string path, std::string source_url,
             bool truncated,
             base::expected<WriteResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ResolveJavascriptCallback(
                  base::Value(cb_id),
                  DownloadErrorDict(
                      WorkspaceErrorCodeForDownload(result.error())));
              return;
            }
            base::DictValue body;
            body.Set("ok", true);
            body.Set("path", path);
            body.Set("bytes_written",
                     static_cast<int>(result->bytes_written));
            body.Set("created", result->created);
            body.Set("source_url", source_url);
            body.Set("truncated", truncated);
            self->ResolveJavascriptCallback(base::Value(cb_id), body);
          },
          weak_factory_.GetWeakPtr(), callback_id, path, source_url,
          truncated));
}

void DaoAgentUIHandler::HandleWorkspaceDownload(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const base::DictValue& params = args[1].GetDict();

  const std::string* path_p = params.FindString("path");
  if (!path_p || path_p->empty()) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("invalid_path"));
    return;
  }
  const std::string path = *path_p;

  // Determine source. Defaults: if "url" is provided we treat it as a
  // URL fetch; otherwise we capture the active tab.
  const std::string* explicit_source = params.FindString("source");
  const std::string* url_p = params.FindString("url");
  std::string source =
      explicit_source ? *explicit_source
                      : (url_p && !url_p->empty() ? "url" : "page");

  if (source == "page" || source == "active_tab") {
    content::WebContents* contents = EnsureAttached();
    if (!contents) {
      ResolveJavascriptCallback(base::Value(callback_id),
                                DownloadErrorDict("io_error"));
      return;
    }
    const std::string page_url = contents->GetURL().spec();

    base::DictValue cdp_params;
    cdp_params.Set("expression", "document.documentElement.outerHTML");
    cdp_params.Set("returnByValue", true);
    devtools_client_->SendCommand(
        "Runtime.evaluate", std::move(cdp_params),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentUIHandler> self,
               std::string cb_id, std::string path, std::string source_url,
               base::Value result) {
              if (!self) {
                return;
              }
              if (!result.is_dict()) {
                self->ResolveJavascriptCallback(
                    base::Value(cb_id), DownloadErrorDict("io_error"));
                return;
              }
              auto* value =
                  result.GetDict().FindByDottedPath("result.value");
              if (!value || !value->is_string()) {
                self->ResolveJavascriptCallback(
                    base::Value(cb_id), DownloadErrorDict("io_error"));
                return;
              }
              // No TruncateText: the body never goes through LLM
              // context — it goes straight to disk via the workspace
              // service. The workspace quota (per-file + total) is the
              // only ceiling we need here.
              std::string html = value->GetString();
              self->WriteDownloadedAndReply(
                  cb_id, path, source_url, std::move(html),
                  /*truncated=*/false);
            },
            weak_factory_.GetWeakPtr(), callback_id, path, page_url));
    return;
  }

  if (source != "url") {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("invalid_path"));
    return;
  }

  if (!url_p || url_p->empty()) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("invalid_path"));
    return;
  }
  GURL gurl(*url_p);
  if (!gurl.is_valid() ||
      (gurl.scheme() != "https" && gurl.scheme() != "http")) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("invalid_path"));
    return;
  }

  // Reject non-text extensions before doing any network IO. We will
  // also re-check after the bytes land (first-8KB NUL probe), but the
  // extension check is cheap and saves an entire round-trip for the
  // common ".png"/".zip" case.
  if (!IsTextExtensionAllowed(base::FilePath::FromUTF8Unsafe(path))) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("binary_rejected"));
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentWorkspaceService* service =
      DaoAgentWorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("io_error"));
    return;
  }

  auto request = std::make_unique<network::ResourceRequest>();
  request->url = gurl;
  request->method = "GET";
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->load_flags = net::LOAD_BYPASS_CACHE;

  if (const auto* headers_dict = params.FindDict("headers")) {
    for (const auto kv : *headers_dict) {
      if (!kv.second.is_string()) {
        continue;
      }
      // Content-Type goes through AttachStringForUpload for bodies;
      // download is GET-only so we just drop it.
      if (base::EqualsCaseInsensitiveASCII(kv.first, "Content-Type")) {
        continue;
      }
      request->headers.SetHeader(kv.first, kv.second.GetString());
    }
  }

  static const net::NetworkTrafficAnnotationTag annotation =
      net::DefineNetworkTrafficAnnotation("dao_agent_workspace_download", R"(
        semantics {
          sender: "Dao Agent Workspace Download"
          description:
            "Downloads a URL the agent picked into the user's agent "
            "workspace sandbox. Bypasses LLM-context echo so large "
            "documents are stored byte-for-byte instead of being "
            "re-typed by the model."
          trigger:
            "User asks the agent to save the active page or an "
            "arbitrary URL into the workspace via the `download` tool."
          data:
            "The URL the agent wants to download. No user credentials "
            "or PII unless the user put them in the URL."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: NO
          setting: "Disable the Workspace tools group in agent settings."
          policy_exception_justification:
            "User-initiated agent action, equivalent to the user "
            "saving a page themselves."
        })");

  std::unique_ptr<network::SimpleURLLoader> loader =
      network::SimpleURLLoader::Create(std::move(request), annotation);
  loader->SetTimeoutDuration(base::Seconds(30));

  network::SimpleURLLoader* loader_ptr = loader.get();

  WorkspaceDownloadRequest entry;
  entry.loader = std::move(loader);
  entry.callback_id = callback_id;
  entry.workspace_path = path;
  entry.source_url = gurl.spec();
  workspace_download_inflight_[loader_ptr] = std::move(entry);

  // Reserve a staging path on the workspace's own filesystem, then kick
  // off DownloadToFile so bytes stream directly to disk (the whole
  // response never sits in the browser-process heap). The completion
  // callback hands the staged file to IngestStagedFile which validates
  // & atomically renames it into the workspace.
  service->AllocateStagingPath(
      base::BindOnce(&DaoAgentUIHandler::OnDownloadStagingAllocated,
                     weak_factory_.GetWeakPtr(), loader_ptr));
}

void DaoAgentUIHandler::OnDownloadStagingAllocated(
    network::SimpleURLLoader* loader_ptr,
    base::FilePath staging_path) {
  auto it = workspace_download_inflight_.find(loader_ptr);
  if (it == workspace_download_inflight_.end()) {
    return;
  }
  if (staging_path.empty()) {
    const std::string cb = it->second.callback_id;
    workspace_download_inflight_.erase(it);
    ResolveJavascriptCallback(base::Value(cb),
                              DownloadErrorDict("io_error"));
    return;
  }
  it->second.staging_path = staging_path;

  network::mojom::URLLoaderFactory* factory =
      Profile::FromWebUI(web_ui())
          ->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess()
          .get();

  // max_body_size matches the workspace per-file cap so the loader
  // aborts mid-stream once it would exceed the quota — saves filling
  // up disk on a runaway response.
  loader_ptr->DownloadToFile(
      factory,
      base::BindOnce(&DaoAgentUIHandler::OnWorkspaceDownloadFileComplete,
                     weak_factory_.GetWeakPtr(), loader_ptr),
      staging_path,
      /*max_body_size=*/
      static_cast<int64_t>(WorkspaceQuota::kPerFileMaxBytes));
}

void DaoAgentUIHandler::OnWorkspaceDownloadFileComplete(
    network::SimpleURLLoader* loader_ptr,
    base::FilePath returned_path) {
  auto it = workspace_download_inflight_.find(loader_ptr);
  if (it == workspace_download_inflight_.end()) {
    return;
  }
  const std::string callback_id = it->second.callback_id;
  const std::string workspace_path = it->second.workspace_path;
  std::string source_url = it->second.source_url;
  const base::FilePath staging_path = it->second.staging_path;

  network::SimpleURLLoader* loader = it->second.loader.get();
  int status_code = 0;
  const network::mojom::URLResponseHead* response_info = loader->ResponseInfo();
  if (response_info && response_info->headers) {
    status_code = response_info->headers->response_code();
  }
  if (loader->GetFinalURL().is_valid()) {
    source_url = loader->GetFinalURL().spec();
  }
  const bool http_ok = status_code >= 200 && status_code < 300;

  // Erase BEFORE the (possibly long-running) ingest so the loader is
  // freed promptly even if the ingest callback is delayed.
  workspace_download_inflight_.erase(it);

  // DownloadToFile signals failure with an empty path. On HTTP error
  // the loader still wrote whatever body it received (e.g. a 404 page),
  // which we don't want in the workspace either.
  if (returned_path.empty() || !http_ok) {
    if (!staging_path.empty()) {
      base::ThreadPool::PostTask(
          FROM_HERE,
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(base::IgnoreResult(&base::DeleteFile),
                         staging_path));
    }
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("io_error"));
    return;
  }

  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentWorkspaceService* service =
      DaoAgentWorkspaceServiceFactory::GetForProfile(profile);
  if (!service) {
    base::ThreadPool::PostTask(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(base::IgnoreResult(&base::DeleteFile), staging_path));
    ResolveJavascriptCallback(base::Value(callback_id),
                              DownloadErrorDict("io_error"));
    return;
  }

  const std::string audit_detail =
      "\"source\":\"url\",\"url\":\"" + source_url + "\"";
  service->IngestStagedFile(
      workspace_path, returned_path, audit_detail,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> self, std::string cb_id,
             std::string path, std::string source_url,
             base::expected<WriteResult, WorkspaceError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->ResolveJavascriptCallback(
                  base::Value(cb_id),
                  DownloadErrorDict(
                      WorkspaceErrorCodeForDownload(result.error())));
              return;
            }
            base::DictValue body;
            body.Set("ok", true);
            body.Set("path", path);
            body.Set("bytes_written",
                     static_cast<int>(result->bytes_written));
            body.Set("created", result->created);
            body.Set("source_url", source_url);
            body.Set("truncated", false);
            self->ResolveJavascriptCallback(base::Value(cb_id), body);
          },
          weak_factory_.GetWeakPtr(), callback_id, workspace_path,
          source_url));
}

}  // namespace dao
