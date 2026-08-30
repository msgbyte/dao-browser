// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_agent_ui.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/barrier_closure.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/ui/browser_window/public/profile_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/dao_agent_resources.h"
#include "chrome/grit/dao_agent_resources_map.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/pdf/browser/pdf_document_helper.h"
#include "components/prefs/pref_service.h"
#include "components/sessions/content/session_tab_helper.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/agent/dao_agent_lock_tab_helper.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_agent_settings_handler.h"
#include "dao/browser/agent/dao_agent_skill_service.h"
#include "dao/browser/agent/dao_agent_skill_service_factory.h"
#include "dao/browser/agent/dao_agent_workspace_service.h"
#include "dao/browser/agent/dao_agent_workspace_service_factory.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/agent/dao_dream_service_factory.h"
#include "dao/browser/agent/workspace/text_only_filter.h"
#include "dao/browser/agent/workspace/workspace_quota.h"
#include "dao/browser/automation/dao_agent_lease_manager.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_tool_executor.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/home/dao_home_agent_tools.h"
#include "dao/browser/home/dao_home_project_service.h"
#include "dao/browser/home/dao_home_project_service_factory.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_cursor_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/webui/dao_home_ui.h"
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
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/base/window_open_disposition.h"
#include "url/origin.h"

namespace dao {

namespace {

Browser* FindLastActiveBrowserForMigration() {
  BrowserWindowInterface* browser = chrome::FindLastActive();
  return browser ? browser->GetBrowserForMigrationOnly() : nullptr;
}

constexpr char kAgentExecutionContextKey[] = "__daoAgentExecutionContext";
constexpr char kLegacyUiOneShotContext[] = "legacy_ui_one_shot";

bool IsDaoHomeUrl(const GURL& url) {
  return url.SchemeIs(content::kChromeUIScheme) && url.host() == "home";
}

bool IsLegacyUiOneShotTool(std::string_view tool_name) {
  return tool_name == "get_page_info" || tool_name == "execute_script" ||
         tool_name == "capture_screenshot";
}

struct LegacyUiOneShotAuthorization {
  LegacyUiOneShotAuthorization(DaoAgentLease acquired_lease,
                               Browser* browser,
                               content::WebContents* target)
      : lease(std::move(acquired_lease)),
        session(browser,
                target,
                DaoBrowserAutomationSession::TargetPolicy::
                    kLegacyUiWithDaoHome) {}

  DaoAgentLease lease;
  DaoBrowserAutomationSession session;
};

using PageToolCompletion = base::OnceCallback<void(DaoBrowserToolResult)>;

PageToolCompletion HoldLegacyUiAuthorizationUntilComplete(
    std::unique_ptr<LegacyUiOneShotAuthorization> authorization,
    PageToolCompletion completion) {
  return base::BindOnce(
      [](std::unique_ptr<LegacyUiOneShotAuthorization> authorization,
         PageToolCompletion completion, DaoBrowserToolResult result) {
        // Release the pinned session and Profile lease before resolving the
        // WebUI promise, so a follow-up one-shot observes a clean lease.
        authorization.reset();
        std::move(completion).Run(std::move(result));
      },
      std::move(authorization), std::move(completion));
}

DaoToolError LocalizeAgentToolError(DaoToolError error) {
  if (error.code == DaoToolErrorCode::kAgentControlBusy) {
    error.message = l10n_util::GetStringUTF8(IDS_DAO_MCP_CONTROL_BUSY);
  }
  return error;
}

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
    dict.Set(
        "createdAt",
        static_cast<double>(report.created_at.InMillisecondsSinceUnixEpoch()));
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

std::optional<int64_t> ReadPositiveInt64(const base::Value* value) {
  if (!value) {
    return std::nullopt;
  }
  if (value->is_int()) {
    const int int_value = value->GetInt();
    return int_value > 0 ? std::optional<int64_t>(int_value) : std::nullopt;
  }
  if (!value->is_double() || !std::isfinite(value->GetDouble())) {
    return std::nullopt;
  }
  const double double_value = value->GetDouble();
  constexpr double kInt64MaxExclusive = 9223372036854775808.0;
  if (double_value < 1 || double_value >= kInt64MaxExclusive ||
      std::trunc(double_value) != double_value) {
    return std::nullopt;
  }
  return static_cast<int64_t>(double_value);
}

std::optional<int64_t> ReadInt64(const base::DictValue& dict,
                                 std::string_view key) {
  return ReadPositiveInt64(dict.Find(key));
}

std::optional<base::DictValue> WeeklyDreamReportToDict(
    const WeeklyDreamReport& report) {
  if (report.status != "completed") {
    return std::nullopt;
  }
  std::optional<base::Value> content =
      base::JSONReader::Read(report.content_json, base::JSON_PARSE_RFC);
  if (!content || !content->is_dict()) {
    return std::nullopt;
  }

  int source_count = 0;
  std::optional<base::Value> stats =
      base::JSONReader::Read(report.material_stats, base::JSON_PARSE_RFC);
  if (stats && stats->is_dict()) {
    source_count = stats->GetDict().FindInt("source_count").value_or(0);
  }

  base::DictValue dict;
  dict.Set("reportKind", "weekly");
  dict.Set("id", static_cast<double>(report.id));
  dict.Set("weekStart", report.week_start);
  dict.Set("weekEnd", report.week_end);
  dict.Set("content", std::move(*content).TakeDict());
  dict.Set("materialStats", report.material_stats);
  dict.Set("triggerKind", report.trigger_kind);
  dict.Set("sourceCount", source_count);
  dict.Set("createdAt", static_cast<double>(
                            report.created_at.InMillisecondsSinceUnixEpoch()));
  return dict;
}

base::ListValue WeeklyDreamReportsToList(
    const std::vector<WeeklyDreamReport>& reports) {
  base::ListValue list;
  for (const WeeklyDreamReport& report : reports) {
    std::optional<base::DictValue> serialized = WeeklyDreamReportToDict(report);
    if (serialized) {
      list.Append(std::move(*serialized));
    }
  }
  return list;
}

base::DictValue WeeklyDreamSourceToDict(const WeeklyDreamSource& source,
                                        bool available) {
  base::DictValue dict;
  dict.Set("refId", source.ref_id);
  dict.Set("sourceKind", source.source_kind);
  dict.Set("title", source.title);
  dict.Set("domain", source.domain);
  dict.Set("available", available);
  return dict;
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

base::DictValue MemorySqlQueryResultToDict(const MemorySqlQueryResult& result) {
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

}  // namespace

bool ShouldCountProactiveOutcomeAsDismissedForScenarioStats(
    const std::string& outcome) {
  return outcome == "not_now" || outcome == "not_helpful";
}

base::DictValue SerializeMemoryContextForAgentUi(const MemoryContext& context) {
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
        static_cast<double>(summary.first_timestamp.ToDeltaSinceWindowsEpoch()
                                .InMicroseconds()));
    relevant_summary.Set(
        "lastTimestamp",
        static_cast<double>(summary.last_timestamp.ToDeltaSinceWindowsEpoch()
                                .InMicroseconds()));
    relevant_summary.Set("primaryDomain", summary.primary_domain);
    result.Set("relevantSummary", std::move(relevant_summary));
  }

  return result;
}

// ---- DaoAgentUIHandler ----

DaoAgentUIHandler::DaoAgentUIHandler()
    : devtools_client_(std::make_unique<DaoDevToolsClient>()),
      browser_tool_executor_(
          std::make_unique<DaoBrowserToolExecutor>(devtools_client_.get(),
                                                   this)) {}

DaoAgentUIHandler::~DaoAgentUIHandler() {
  weak_factory_.InvalidateWeakPtrs();
  AbortAgentTurn(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                  "Dao Agent UI was destroyed."));
}

void DaoAgentUIHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "beginAgentTurn",
      base::BindRepeating(&DaoAgentUIHandler::HandleBeginAgentTurn,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelBeginAgentTurn",
      base::BindRepeating(&DaoAgentUIHandler::HandleCancelBeginAgentTurn,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelHomeHistoryClaim",
      base::BindRepeating(&DaoAgentUIHandler::HandleCancelHomeHistoryClaim,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "endAgentTurn",
      base::BindRepeating(&DaoAgentUIHandler::HandleEndAgentTurn,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "executeHomeTool",
      base::BindRepeating(&DaoAgentUIHandler::HandleExecuteHomeTool,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelBrowserTool",
      base::BindRepeating(&DaoAgentUIHandler::HandleCancelBrowserTool,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getPageInfo", base::BindRepeating(&DaoAgentUIHandler::HandleGetPageInfo,
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
      "getPdfText", base::BindRepeating(&DaoAgentUIHandler::HandleGetPdfText,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "moveCursor", base::BindRepeating(&DaoAgentUIHandler::HandleMoveCursor,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "agentClick", base::BindRepeating(&DaoAgentUIHandler::HandleAgentClick,
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
      "clickByRef", base::BindRepeating(&DaoAgentUIHandler::HandleClickByRef,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "captureScreenshot",
      base::BindRepeating(&DaoAgentUIHandler::HandleCaptureScreenshot,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "scrollPage", base::BindRepeating(&DaoAgentUIHandler::HandleScrollPage,
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
      "listTabs", base::BindRepeating(&DaoAgentUIHandler::HandleListTabs,
                                      base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "switchTab", base::BindRepeating(&DaoAgentUIHandler::HandleSwitchTab,
                                       base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openTab", base::BindRepeating(&DaoAgentUIHandler::HandleOpenTab,
                                     base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "closeTab", base::BindRepeating(&DaoAgentUIHandler::HandleCloseTab,
                                      base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "pressKeyChord",
      base::BindRepeating(&DaoAgentUIHandler::HandlePressKeyChord,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "typeText", base::BindRepeating(&DaoAgentUIHandler::HandleTypeText,
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
      "openAgentSettings",
      base::BindRepeating(&DaoAgentUIHandler::HandleOpenAgentSettings,
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
      "getPageHtml", base::BindRepeating(&DaoAgentUIHandler::HandleGetPageHtml,
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
      "searchInResources",
      base::BindRepeating(&DaoAgentUIHandler::HandleSearchInResources,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "nativeFetch", base::BindRepeating(&DaoAgentUIHandler::HandleNativeFetch,
                                         base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceDownload",
      base::BindRepeating(&DaoAgentUIHandler::HandleWorkspaceDownload,
                          base::Unretained(this)));
}

void DaoAgentUIHandler::MoveCursor(content::WebContents* target,
                                   double x,
                                   double y,
                                   base::OnceCallback<void(bool)> callback) {
  Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  auto* cursor_view = browser_view ? browser_view->dao_agent_cursor() : nullptr;
  if (!target || !browser_view || !cursor_view) {
    std::move(callback).Run(false);
    return;
  }
  if (!CanAnimateAgentCursorForTarget(target)) {
    cursor_view->Hide();
    std::move(callback).Run(false);
    return;
  }

  gfx::Rect viewport_screen;
  if (target->GetRenderWidgetHostView()) {
    viewport_screen = target->GetRenderWidgetHostView()->GetViewBounds();
  } else {
    viewport_screen = browser_view->contents_container()->GetBoundsInScreen();
  }
  const gfx::Rect cursor_bounds = cursor_view->GetBoundsInScreen();
  const float view_x =
      static_cast<float>(viewport_screen.x() - cursor_bounds.x()) + x;
  const float view_y =
      static_cast<float>(viewport_screen.y() - cursor_bounds.y()) + y;
  if (!cursor_view->is_visible()) {
    cursor_view->ShowAtCenter();
  }
  cursor_view->AnimateTo(view_x, view_y,
                         base::BindOnce(
                             [](base::WeakPtr<content::WebContents> target,
                                base::OnceCallback<void(bool)> done) {
                               std::move(done).Run(
                                   target && CanAnimateAgentCursorForTarget(
                                                 target.get()));
                             },
                             target->GetWeakPtr(),
                             std::move(callback)));
}

void DaoAgentUIHandler::PlayClickRipple(content::WebContents* target) {
  Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  auto* cursor_view = browser_view ? browser_view->dao_agent_cursor() : nullptr;
  if (!cursor_view) {
    return;
  }
  if (!CanAnimateAgentCursorForTarget(target)) {
    cursor_view->Hide();
    return;
  }
  cursor_view->PlayClickRipple();
}

void DaoAgentUIHandler::CancelCursor(content::WebContents* target) {
  if (!target) {
    return;
  }
  for (BrowserWindowInterface* browser_window :
       GetAllBrowserWindowInterfaces()) {
    Browser* browser =
        browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
    if (!browser || browser->profile() != target->GetBrowserContext()) {
      continue;
    }
    BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
    if (browser_view && browser_view->dao_agent_cursor()) {
      browser_view->dao_agent_cursor()->Hide();
    }
  }
}

bool DaoAgentUIHandler::IsTargetLocked(content::WebContents* target) {
  return target && DaoAgentLockTabHelper::IsLocked(target);
}

void DaoAgentUIHandler::LockTarget(content::WebContents* target) {
  if (target) {
    DaoAgentLockTabHelper::LockContents(target);
  }
}

void DaoAgentUIHandler::UnlockTarget(content::WebContents* target) {
  if (target) {
    DaoAgentLockTabHelper::UnlockContents(target);
  }
}

void DaoAgentUIHandler::SetAgentTurnTarget(content::WebContents* target) {
  if (!agent_turn_session_) {
    return;
  }
  agent_turn_session_->SetTarget(target);
}

void DaoAgentUIHandler::AbortAgentTurn(DaoToolError error) {
  content::WebContents* target =
      agent_turn_session_ ? ResolveTargetContents() : nullptr;
  if (home_turn_authorization_ && agent_turn_session_ &&
      !active_turn_id_.empty()) {
    Profile* profile = agent_turn_session_->profile();
    if (profile) {
      DaoHomeProjectService* service =
          DaoHomeProjectServiceFactory::GetForProfile(profile);
      service->ClearHistoryBootstrapForTurn(active_turn_id_);
    }
  }
  InvalidateHomeMutationLeases();
  if (target && IsDaoHomeUrl(target->GetLastCommittedURL())) {
    content::WebUI* target_ui = target->GetWebUI();
    DaoHomeUI* home_ui = target_ui && target_ui->GetController()
                             ? target_ui->GetController()->GetAs<DaoHomeUI>()
                             : nullptr;
    if (home_ui) {
      home_ui->CancelAgentSession();
    }
  }
  if (browser_tool_executor_) {
    browser_tool_executor_->CancelAll(error);
    browser_tool_executor_->ClearSessionState(agent_turn_session_.get());
  }
  agent_turn_session_.reset();
  agent_turn_lease_.reset();
  home_turn_authorization_.reset();
  agent_turn_unavailable_error_.reset();
  active_turn_id_.clear();
  pending_begin_callback_id_.clear();
}

void DaoAgentUIHandler::FinishPendingBeginAgentTurn(
    const std::string& callback_id) {
  if (pending_begin_callback_id_ == callback_id) {
    pending_begin_callback_id_.clear();
  }
}

bool DaoAgentUIHandler::OwnsActiveHomeTurn(
    const std::string& turn_id,
    const base::WeakPtr<content::WebContents>& target) {
  return active_turn_id_ == turn_id && agent_turn_session_ && target &&
         home_turn_authorization_ && home_turn_authorization_->IsValid() &&
         ResolveTargetContents() == target.get() &&
         IsDaoHomeUrl(target->GetLastCommittedURL());
}

void DaoAgentUIHandler::InvalidateHomeMutationLeases() {
  for (const auto& entry : home_mutation_leases_) {
    entry.second->Invalidate();
  }
  home_mutation_leases_.clear();
}

void DaoAgentUIHandler::ExecutePageTool(std::string callback_id,
                                        std::string tool_name,
                                        base::DictValue arguments) {
  bool legacy_ui_one_shot = false;
  if (const base::Value* context = arguments.Find(kAgentExecutionContextKey)) {
    if (!context->is_string() ||
        context->GetString() != kLegacyUiOneShotContext ||
        !IsLegacyUiOneShotTool(tool_name)) {
      ResolvePageToolError(
          std::move(callback_id),
          MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                           "Invalid Dao Agent execution context."));
      return;
    }
    legacy_ui_one_shot = true;
    arguments.Remove(kAgentExecutionContextKey);
  }
  DaoBrowserAutomationSession* session = agent_turn_session_.get();
  if (session && agent_turn_lease_) {
    DaoBrowserToolCall call;
    call.request_id = callback_id;
    call.name = std::move(tool_name);
    call.arguments = std::move(arguments);
    browser_tool_executor_->Execute(
        session, DaoToolClient::kDaoAgent, std::move(call),
        base::BindOnce(&DaoAgentUIHandler::OnPageToolComplete,
                       weak_factory_.GetWeakPtr(), std::move(callback_id)));
    return;
  }

  if (!legacy_ui_one_shot && agent_turn_unavailable_error_) {
    ResolvePageToolError(std::move(callback_id),
                         *agent_turn_unavailable_error_);
    return;
  }

  if (!legacy_ui_one_shot) {
    ResolvePageToolError(
        std::move(callback_id),
        MakeDaoToolError(DaoToolErrorCode::kLeaseBusy,
                         "No active Dao Agent turn owns browser control."));
    return;
  }

  content::WebContents* target = GetActivePageContents();
  Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
  if (!target || !browser) {
    ResolvePageToolError(
        std::move(callback_id),
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "No active browser target is available."));
    return;
  }

  auto acquired = DaoAgentLeaseManager::GetForProfile(browser->profile())
                      ->TryAcquire(tabs::TabInterface::GetFromContents(target)
                                       ->GetHandle(),
                                   {DaoToolClient::kDaoAgent,
                                    "dao-agent-legacy-ui", "Dao Agent UI"});
  if (!acquired.has_value()) {
    ResolvePageToolError(std::move(callback_id),
                         LocalizeAgentToolError(std::move(acquired).error()));
    return;
  }
  auto authorization = std::make_unique<LegacyUiOneShotAuthorization>(
      std::move(acquired).value(), browser, target);

  DaoBrowserToolCall call;
  call.request_id = callback_id;
  call.name = std::move(tool_name);
  call.arguments = std::move(arguments);
  DaoBrowserAutomationSession* transient_session = &authorization->session;
  PageToolCompletion completion = HoldLegacyUiAuthorizationUntilComplete(
      std::move(authorization),
      base::BindOnce(&DaoAgentUIHandler::OnPageToolComplete,
                     weak_factory_.GetWeakPtr(), std::move(callback_id)));
  browser_tool_executor_->Execute(transient_session, DaoToolClient::kDaoAgent,
                                  std::move(call), std::move(completion));
}

void DaoAgentUIHandler::ExecuteTabTool(std::string callback_id,
                                       std::string tool_name,
                                       base::DictValue arguments) {
  if (!RequireActiveAgentTurn(callback_id)) {
    return;
  }
  auto target = agent_turn_session_->ResolveTarget();
  base::WeakPtr<content::WebContents> previous_target =
      target.has_value() ? target.value()->GetWeakPtr() : nullptr;

  DaoBrowserToolCall call;
  call.request_id = callback_id;
  call.name = std::move(tool_name);
  call.arguments = std::move(arguments);
  browser_tool_executor_->Execute(
      agent_turn_session_.get(), DaoToolClient::kDaoAgent, std::move(call),
      base::BindOnce(&DaoAgentUIHandler::OnTabToolComplete,
                     weak_factory_.GetWeakPtr(), std::move(callback_id),
                     std::move(previous_target)));
}

void DaoAgentUIHandler::OnPageToolComplete(std::string callback_id,
                                           DaoBrowserToolResult result) {
  if (result.error) {
    ResolvePageToolError(std::move(callback_id), std::move(*result.error));
    return;
  }
  base::Value response = std::move(result.data);
  if (result.media) {
    if (!response.is_dict()) {
      response =
          base::Value(base::DictValue().Set("result", std::move(response)));
    }
    response.GetDict().Set(
        "media", base::DictValue()
                     .Set("mimeType", std::move(result.media->mime_type))
                     .Set("data", std::move(result.media->data)));
  }
  ResolveJavascriptCallback(base::Value(callback_id), std::move(response));
}

void DaoAgentUIHandler::OnTabToolComplete(
    std::string callback_id,
    base::WeakPtr<content::WebContents> previous_target,
    DaoBrowserToolResult result) {
  OnPageToolComplete(std::move(callback_id), std::move(result));
}

void DaoAgentUIHandler::ResolvePageToolError(std::string callback_id,
                                             DaoToolError error) {
  base::DictValue response;
  response.Set("error", std::move(error.message));
  response.Set("code", std::string(DaoToolErrorCodeToString(error.code)));
  response.Set("retryable", error.retryable);
  ResolveJavascriptCallback(base::Value(callback_id), std::move(response));
}

content::WebContents* DaoAgentUIHandler::GetActivePageContents() {
  Browser* browser = FindLastActiveBrowserForMigration();
  if (!browser) {
    return nullptr;
  }

  content::WebContents* contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return nullptr;
  }
  if (!tabs::TabInterface::MaybeGetFromContents(contents)) {
    return nullptr;
  }

  // Don't attach to the agent page itself.
  if (contents->GetURL().host() == "agent") {
    return nullptr;
  }

  return contents;
}

content::WebContents* DaoAgentUIHandler::ResolveTargetContents() {
  if (agent_turn_session_) {
    auto target = agent_turn_session_->ResolveTarget();
    return target.has_value() ? target.value() : nullptr;
  }

  return GetActivePageContents();
}

content::WebContents* DaoAgentUIHandler::EnsureAttached() {
  if (!agent_turn_lease_ || !agent_turn_session_) {
    return nullptr;
  }
  content::WebContents* contents = ResolveTargetContents();
  if (!contents) {
    return nullptr;
  }

  if (!devtools_client_->AttachTo(contents)) {
    return nullptr;
  }

  return contents;
}

bool DaoAgentUIHandler::RequireActiveAgentTurn(const std::string& callback_id) {
  if (agent_turn_lease_ && agent_turn_session_) {
    return true;
  }
  if (agent_turn_unavailable_error_ && !active_turn_id_.empty()) {
    ResolvePageToolError(callback_id, *agent_turn_unavailable_error_);
    return false;
  }
  ResolvePageToolError(
      callback_id,
      MakeDaoToolError(DaoToolErrorCode::kLeaseBusy,
                       "No active Dao Agent turn owns browser control."));
  return false;
}

void DaoAgentUIHandler::HandleBeginAgentTurn(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  std::string history_claim_token;
  if (args.size() > 1 && args[1].is_dict()) {
    const std::string* token =
        args[1].GetDict().FindString("historyClaimToken");
    if (token && token->size() <= 64) {
      history_claim_token = *token;
    }
  }

  auto clear_history_claim = [this, &history_claim_token]() {
    if (history_claim_token.empty()) {
      return;
    }
    Profile* profile = Profile::FromWebUI(web_ui());
    if (profile) {
      DaoHomeProjectServiceFactory::GetForProfile(profile)
          ->ClearHistoryBootstrapForClaim(history_claim_token);
    }
  };

  AbortAgentTurn(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                  "Previous Dao Agent turn was replaced."));
  pending_begin_callback_id_ = callback_id;
  content::WebContents* contents = GetActivePageContents();
  Browser* browser = contents ? chrome::FindBrowserWithTab(contents) : nullptr;
  if (!contents || !browser) {
    clear_history_claim();
    FinishPendingBeginAgentTurn(callback_id);
    ResolvePageToolError(
        callback_id,
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "No active browser target is available."));
    return;
  }

  auto acquired = DaoAgentLeaseManager::GetForProfile(browser->profile())
                      ->TryAcquire(tabs::TabInterface::GetFromContents(contents)
                                       ->GetHandle(),
                                   {DaoToolClient::kDaoAgent, "dao-agent-turn",
                                    "Dao Agent"});
  if (!acquired.has_value()) {
    DaoToolError error = LocalizeAgentToolError(std::move(acquired).error());
    if (error.code != DaoToolErrorCode::kAgentControlBusy) {
      clear_history_claim();
      FinishPendingBeginAgentTurn(callback_id);
      ResolvePageToolError(callback_id, std::move(error));
      return;
    }
    active_turn_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
    clear_history_claim();
    agent_turn_unavailable_error_ = std::move(error);
    base::DictValue response;
    response.Set("success", true);
    response.Set("turnId", active_turn_id_);
    response.Set("url", contents->GetVisibleURL().spec());
    response.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  agent_turn_lease_ =
      std::make_unique<DaoAgentLease>(std::move(acquired).value());
  agent_turn_session_ =
      std::make_unique<DaoBrowserAutomationSession>(browser, contents);
  active_turn_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  SetAgentTurnTarget(contents);
  agent_turn_session_->set_expected_domain(expected_domain_);

  base::DictValue response;
  response.Set("success", true);
  response.Set("turnId", active_turn_id_);
  response.Set("url", contents->GetVisibleURL().spec());
  response.Set("title", base::UTF16ToUTF8(contents->GetTitle()));
  if (IsDaoHomeUrl(contents->GetLastCommittedURL())) {
    content::WebUI* target_ui = contents->GetWebUI();
    DaoHomeUI* home_ui = target_ui && target_ui->GetController()
                             ? target_ui->GetController()->GetAs<DaoHomeUI>()
                             : nullptr;
    home_turn_authorization_ =
        home_ui ? home_ui->CreateMutationLease() : nullptr;
    if (!home_turn_authorization_) {
      clear_history_claim();
      AbortAgentTurn(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                      "Dao Home is not visible."));
      ResolvePageToolError(
          callback_id,
          MakeDaoToolError(DaoToolErrorCode::kTargetForbidden,
                           "Dao Home must be visible to start an Agent turn."));
      return;
    }
    base::DictValue home_context =
        base::DictValue().Set("active", true).Set("revision", std::string());
    Profile* profile = agent_turn_session_->profile();
    DaoHomeProjectService* service =
        profile ? DaoHomeProjectServiceFactory::GetForProfile(profile)
                : nullptr;
    if (service) {
      const bool claimed_history = service->ClaimHistoryBootstrap(
          contents, history_claim_token, active_turn_id_);
      if (claimed_history) {
        home_context.Set("bootstrapKind", "history");
      }
      response.Set("homeContext", std::move(home_context));
      const std::string turn_id = active_turn_id_;
      base::WeakPtr<content::WebContents> target = contents->GetWeakPtr();
      service->GetSnapshot(base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> self, std::string callback_id,
             std::string turn_id, base::WeakPtr<content::WebContents> target,
             base::DictValue response, HomeSnapshot snapshot) {
            if (!self) {
              return;
            }
            if (!self->OwnsActiveHomeTurn(turn_id, target)) {
              if (self->active_turn_id_ == turn_id) {
                self->AbortAgentTurn(
                    MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                     "The active Dao Home turn changed."));
              }
              self->ResolvePageToolError(
                  std::move(callback_id),
                  MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                   "The active Dao Home turn changed."));
              return;
            }
            response.FindDict("homeContext")
                ->Set("revision", snapshot.revision);
            self->ResolveJavascriptCallback(base::Value(callback_id),
                                            std::move(response));
          },
          weak_factory_.GetWeakPtr(), callback_id, std::move(turn_id),
          std::move(target), std::move(response)));
      return;
    }
    response.Set("homeContext", std::move(home_context));
  }
  clear_history_claim();
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleCancelBeginAgentTurn(
    const base::ListValue& args) {
  if (args.empty() || !args[0].is_string() ||
      pending_begin_callback_id_ != args[0].GetString()) {
    return;
  }
  AbortAgentTurn(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                  "Dao Agent turn startup timed out."));
}

void DaoAgentUIHandler::HandleCancelHomeHistoryClaim(
    const base::ListValue& args) {
  if (args.empty() || !args[0].is_string() || args[0].GetString().size() > 64) {
    return;
  }
  Profile* profile = Profile::FromWebUI(web_ui());
  if (profile) {
    DaoHomeProjectServiceFactory::GetForProfile(profile)
        ->ClearHistoryBootstrapForClaim(args[0].GetString());
  }
}

void DaoAgentUIHandler::HandleExecuteHomeTool(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const base::DictValue& request = args[1].GetDict();
  const std::string* name = request.FindString("name");
  const base::DictValue* arguments = request.FindDict("arguments");
  if (!name || !arguments) {
    ResolvePageToolError(callback_id,
                         MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                                          "Invalid Dao Home tool request."));
    return;
  }
  if (!RequireActiveAgentTurn(callback_id)) {
    return;
  }
  content::WebContents* target = ResolveTargetContents();
  Profile* profile = agent_turn_session_->profile();
  if (!target || !profile ||
      target->GetBrowserContext() != profile || profile->IsOffTheRecord() ||
      !IsDaoHomeUrl(target->GetLastCommittedURL())) {
    ResolvePageToolError(
        callback_id,
        MakeDaoToolError(
            DaoToolErrorCode::kTargetForbidden,
            "Dao Home tools require the exact active dao://home tab."));
    return;
  }
  if (!home_agent_tools_) {
    home_agent_tools_ = std::make_unique<DaoHomeAgentTools>(
        DaoHomeProjectServiceFactory::GetForProfile(profile));
  }
  content::WebUI* target_ui = target->GetWebUI();
  DaoHomeUI* home_ui = target_ui && target_ui->GetController()
                           ? target_ui->GetController()->GetAs<DaoHomeUI>()
                           : nullptr;
  scoped_refptr<DaoHomeMutationLease> mutation_lease;
  if (home_ui && home_turn_authorization_ &&
      home_turn_authorization_->IsValid()) {
    mutation_lease =
        base::MakeRefCounted<DaoHomeMutationLease>(home_turn_authorization_);
  }
  if (!mutation_lease) {
    ResolvePageToolError(callback_id,
                         MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                          "The active Dao Home turn changed."));
    return;
  }
  home_agent_tools_->SetConnectorRunner(base::BindRepeating(
      [](base::WeakPtr<content::WebContents> target, std::string draft_id,
         std::string connector_id, base::Value input,
         DaoHomeAgentTools::Callback callback) {
        content::WebUI* target_ui = target ? target->GetWebUI() : nullptr;
        DaoHomeUI* home_ui =
            target_ui && target_ui->GetController()
                ? target_ui->GetController()->GetAs<DaoHomeUI>()
                : nullptr;
        if (!home_ui) {
          std::move(callback).Run(base::Value(
              base::DictValue().Set("ok", false).Set("code", "cancelled")));
          return;
        }
        home_ui->CollectConnectorForAgent(
            std::move(draft_id), std::move(connector_id), std::move(input),
            std::move(callback));
      },
      target->GetWeakPtr()));
  home_agent_tools_->SetPreviewRunner(base::BindRepeating(
      [](base::WeakPtr<content::WebContents> target, std::string draft_id,
         std::string entry, HomePreviewRequirements requirements,
         DaoHomeAgentTools::Callback callback) {
        content::WebUI* target_ui = target ? target->GetWebUI() : nullptr;
        DaoHomeUI* home_ui =
            target_ui && target_ui->GetController()
                ? target_ui->GetController()->GetAs<DaoHomeUI>()
                : nullptr;
        if (!home_ui) {
          std::move(callback).Run(base::Value(
              base::DictValue()
                  .Set("error", "The active Dao Home host is unavailable.")
                  .Set("code", "cancelled")));
          return;
        }
        home_ui->PreviewDraftForAgent(std::move(draft_id), std::move(entry),
                                      std::move(requirements),
                                      std::move(callback));
      },
      target->GetWeakPtr()));
  const std::string turn_id = active_turn_id_;
  base::WeakPtr<content::WebContents> target_weak = target->GetWeakPtr();
  auto existing = home_mutation_leases_.find(callback_id);
  if (existing != home_mutation_leases_.end()) {
    existing->second->Invalidate();
  }
  home_mutation_leases_.insert_or_assign(callback_id, mutation_lease);
  DaoHomeAgentTools::OwnerValidator owner_validator = base::BindRepeating(
      [](base::WeakPtr<DaoAgentUIHandler> self, std::string turn_id,
         base::WeakPtr<content::WebContents> target) {
        return self && self->OwnsActiveHomeTurn(turn_id, target);
      },
      weak_factory_.GetWeakPtr(), turn_id, target_weak);
  home_agent_tools_->Execute(
      *name, arguments->Clone(), mutation_lease, home_turn_authorization_,
      std::move(owner_validator), turn_id,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentUIHandler> self, std::string callback_id,
             std::string turn_id, base::WeakPtr<content::WebContents> target,
             base::Value result) {
            if (!self) {
              return;
            }
            auto lease = self->home_mutation_leases_.find(callback_id);
            if (lease != self->home_mutation_leases_.end()) {
              lease->second->Invalidate();
              self->home_mutation_leases_.erase(lease);
            }
            if (!self->OwnsActiveHomeTurn(turn_id, target)) {
              self->ResolvePageToolError(
                  std::move(callback_id),
                  MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                   "The active Dao Home turn changed."));
              return;
            }
            self->ResolveJavascriptCallback(base::Value(callback_id),
                                            std::move(result));
          },
          weak_factory_.GetWeakPtr(), callback_id, std::move(turn_id),
          std::move(target_weak)));
}

void DaoAgentUIHandler::HandleEndAgentTurn(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  const std::string* turn_id = args.size() >= 2 && args[1].is_dict()
                                   ? args[1].GetDict().FindString("turnId")
                                   : nullptr;
  if (!turn_id || active_turn_id_.empty() || *turn_id != active_turn_id_) {
    base::DictValue response;
    response.Set("success", false);
    response.Set("stale", true);
    ResolveJavascriptCallback(base::Value(callback_id), response);
    return;
  }

  AbortAgentTurn(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                  "Dao Agent turn ended."));
  base::DictValue response;
  response.Set("success", true);
  ResolveJavascriptCallback(base::Value(callback_id), response);
}

void DaoAgentUIHandler::HandleCancelBrowserTool(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  browser_tool_executor_->Cancel(
      args[0].GetString(), MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                            "Browser tool call was aborted."));
}

void DaoAgentUIHandler::HandleGetPageInfo(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "get_page_info",
                  args.size() >= 2 && args[1].is_dict()
                      ? args[1].GetDict().Clone()
                      : base::DictValue());
}

void DaoAgentUIHandler::HandleClickElement(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "click_element",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleExecuteScript(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  base::DictValue arguments =
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue();
  if (const std::optional<bool> lock_tab = arguments.FindBool("lockTab")) {
    arguments.Set("lock_tab", *lock_tab);
    arguments.Remove("lockTab");
  }
  ExecutePageTool(args[0].GetString(), "execute_script", std::move(arguments));
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
          [](base::WeakPtr<DaoAgentUIHandler> handler, std::string cb_id,
             GURL initial_url) {
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

void DaoAgentUIHandler::StartPdfCapture(std::unique_ptr<PdfCaptureState> state,
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
      page_index, base::BindOnce(&DaoAgentUIHandler::OnPdfPageText,
                                 weak_factory_.GetWeakPtr(), std::move(state)));
}

void DaoAgentUIHandler::OnPdfPageText(std::unique_ptr<PdfCaptureState> state,
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

void DaoAgentUIHandler::HandleHighlightElement(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "highlight_element",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleClearHighlight(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  if (agent_turn_session_) {
    browser_tool_executor_->ClearPageState(agent_turn_session_.get());
  } else {
    content::WebContents* target = GetActivePageContents();
    Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
    if (target && browser) {
      auto acquired =
          DaoAgentLeaseManager::GetForProfile(browser->profile())
              ->TryAcquire(tabs::TabInterface::GetFromContents(target)
                               ->GetHandle(),
                           {DaoToolClient::kDaoAgent,
                            "dao-agent-clear-highlight", "Dao Agent"});
      if (!acquired.has_value()) {
        ResolvePageToolError(callback_id, std::move(acquired).error());
        return;
      }
      DaoBrowserAutomationSession session(browser, target);
      browser_tool_executor_->ClearPageState(&session);
    } else {
      browser_tool_executor_->ClearPageState(nullptr);
    }
  }
  ResolveJavascriptCallback(base::Value(callback_id), base::Value(true));
}

void DaoAgentUIHandler::HandleMoveCursor(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "move_cursor",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleAgentClick(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "agent_click",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleGetAccessibilityTree(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "get_accessibility_tree",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleClickByRef(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "click_by_ref",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleCaptureScreenshot(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "capture_screenshot",
                  args.size() >= 2 && args[1].is_dict()
                      ? args[1].GetDict().Clone()
                      : base::DictValue());
}

void DaoAgentUIHandler::HandleScrollPage(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  std::string direction = "down";
  if (args[1].is_dict()) {
    const std::string* requested_direction =
        args[1].GetDict().FindString("direction");
    if (requested_direction) {
      direction = *requested_direction;
    }
  }
  base::DictValue arguments =
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue();
  arguments.Remove("direction");
  ExecutePageTool(args[0].GetString(),
                  direction == "up" ? "scroll_up" : "scroll_down",
                  std::move(arguments));
}

void DaoAgentUIHandler::HandleScrollToElement(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "scroll_to_element",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleSetExpectedDomain(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  if (args[1].is_dict()) {
    auto* domain = args[1].GetDict().FindString("domain");
    if (domain) {
      expected_domain_ = *domain;
      if (agent_turn_session_) {
        agent_turn_session_->set_expected_domain(expected_domain_);
      }
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
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecuteTabTool(args[0].GetString(), "list_tabs", base::DictValue());
}

void DaoAgentUIHandler::HandleSwitchTab(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecuteTabTool(
      args[0].GetString(), "switch_tab",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleOpenTab(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecuteTabTool(
      args[0].GetString(), "open_tab",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleCloseTab(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecuteTabTool(
      args[0].GetString(), "close_tab",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

// ---- Keyboard Input ----

void DaoAgentUIHandler::HandlePressKeyChord(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "press_key_chord",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

void DaoAgentUIHandler::HandleTypeText(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(
      args[0].GetString(), "type_text",
      args[1].is_dict() ? args[1].GetDict().Clone() : base::DictValue());
}

// ---- Network/Console Debugging ----

void DaoAgentUIHandler::HandleEnableNetworkTracking(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "enable_network_tracking",

                  base::DictValue());
}

void DaoAgentUIHandler::HandleGetNetworkRequests(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "get_network_requests",

                  base::DictValue());
}

void DaoAgentUIHandler::HandleClearNetworkRequests(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "clear_network_requests",

                  base::DictValue());
}

void DaoAgentUIHandler::HandleEnableConsoleTracking(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "enable_console_tracking",
                  base::DictValue());
}

void DaoAgentUIHandler::HandleGetConsoleMessages(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  base::DictValue arguments;
  if (args.size() >= 2 && args[1].is_dict()) {
    if (const std::string* filter = args[1].GetDict().FindString("filter")) {
      arguments.Set("filter", *filter);
    }
  }
  ExecutePageTool(args[0].GetString(), "get_console_messages",
                  std::move(arguments));
}

void DaoAgentUIHandler::HandleClearConsoleMessages(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "clear_console_messages",

                  base::DictValue());
}

void DaoAgentUIHandler::HandleGetPageHtml(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "get_page_html", base::DictValue());
}

void DaoAgentUIHandler::HandleListPageResources(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  base::DictValue arguments;
  if (args.size() >= 2 && args[1].is_dict()) {
    if (const std::string* type_filter =
            args[1].GetDict().FindString("type_filter")) {
      arguments.Set("type_filter", *type_filter);
    }
  }
  ExecutePageTool(args[0].GetString(), "list_page_resources",
                  std::move(arguments));
}

void DaoAgentUIHandler::HandleGetResourceContent(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  base::DictValue arguments;
  if (args[1].is_dict()) {
    if (const std::string* url = args[1].GetDict().FindString("url")) {
      arguments.Set("url", *url);
    }
    if (const std::string* frame_id =
            args[1].GetDict().FindString("frame_id")) {
      arguments.Set("frame_id", *frame_id);
    }
  }
  ExecutePageTool(args[0].GetString(), "get_resource_content",
                  std::move(arguments));
}

void DaoAgentUIHandler::HandleGetNetworkBody(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  base::DictValue arguments;
  if (args[1].is_dict()) {
    if (const std::string* request_id =
            args[1].GetDict().FindString("request_id")) {
      arguments.Set("request_id", *request_id);
    }
  }
  ExecutePageTool(args[0].GetString(), "get_network_body",
                  std::move(arguments));
}

void DaoAgentUIHandler::HandleSearchInResources(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  ExecutePageTool(args[0].GetString(), "search_in_resources",
                  args[1].GetDict().Clone());
}

void DaoAgentUIHandler::HandleOpenAgentSettings(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  Browser* browser = FindLastActiveBrowserForMigration();
  if (!browser) {
    ResolveJavascriptCallback(base::Value(callback_id),
                              base::DictValue().Set("success", false));
    return;
  }

  const GURL settings_url(std::string(content::kChromeUIScheme) +
                          "://settings/agent");
  NavigateParams params(browser, settings_url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
  ResolveJavascriptCallback(
      base::Value(callback_id),
      base::DictValue().Set("success",
                            params.navigated_or_inserted_contents != nullptr));
}

void DaoAgentUIHandler::HandleCloseSidebar(const base::ListValue& args) {
  Browser* browser = FindLastActiveBrowserForMigration();
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

void DaoAgentUIHandler::HandleFocusAgentSidebar(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();

  Browser* browser = FindLastActiveBrowserForMigration();
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

  service->GetMemoryContext(url, domain, session_id,
                            base::BindOnce(
                                [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                                   std::string cb_id, MemoryContext ctx) {
                                  if (!handler) {
                                    return;
                                  }
                                  handler->ResolveJavascriptCallback(
                                      base::Value(cb_id),
                                      SerializeMemoryContextForAgentUi(ctx));
                                },
                                weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleEndSession(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string session_id = args[1].is_string() ? args[1].GetString() : "";

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
          [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
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
              msg.Set(
                  "timestamp",
                  static_cast<double>(
                      m.timestamp.ToDeltaSinceWindowsEpoch().InMicroseconds()));
              list.Append(std::move(msg));
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), list);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleGetPreferences(const base::ListValue& args) {
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
          [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
             std::vector<Preference> prefs) {
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

  service->MergePreference(key, value, confidence,
                           base::BindOnce(
                               [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                                  std::string cb_id, bool success) {
                                 if (!handler) {
                                   return;
                                 }
                                 handler->ResolveJavascriptCallback(
                                     base::Value(cb_id), base::Value(success));
                               },
                               weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleDeleteMemory(const base::ListValue& args) {
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
        args[2].GetInt(), base::BindOnce(
                              [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                                 std::string cb_id, bool success) {
                                if (!handler) {
                                  return;
                                }
                                handler->ResolveJavascriptCallback(
                                    base::Value(cb_id), base::Value(success));
                              },
                              weak_factory_.GetWeakPtr(), callback_id));
  } else if (type == "episode" && args[2].is_int()) {
    service->DeleteEpisode(args[2].GetInt(),
                           base::BindOnce(
                               [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                                  std::string cb_id, bool success) {
                                 if (!handler) {
                                   return;
                                 }
                                 handler->ResolveJavascriptCallback(
                                     base::Value(cb_id), base::Value(success));
                               },
                               weak_factory_.GetWeakPtr(), callback_id));
  } else if (type == "conversation" && args[2].is_string()) {
    service->DeleteConversation(
        args[2].GetString(),
        base::BindOnce(
            [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
               bool success) {
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

void DaoAgentMemoryHandler::HandleGetEpisodes(const base::ListValue& args) {
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
          [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
             std::vector<Episode> episodes) {
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
              ep.Set(
                  "timestamp",
                  static_cast<double>(
                      e.timestamp.ToDeltaSinceWindowsEpoch().InMicroseconds()));
              ep.Set("confidence", e.confidence);
              list.Append(std::move(ep));
            }
            handler->ResolveJavascriptCallback(base::Value(cb_id), list);
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleClearAllMemory(const base::ListValue& args) {
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

void DaoAgentMemoryHandler::HandleGetStorageStats(const base::ListValue& args) {
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
            [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
               bool success) {
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
            [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
               bool success) {
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
  double threshold =
      args[1].is_double()
          ? args[1].GetDouble()
          : (args[1].is_int() ? static_cast<double>(args[1].GetInt()) : 0.75);

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
      std::move(feedback), base::BindOnce(
                               [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                                  std::string cb_id, bool success) {
                                 if (!handler) {
                                   return;
                                 }
                                 handler->ResolveJavascriptCallback(
                                     base::Value(cb_id), base::Value(success));
                               },
                               weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleSaveEpisode(const base::ListValue& args) {
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
  if (auto* v = d.FindString("domain")) {
    episode.domain = *v;
  }
  if (auto* v = d.FindString("pathTemplate")) {
    episode.path_template = *v;
  }
  if (auto* v = d.FindString("url")) {
    episode.url = *v;
  }
  if (auto* v = d.FindString("title")) {
    episode.title = *v;
  }
  if (auto* v = d.FindString("intent")) {
    episode.intent = *v;
  }
  if (auto* v = d.FindString("entities")) {
    episode.entities = *v;
  }
  if (auto* v = d.FindString("toolsUsed")) {
    episode.tools_used = *v;
  }
  if (auto* v = d.FindString("outcome")) {
    episode.outcome = *v;
  }
  episode.confidence = d.FindDouble("confidence").value_or(0.7);
  if (auto* v = d.FindString("userAction")) {
    episode.user_action = *v;
  }
  if (auto* v = d.FindString("actionResult")) {
    episode.action_result = *v;
  }
  episode.timestamp = base::Time::Now();

  service->SaveEpisode(std::move(episode),
                       base::BindOnce(
                           [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                              std::string cb_id, bool success) {
                             if (!handler) {
                               return;
                             }
                             handler->ResolveJavascriptCallback(
                                 base::Value(cb_id), base::Value(success));
                           },
                           weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentMemoryHandler::HandleSaveSummary(const base::ListValue& args) {
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
  if (auto* v = d.FindString("sessionId")) {
    summary.session_id = *v;
  }
  if (auto* v = d.FindString("summary")) {
    summary.summary = *v;
  }
  summary.message_count = d.FindInt("messageCount").value_or(0);
  if (auto* v = d.FindString("primaryDomain")) {
    summary.primary_domain = *v;
  }
  summary.first_timestamp = base::Time::Now();
  summary.last_timestamp = base::Time::Now();

  service->SaveConversationSummary(
      std::move(summary), base::BindOnce(
                              [](base::WeakPtr<DaoAgentMemoryHandler> handler,
                                 std::string cb_id, bool success) {
                                if (!handler) {
                                  return;
                                }
                                handler->ResolveJavascriptCallback(
                                    base::Value(cb_id), base::Value(success));
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
  if (ProfileBrowserCollection* collection =
          ProfileBrowserCollection::GetForProfile(
              Profile::FromWebUI(web_ui()))) {
    collection->ForEach(
        [&target, tab_id](BrowserWindowInterface* browser_window) {
          TabStripModel* model =
              browser_window->GetBrowserForMigrationOnly()->tab_strip_model();
          for (int i = 0; i < model->count(); ++i) {
            content::WebContents* wc = model->GetWebContentsAt(i);
            if (wc && sessions::SessionTabHelper::IdForTab(wc).id() == tab_id) {
              target = wc;
              return false;
            }
          }
          return true;
        });
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
          [](base::WeakPtr<DaoAgentMemoryHandler> handler, std::string cb_id,
             base::Value result) {
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
  web_ui()->RegisterMessageCallback(
      "getWeeklyDreamReport",
      base::BindRepeating(&DaoDreamReportHandler::HandleGetWeeklyDreamReport,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getWeeklyDreamReports",
      base::BindRepeating(&DaoDreamReportHandler::HandleGetWeeklyDreamReports,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getWeeklyDreamSources",
      base::BindRepeating(&DaoDreamReportHandler::HandleGetWeeklyDreamSources,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openWeeklyDreamSource",
      base::BindRepeating(&DaoDreamReportHandler::HandleOpenWeeklyDreamSource,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "markWeeklyDreamReportViewed",
      base::BindRepeating(
          &DaoDreamReportHandler::HandleMarkWeeklyDreamReportViewed,
          base::Unretained(this)));
}

void DaoDreamReportHandler::HandleGetDreamReport(const base::ListValue& args) {
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

void DaoDreamReportHandler::HandleGetDreamReports(const base::ListValue& args) {
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
      limit, base::BindOnce(
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

void DaoDreamReportHandler::HandleGetWeeklyDreamReport(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
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

  std::string week_start;
  if (args.size() >= 2 && args[1].is_dict()) {
    if (const std::string* value = args[1].GetDict().FindString("weekStart")) {
      week_start = *value;
    }
  }
  if (week_start.empty()) {
    memory->GetWeeklyDreamReports(
        1, base::BindOnce(
               [](base::WeakPtr<DaoDreamReportHandler> self,
                  std::string callback_id,
                  std::vector<WeeklyDreamReport> reports) {
                 if (!self) {
                   return;
                 }
                 std::optional<base::DictValue> serialized;
                 if (!reports.empty()) {
                   serialized = WeeklyDreamReportToDict(reports.front());
                 }
                 self->ResolveJavascriptCallback(
                     base::Value(callback_id),
                     serialized ? base::Value(std::move(*serialized))
                                : base::Value());
               },
               weak_factory_.GetWeakPtr(), callback_id));
    return;
  }

  memory->GetWeeklyDreamReportByWeekStart(
      week_start,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamReportHandler> self, std::string callback_id,
             std::optional<WeeklyDreamReport> report) {
            if (!self) {
              return;
            }
            std::optional<base::DictValue> serialized;
            if (report) {
              serialized = WeeklyDreamReportToDict(*report);
            }
            self->ResolveJavascriptCallback(
                base::Value(callback_id),
                serialized ? base::Value(std::move(*serialized))
                           : base::Value());
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoDreamReportHandler::HandleGetWeeklyDreamReports(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
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
  memory->GetWeeklyDreamReports(
      limit,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamReportHandler> self, std::string callback_id,
             std::vector<WeeklyDreamReport> reports) {
            if (self) {
              self->ResolveJavascriptCallback(
                  base::Value(callback_id), WeeklyDreamReportsToList(reports));
            }
          },
          weak_factory_.GetWeakPtr(), callback_id));
}

void DaoDreamReportHandler::HandleGetWeeklyDreamSources(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::optional<int64_t> report_id =
      ReadInt64(args[1].GetDict(), "reportId");
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!report_id || !memory) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("weekly_invalid_report"));
    return;
  }

  memory->GetWeeklyDreamSources(
      *report_id,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamReportHandler> self, std::string callback_id,
             Profile* profile, std::vector<WeeklyDreamSource> sources) {
            if (!self) {
              return;
            }
            auto available =
                std::make_shared<std::vector<bool>>(sources.size(), false);
            size_t page_count = 0;
            for (size_t i = 0; i < sources.size(); ++i) {
              if (sources[i].source_kind == "conversation") {
                (*available)[i] = !sources[i].local_locator.empty();
              } else if (sources[i].source_kind == "page") {
                ++page_count;
              }
            }

            auto finish = base::BindOnce(
                [](base::WeakPtr<DaoDreamReportHandler> self,
                   std::string callback_id,
                   std::vector<WeeklyDreamSource> sources,
                   std::shared_ptr<std::vector<bool>> available) {
                  if (!self) {
                    return;
                  }
                  base::ListValue result;
                  for (size_t i = 0; i < sources.size(); ++i) {
                    result.Append(
                        WeeklyDreamSourceToDict(sources[i], (*available)[i]));
                  }
                  self->ResolveJavascriptCallback(base::Value(callback_id),
                                                  std::move(result));
                },
                self, callback_id, sources, available);
            history::HistoryService* history =
                HistoryServiceFactory::GetForProfile(
                    profile, ServiceAccessType::EXPLICIT_ACCESS);
            if (!history || page_count == 0) {
              std::move(finish).Run();
              return;
            }

            base::RepeatingClosure barrier =
                base::BarrierClosure(page_count, std::move(finish));
            for (size_t i = 0; i < sources.size(); ++i) {
              if (sources[i].source_kind != "page") {
                continue;
              }
              const GURL url(sources[i].local_locator);
              if (!url.is_valid()) {
                barrier.Run();
                continue;
              }
              history->QueryURL(
                  url,
                  base::BindOnce(
                      [](std::shared_ptr<std::vector<bool>> available,
                         size_t index, base::RepeatingClosure barrier,
                         history::QueryURLResult result) {
                        (*available)[index] = result.success;
                        barrier.Run();
                      },
                      available, i, barrier),
                  &self->history_task_tracker_);
            }
          },
          weak_factory_.GetWeakPtr(), callback_id, profile));
}

void DaoDreamReportHandler::HandleOpenWeeklyDreamSource(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const base::DictValue& params = args[1].GetDict();
  const std::optional<int64_t> report_id = ReadInt64(params, "reportId");
  const std::string* ref_id = params.FindString("refId");
  Profile* profile = Profile::FromWebUI(web_ui());
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!report_id || !ref_id || ref_id->empty() || !memory) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("weekly_source_unavailable"));
    return;
  }

  memory->GetWeeklyDreamSource(
      *report_id, *ref_id,
      base::BindOnce(
          [](base::WeakPtr<DaoDreamReportHandler> self, std::string callback_id,
             Profile* profile, std::optional<WeeklyDreamSource> source) {
            if (!self || !source) {
              if (self) {
                self->RejectJavascriptCallback(
                    base::Value(callback_id),
                    base::Value("weekly_source_unavailable"));
              }
              return;
            }

            Browser* browser =
                chrome::FindBrowserWithTab(self->web_ui()->GetWebContents());
            if (source->source_kind == "conversation") {
              BrowserView* browser_view =
                  browser ? BrowserView::GetBrowserViewForBrowser(browser)
                          : nullptr;
              if (!browser_view || !browser_view->dao_agent_sidebar() ||
                  source->local_locator.empty()) {
                self->RejectJavascriptCallback(
                    base::Value(callback_id),
                    base::Value("weekly_source_unavailable"));
                return;
              }
              browser_view->dao_agent_sidebar()->ExpandAndOpenSession(
                  source->local_locator);
              self->ResolveJavascriptCallback(base::Value(callback_id),
                                              base::Value(true));
              return;
            }

            const GURL url(source->local_locator);
            history::HistoryService* history =
                HistoryServiceFactory::GetForProfile(
                    profile, ServiceAccessType::EXPLICIT_ACCESS);
            if (source->source_kind != "page" || !url.is_valid() || !browser ||
                !history) {
              self->RejectJavascriptCallback(
                  base::Value(callback_id),
                  base::Value("weekly_source_unavailable"));
              return;
            }
            history->QueryURL(
                url,
                base::BindOnce(
                    [](base::WeakPtr<DaoDreamReportHandler> self,
                       std::string callback_id, GURL url,
                       history::QueryURLResult result) {
                      if (!self) {
                        return;
                      }
                      if (!result.success) {
                        self->RejectJavascriptCallback(
                            base::Value(callback_id),
                            base::Value("weekly_source_unavailable"));
                        return;
                      }
                      Browser* browser = chrome::FindBrowserWithTab(
                          self->web_ui()->GetWebContents());
                      if (!browser) {
                        self->RejectJavascriptCallback(
                            base::Value(callback_id),
                            base::Value("weekly_source_unavailable"));
                        return;
                      }
                      NavigateParams navigate_params(browser, url,
                                                     ui::PAGE_TRANSITION_TYPED);
                      navigate_params.disposition =
                          WindowOpenDisposition::NEW_FOREGROUND_TAB;
                      Navigate(&navigate_params);
                      self->ResolveJavascriptCallback(base::Value(callback_id),
                                                      base::Value(true));
                    },
                    self, callback_id, url),
                &self->history_task_tracker_);
          },
          weak_factory_.GetWeakPtr(), callback_id, profile));
}

void DaoDreamReportHandler::HandleMarkWeeklyDreamReportViewed(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::optional<int64_t> report_id =
      args[1].is_dict() ? ReadInt64(args[1].GetDict(), "reportId")
                        : ReadPositiveInt64(&args[1]);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(Profile::FromWebUI(web_ui()));
  if (!report_id || !memory) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("weekly_invalid_report"));
    return;
  }
  memory->MarkWeeklyDreamReportViewed(
      *report_id, base::BindOnce(
                      [](base::WeakPtr<DaoDreamReportHandler> self,
                         std::string callback_id, bool success) {
                        if (!self) {
                          return;
                        }
                        if (success) {
                          self->ResolveJavascriptCallback(
                              base::Value(callback_id), base::Value(true));
                        } else {
                          self->RejectJavascriptCallback(
                              base::Value(callback_id),
                              base::Value("weekly_invalid_report"));
                        }
                      },
                      weak_factory_.GetWeakPtr(), callback_id));
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
      base::BindRepeating(&DaoDreamRunnerHandler::HandleGetDreamExcludedDomains,
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
  web_ui()->RegisterMessageCallback(
      "startManualWeeklyDream",
      base::BindRepeating(&DaoDreamRunnerHandler::HandleStartManualWeeklyDream,
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

void DaoDreamRunnerHandler::RunDream(
    const DaoDreamService::DreamRunRequest& request,
    const base::DictValue& material) {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::DictValue payload;
  payload.Set("requestId", request.request_id);
  payload.Set("reportKind",
              request.report_kind == DaoDreamService::ReportKind::kWeekly
                  ? "weekly"
                  : "daily");
  payload.Set("periodStart", request.period_start);
  payload.Set("periodEnd", request.period_end);
  payload.Set("material", material.Clone());
  payload.Set("debug", profile->GetPrefs()->GetBoolean(prefs::kDaoDreamDebug));
  FireWebUIListener("dream-run", payload);
}

void DaoDreamRunnerHandler::HandleDreamComplete(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string& request_id = args[0].GetString();
  if (request_id.empty()) {
    return;
  }
  const base::DictValue& envelope = args[1].GetDict();
  const std::string* status = envelope.FindString("status");
  if (!status || status->empty()) {
    return;
  }
  DaoDreamService* service = GetDreamService();
  if (!service) {
    return;
  }
  if (*status == "skipped" && !envelope.Find("result")) {
    service->OnDreamSkipped(request_id);
    return;
  }
  const base::DictValue* result = envelope.FindDict("result");
  if (*status == "completed" && result) {
    service->OnDreamResult(request_id, result->Clone());
  }
}

void DaoDreamRunnerHandler::HandleDreamFailed(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_dict()) {
    return;
  }
  const std::string& request_id = args[0].GetString();
  const base::DictValue& envelope = args[1].GetDict();
  const std::string* code = envelope.FindString("code");
  const std::string* message = envelope.FindString("message");
  if (request_id.empty() || !code || code->empty() || !message ||
      message->empty() ||
      (*code != "configuration" && *code != "provider" &&
       *code != "invalid_output")) {
    return;
  }
  if (DaoDreamService* service = GetDreamService()) {
    service->OnDreamFailed(request_id, {*code, *message});
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
          self->RejectJavascriptCallback(
              base::Value(callback_id),
              base::Value(error.empty() ? "dream run failed" : error));
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

void DaoDreamRunnerHandler::HandleStartManualWeeklyDream(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 1 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  if (args.size() >= 2 && !args[1].is_dict()) {
    RejectJavascriptCallback(base::Value(callback_id),
                             base::Value("weekly_invalid_week_start"));
    return;
  }

  std::optional<std::string> week_start;
  if (args.size() >= 2) {
    const base::DictValue& params = args[1].GetDict();
    if (const base::Value* value = params.Find("weekStart")) {
      if (!value->is_string()) {
        RejectJavascriptCallback(base::Value(callback_id),
                                 base::Value("weekly_invalid_week_start"));
        return;
      }
      week_start = value->GetString();
    }
  }

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
          return;
        }
        self->RejectJavascriptCallback(base::Value(callback_id),
                                       base::Value(error));
      },
      weak_factory_.GetWeakPtr(), callback_id);
  if (week_start) {
    service->StartManualWeeklyDreamForWeekStart(*week_start,
                                                std::move(callback));
    return;
  }
  service->StartManualWeeklyDream(std::move(callback));
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
      base::BindRepeating(&DaoAgentDreamHandler::HandleGetUnviewedDreamReport,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "markDreamReportViewed",
      base::BindRepeating(&DaoAgentDreamHandler::HandleMarkDreamReportViewed,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openDreamReport",
      base::BindRepeating(&DaoAgentDreamHandler::HandleOpenDreamReport,
                          base::Unretained(this)));
}

void DaoAgentDreamHandler::HandleOpenDreamReport(const base::ListValue& args) {
  Browser* browser = FindLastActiveBrowserForMigration();
  if (!browser) {
    return;
  }
  GURL dream_url(std::string(content::kChromeUIScheme) + "://dream/");
  NavigateParams params(browser, dream_url, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
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

void DaoAgentSkillHandler::HandleGetSkillRegistry(const base::ListValue& args) {
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

void DaoAgentSkillHandler::HandleGetSkillContent(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id = args[1].is_string() ? args[1].GetString() : "";

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

void DaoAgentSkillHandler::HandleSaveUserSkill(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id = args[1].is_string() ? args[1].GetString() : "";
  const std::string skill_md = args[2].is_string() ? args[2].GetString() : "";
  const std::string host = args[3].is_string() ? args[3].GetString() : "";

  auto* service = GetSkillService();
  if (!service || skill_id.empty() || skill_md.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->SaveUserSkill(skill_id, skill_md, host,
                         base::BindOnce(
                             [](base::WeakPtr<DaoAgentSkillHandler> handler,
                                std::string cb_id, bool success) {
                               if (!handler) {
                                 return;
                               }
                               handler->ResolveJavascriptCallback(
                                   base::Value(cb_id), base::Value(success));
                             },
                             weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleDeleteUserSkill(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id = args[1].is_string() ? args[1].GetString() : "";

  auto* service = GetSkillService();
  if (!service || skill_id.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->DeleteUserSkill(skill_id,
                           base::BindOnce(
                               [](base::WeakPtr<DaoAgentSkillHandler> handler,
                                  std::string cb_id, bool success) {
                                 if (!handler) {
                                   return;
                                 }
                                 handler->ResolveJavascriptCallback(
                                     base::Value(cb_id), base::Value(success));
                               },
                               weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleOpenSkillsDirectory(
    const base::ListValue& args) {
  Profile* profile = Profile::FromWebUI(web_ui());
  base::FilePath skills_path = profile->GetPath().AppendASCII("DaoAgentSkills");
  platform_util::OpenItem(profile, skills_path, platform_util::OPEN_FOLDER,
                          platform_util::OpenOperationCallback());
}

void DaoAgentSkillHandler::HandleSetSkillDisabled(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string()) {
    return;
  }
  const std::string callback_id = args[0].GetString();
  const std::string skill_id = args[1].is_string() ? args[1].GetString() : "";
  const bool disabled = args[2].is_bool() ? args[2].GetBool() : false;

  auto* service = GetSkillService();
  if (!service || skill_id.empty()) {
    ResolveJavascriptCallback(base::Value(callback_id), base::Value(false));
    return;
  }

  service->SetSkillDisabled(skill_id, disabled,
                            base::BindOnce(
                                [](base::WeakPtr<DaoAgentSkillHandler> handler,
                                   std::string cb_id, bool success) {
                                  if (!handler) {
                                    return;
                                  }
                                  handler->ResolveJavascriptCallback(
                                      base::Value(cb_id), base::Value(success));
                                },
                                weak_factory_.GetWeakPtr(), callback_id));
}

void DaoAgentSkillHandler::HandleOpenSkillManager(const base::ListValue& args) {
  Browser* browser = FindLastActiveBrowserForMigration();
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

DaoAgentWorkspaceService* DaoAgentWorkspaceHandler::GetWorkspaceService() {
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
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceApplyPatch,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceList",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceList,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceOpenFolder",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceOpenFolder,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceGetRecentActivity",
      base::BindRepeating(
          &DaoAgentWorkspaceHandler::HandleWorkspaceGetRecentActivity,
          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "workspaceGetInfo",
      base::BindRepeating(&DaoAgentWorkspaceHandler::HandleWorkspaceGetInfo,
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
            body.Set("bytes_written", static_cast<int>(result->bytes_written));
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
            body.Set("bytes_written", static_cast<int>(result->bytes_written));
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

DaoIndexUI::DaoIndexUI(content::WebUI* web_ui) : WebUIController(web_ui) {
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

DaoMemoryUI::DaoMemoryUI(content::WebUI* web_ui) : WebUIController(web_ui) {
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

DaoDreamUI::DaoDreamUI(content::WebUI* web_ui) : WebUIController(web_ui) {
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

DaoAgentUI::DaoAgentUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, "agent");

  // Serve resource files from the GRD-generated resource map.
  source->AddResourcePaths(kDaoAgentResources);
  source->SetDefaultResource(IDR_DAO_AGENT_AGENT_HTML);
  source->AddString(
      "browser_tool_catalog_json",
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_DAO_AGENT_BROWSER_TOOL_CATALOG_JSON));

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
      network::mojom::CSPDirectiveName::ConnectSrc, "connect-src * 'self';");

  // Allow innerHTML usage (streaming markdown rendering).
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop lit-html;");

  // Register message handlers.
  web_ui->AddMessageHandler(std::make_unique<DaoAgentSettingsHandler>());
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

DaoSkillsUI::DaoSkillsUI(content::WebUI* web_ui) : WebUIController(web_ui) {
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
    request->site_for_cookies =
        net::SiteForCookies::FromOrigin(active_tab_origin);
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
      response.Set("error", "http " + base::NumberToString(status_code));
    }
  } else {
    response.Set("body", "");
    response.Set("ok", false);
    int net_error = loader->NetError();
    response.Set("error", "net error " + base::NumberToString(net_error));
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

void DaoAgentUIHandler::WriteDownloadedAndReply(const std::string& callback_id,
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
          [](base::WeakPtr<DaoAgentUIHandler> self, std::string cb_id,
             std::string path, std::string source_url, bool truncated,
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
            body.Set("bytes_written", static_cast<int>(result->bytes_written));
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
  std::string source = explicit_source
                           ? *explicit_source
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
            [](base::WeakPtr<DaoAgentUIHandler> self, std::string cb_id,
               std::string path, std::string source_url,
               DaoDevToolsClient::CommandResult result) {
              if (!self) {
                return;
              }
              if (!result.has_value()) {
                self->ResolvePageToolError(cb_id, result.error());
                return;
              }
              base::Value command_value = std::move(result).value();
              if (!command_value.is_dict()) {
                self->ResolveJavascriptCallback(base::Value(cb_id),
                                                DownloadErrorDict("io_error"));
                return;
              }
              auto* value =
                  command_value.GetDict().FindByDottedPath("result.value");
              if (!value || !value->is_string()) {
                self->ResolveJavascriptCallback(base::Value(cb_id),
                                                DownloadErrorDict("io_error"));
                return;
              }
              // No TruncateText: the body never goes through LLM
              // context — it goes straight to disk via the workspace
              // service. The workspace quota (per-file + total) is the
              // only ceiling we need here.
              std::string html = value->GetString();
              self->WriteDownloadedAndReply(cb_id, path, source_url,
                                            std::move(html),
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
    ResolveJavascriptCallback(base::Value(cb), DownloadErrorDict("io_error"));
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
          FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
          base::BindOnce(base::IgnoreResult(&base::DeleteFile), staging_path));
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
            body.Set("bytes_written", static_cast<int>(result->bytes_written));
            body.Set("created", result->created);
            body.Set("source_url", source_url);
            body.Set("truncated", false);
            self->ResolveJavascriptCallback(base::Value(cb_id), body);
          },
          weak_factory_.GetWeakPtr(), callback_id, workspace_path, source_url));
}

}  // namespace dao
