// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/webui/dao_home_ui.h"

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/strings/escape.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/uuid.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/grit/dao_home_resources.h"
#include "chrome/grit/dao_home_resources_map.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/home/dao_home_connector_executor.h"
#include "dao/browser/home/dao_home_history_material.h"
#include "dao/browser/home/dao_home_manifest.h"
#include "dao/browser/home/dao_home_project_service.h"
#include "dao/browser/home/dao_home_project_service_factory.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "third_party/blink/public/mojom/devtools/console_message.mojom-shared.h"
#include "ui/base/l10n/l10n_util.h"

namespace dao {
namespace {

constexpr char kHomeHost[] = "home";
constexpr char kHomeAppHost[] = "dao-home-app";
constexpr char kHomeAppUrl[] = "chrome-untrusted://dao-home-app/";
constexpr char kHomeConnectorUrl[] = "chrome-untrusted://dao-home-connector/";

size_t FindGeneratedRuntimeInjectionOffset(std::string_view body) {
  constexpr std::string_view kUtf8ByteOrderMark = "\xEF\xBB\xBF";
  constexpr std::string_view kDoctypePrefix = "<!doctype";

  size_t offset = 0;
  if (base::StartsWith(body, kUtf8ByteOrderMark,
                       base::CompareCase::SENSITIVE)) {
    offset = kUtf8ByteOrderMark.size();
  }
  while (offset < body.size() && base::IsAsciiWhitespace(body[offset])) {
    ++offset;
  }

  const std::string_view remaining = body.substr(offset);
  if (!base::StartsWith(remaining, kDoctypePrefix,
                        base::CompareCase::INSENSITIVE_ASCII)) {
    return offset;
  }
  const size_t after_prefix = offset + kDoctypePrefix.size();
  if (after_prefix < body.size() && body[after_prefix] != '>' &&
      !base::IsAsciiWhitespace(body[after_prefix])) {
    return offset;
  }

  char quote = 0;
  for (size_t index = after_prefix; index < body.size(); ++index) {
    const char character = body[index];
    if (quote != 0) {
      if (character == quote) {
        quote = 0;
      }
      continue;
    }
    if (character == '\'' || character == '"') {
      quote = character;
    } else if (character == '>') {
      return index + 1;
    }
  }
  return offset;
}

constexpr char kGeneratedActionRuntime[] = R"js(
(() => {
  if (window.parent === window) {
    return;
  }
  const parts = location.pathname.split('/').filter(Boolean);
  const revision = parts[parts[0] === 'preview' ? 1 : 0] ?? '';
  let nextRequestId = 0;
  window.addEventListener('click', event => {
    const action = event.target instanceof Element ?
        event.target.closest('[data-dao-action][data-dao-action-url]') : null;
    const feedItem = !action && event.target instanceof Element ?
        event.target.closest(
            '[data-dao-feed-link][data-dao-feed-url][data-dao-feed-source]') :
        null;
    const url = action?.dataset.daoActionUrl ?? feedItem?.dataset.daoFeedUrl;
    if (!url) {
      return;
    }
    event.preventDefault();
    event.stopImmediatePropagation();
    window.parent.postMessage({
      daoHome: 1,
      requestId: `${revision}:action:${++nextRequestId}`,
      revision,
      method: action ? 'navigation.openAction' : 'navigation.openFeedItem',
      params: action ? {actionId: action.dataset.daoAction, url} : {
        sourceId: feedItem.dataset.daoFeedSource,
        url,
      },
    }, 'dao://home');
  }, {capture: true});
})();
)js";
constexpr size_t kMaxRetainedConnectorExecutors = 16;
constexpr size_t kMaxBootstrapPermissionSelections = 3;
constexpr std::string_view kBootstrapPermissionRequestSuffix =
    "-permission-batch";
constexpr char16_t kHistoryBootstrapPreviewInspectionScript[] =
    uR"js(
({
  hasDirectExternalNavigation: [...document.querySelectorAll(
      'a[href], form[action], [formaction]')]
      .some(node => {
        const raw = node.matches('a[href]') ? node.getAttribute('href') :
            node.hasAttribute('formaction') ? node.getAttribute('formaction') :
                                              node.getAttribute('action');
        try {
          const destination = new URL(raw, document.baseURI);
          return destination.protocol === 'http:' ||
              destination.protocol === 'https:';
        } catch {
          return false;
        }
      }),
  hasFeed: [...document.querySelectorAll('[data-dao-feed]')]
      .some(node => {
        const rect = node.getBoundingClientRect();
        const style = getComputedStyle(node);
        return (node.textContent ?? '').trim().length > 0 && rect.width > 0 &&
            rect.height > 0 && style.visibility !== 'hidden' &&
            style.display !== 'none';
      }),
  actions: [...document.querySelectorAll('[data-dao-action]')].map(node => ({
    id: node.getAttribute('data-dao-action'),
    url: node.getAttribute('data-dao-action-url'),
    focusable: node.matches('button:not([disabled]), [tabindex]:not([tabindex="-1"])'),
    visible: (() => {
      const rect = node.getBoundingClientRect();
      const style = getComputedStyle(node);
      return rect.width > 0 && rect.height > 0 && rect.bottom > 0 &&
          rect.top < innerHeight && style.visibility !== 'hidden' &&
          style.display !== 'none';
    })(),
  })),
  sourceSlots: [...document.querySelectorAll('[data-dao-source-slot]')].map(node => ({
    id: node.getAttribute('data-dao-source-slot'),
    connectorId: node.getAttribute('data-dao-connector'),
  })),
})
)js";

base::Value ConnectorError(std::string code, std::string message) {
  return base::Value(base::DictValue()
                         .Set("error", std::move(message))
                         .Set("code", std::move(code)));
}

void IgnoreDiscardResult(base::expected<void, HomeError>) {}

void DiscardDraftWithoutReply(base::WeakPtr<DaoHomeProjectService> service,
                              const std::string& draft_id) {
  if (service) {
    service->DiscardDraft(draft_id, base::BindOnce(&IgnoreDiscardResult));
  }
}

base::DictValue SnapshotToValue(const HomeSnapshot& snapshot) {
  base::DictValue value;
  value.Set("hasProject", snapshot.has_project);
  value.Set("revision", snapshot.revision);
  value.Set("entry", snapshot.manifest ? snapshot.manifest->entry : "");
  base::ListValue connectors;
  if (snapshot.manifest) {
    for (const HomeConnector& connector : snapshot.manifest->connectors) {
      base::ListValue origins;
      for (const url::Origin& origin : connector.permissions.origins) {
        origins.Append(origin.Serialize());
      }
      connectors.Append(
          base::DictValue()
              .Set("id", connector.id)
              .Set("origins", std::move(origins))
              .Set("granted", std::ranges::find(snapshot.granted_connector_ids,
                                                connector.id) !=
                                  snapshot.granted_connector_ids.end()));
    }
  }
  value.Set("connectors", std::move(connectors));
  return value;
}

base::DictValue VersionToValue(const HomeVersion& version) {
  base::ListValue changed_files;
  for (const std::string& path : version.changed_files) {
    changed_files.Append(path);
  }
  return base::DictValue()
      .Set("id", version.id)
      .Set("parent", version.parent)
      .Set("restoredFrom", version.restored_from)
      .Set("summary", version.summary)
      .Set("kind", HomeRevisionKindToString(version.kind))
      .Set("createdAtMs", static_cast<double>(version.created_at_ms))
      .Set("changedFiles", std::move(changed_files));
}

base::DictValue LimitsToValue(const HomeLimits& limits) {
  return base::DictValue()
      .Set("maxResultBytes", static_cast<double>(limits.max_result_bytes))
      .Set("maxItemsPerConnector", limits.max_items_per_connector);
}

base::DictValue PermissionToValue(const HomePermissionRequest& request) {
  base::ListValue origins;
  for (const url::Origin& origin : request.connector.permissions.origins) {
    origins.Append(origin.Serialize());
  }
  base::ListValue paths;
  for (const std::string& path : request.connector.permissions.paths) {
    paths.Append(path);
  }
  base::ListValue capabilities;
  for (HomePageCapability capability :
       request.connector.permissions.capabilities) {
    capabilities.Append(HomePageCapabilityToString(capability));
  }
  base::DictValue value =
      base::DictValue()
          .Set("kind", "single")
          .Set("id", request.id)
          .Set("draftId", request.draft_id)
          .Set("baseRevision", request.base_revision)
          .Set("connectorId", request.connector.id)
          .Set("origins", std::move(origins))
          .Set("paths", std::move(paths))
          .Set("capabilities", std::move(capabilities))
          .Set("mode", "read")
          .Set("requestedLimits", LimitsToValue(request.requested_limits));
  if (request.previous_limits) {
    value.Set("previousLimits", LimitsToValue(*request.previous_limits));
  }
  return value;
}

base::DictValue BootstrapPermissionItemToValue(
    const HomePermissionBatchItem& item) {
  base::ListValue origins;
  for (const url::Origin& origin : item.connector.permissions.origins) {
    origins.Append(origin.Serialize());
  }
  base::ListValue paths;
  for (const std::string& path : item.connector.permissions.paths) {
    paths.Append(path);
  }
  base::ListValue capabilities;
  for (HomePageCapability capability :
       item.connector.permissions.capabilities) {
    capabilities.Append(HomePageCapabilityToString(capability));
  }
  base::DictValue value =
      base::DictValue()
          .Set("connectorId", item.connector_id)
          .Set("label", item.label)
          .Set("origins", std::move(origins))
          .Set("paths", std::move(paths))
          .Set("capabilities", std::move(capabilities))
          .Set("mode", "read")
          .Set("requestedLimits", LimitsToValue(item.requested_limits))
          .Set("authenticationMayBeRequired",
               item.authentication_may_be_required);
  if (item.previous_limits) {
    value.Set("previousLimits", LimitsToValue(*item.previous_limits));
  }
  return value;
}

base::DictValue BootstrapPermissionToValue(
    const HomePermissionBatchRequest& request) {
  base::ListValue items;
  for (const HomePermissionBatchItem& item : request.items) {
    items.Append(BootstrapPermissionItemToValue(item));
  }
  return base::DictValue()
      .Set("kind", "batch")
      .Set("id", request.id)
      .Set("draftId", request.draft_id)
      .Set("baseRevision", request.base_revision)
      .Set("items", std::move(items));
}

bool IsValidBootstrapPermissionRequestId(std::string_view request_id) {
  if (!base::EndsWith(request_id, kBootstrapPermissionRequestSuffix)) {
    return false;
  }
  const std::string_view transaction_id = request_id.substr(
      0, request_id.size() - kBootstrapPermissionRequestSuffix.size());
  return base::Uuid::ParseLowercase(transaction_id).is_valid();
}

bool IsValidBootstrapConnectorId(std::string_view connector_id) {
  if (connector_id.empty() || connector_id.size() > 64) {
    return false;
  }
  return std::ranges::all_of(connector_id, [](char value) {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '_' || value == '-';
  });
}

struct ProjectResourcePath {
  bool preview = false;
  std::string id;
  std::string resource;
};

std::optional<ProjectResourcePath> ParseResourcePath(std::string_view path) {
  std::string normalized(path);
  if (const size_t query = normalized.find('?'); query != std::string::npos) {
    normalized.resize(query);
  }
  constexpr std::string_view preview_prefix = "preview/";
  const bool preview = base::StartsWith(normalized, preview_prefix);
  const size_t id_start = preview ? preview_prefix.size() : 0;
  const size_t separator = normalized.find('/', id_start);
  if (separator == std::string::npos || separator == id_start ||
      separator + 1 >= normalized.size()) {
    return std::nullopt;
  }
  std::string id = normalized.substr(id_start, separator - id_start);
  std::string resource = normalized.substr(separator + 1);
  if (!base::Uuid::ParseLowercase(id).is_valid() ||
      !IsValidHomeRelativePath(resource)) {
    return std::nullopt;
  }
  return ProjectResourcePath{preview, std::move(id), std::move(resource)};
}

bool IsExpectedAgentPreviewFrame(content::RenderFrameHost* frame,
                                 content::WebContents* contents,
                                 const std::string& draft_id,
                                 const std::string& entry) {
  if (!frame || !frame->IsRenderFrameLive() ||
      content::WebContents::FromRenderFrameHost(frame) != contents) {
    return false;
  }
  const GURL& url = frame->GetLastCommittedURL();
  if (!url.SchemeIs(content::kChromeUIUntrustedScheme) ||
      url.host() != kHomeAppHost || url.has_query() || url.has_ref() ||
      url.path().size() < 2) {
    return false;
  }
  std::optional<ProjectResourcePath> parsed =
      ParseResourcePath(url.path().substr(1));
  return parsed && parsed->preview && parsed->id == draft_id &&
         base::UnescapeURLComponent(
             parsed->resource,
             base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
                 base::UnescapeRule::PATH_SEPARATORS |
                 base::UnescapeRule::SPACES) == entry;
}

}  // namespace

WEB_UI_CONTROLLER_TYPE_IMPL(DaoHomeUI)

DaoHomeUIConfig::DaoHomeUIConfig()
    : DefaultWebUIConfig(content::kChromeUIScheme, kHomeHost) {}

DaoHomeUIConfig::~DaoHomeUIConfig() = default;

bool DaoHomeUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  return !Profile::FromBrowserContext(browser_context)->IsOffTheRecord();
}

DaoHomeAppUIConfig::DaoHomeAppUIConfig()
    : DefaultWebUIConfig(content::kChromeUIUntrustedScheme, kHomeAppHost) {}

DaoHomeAppUIConfig::~DaoHomeAppUIConfig() = default;

bool DaoHomeAppUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return !Profile::FromBrowserContext(browser_context)->IsOffTheRecord();
}

DaoHomeConnectorUIConfig::DaoHomeConnectorUIConfig()
    : DefaultWebUIConfig(content::kChromeUIUntrustedScheme,
                         "dao-home-connector") {}

DaoHomeConnectorUIConfig::~DaoHomeConnectorUIConfig() = default;

bool DaoHomeConnectorUIConfig::IsWebUIEnabled(
    content::BrowserContext* browser_context) {
  return !Profile::FromBrowserContext(browser_context)->IsOffTheRecord();
}

DaoHomeUIHandler::DaoHomeUIHandler(DaoHomeProjectService* service)
    : service_(service),
      connector_executor_(std::make_unique<DaoHomeConnectorExecutor>()),
      draft_connector_executor_(std::make_unique<DaoHomeConnectorExecutor>()) {}

DaoHomeUIHandler::~DaoHomeUIHandler() {
  InvalidateDocumentMutationLease();
  CancelAllSessions();
}

void DaoHomeUIHandler::RegisterMessages() {
  Observe(web_ui()->GetWebContents());
  web_ui()->RegisterMessageCallback(
      "getHomeSnapshot",
      base::BindRepeating(&DaoHomeUIHandler::HandleGetSnapshot,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getHomeVersions",
      base::BindRepeating(&DaoHomeUIHandler::HandleGetVersions,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getHomeFiles", base::BindRepeating(&DaoHomeUIHandler::HandleGetFiles,
                                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "readHomeFile", base::BindRepeating(&DaoHomeUIHandler::HandleReadFile,
                                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "getHomePermission",
      base::BindRepeating(&DaoHomeUIHandler::HandleGetPermission,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "approveHomePermission",
      base::BindRepeating(&DaoHomeUIHandler::HandleApprovePermission,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelHomePermission",
      base::BindRepeating(&DaoHomeUIHandler::HandleCancelPermission,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "resolveHomeBootstrapPermission",
      base::BindRepeating(&DaoHomeUIHandler::HandleResolveBootstrapPermission,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openHomeAgent", base::BindRepeating(&DaoHomeUIHandler::HandleOpenAgent,
                                           base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "rollbackHome", base::BindRepeating(&DaoHomeUIHandler::HandleRollback,
                                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "resetHome", base::BindRepeating(&DaoHomeUIHandler::HandleReset,
                                       base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "exportHome", base::BindRepeating(&DaoHomeUIHandler::HandleExport,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "importHome", base::BindRepeating(&DaoHomeUIHandler::HandleImport,
                                        base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startHomeConnector",
      base::BindRepeating(&DaoHomeUIHandler::HandleStartConnector,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "startHomeDraftConnector",
      base::BindRepeating(&DaoHomeUIHandler::HandleStartDraftConnector,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "callHomeConnectorPage",
      base::BindRepeating(&DaoHomeUIHandler::HandleCallConnectorPage,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "finishHomeConnector",
      base::BindRepeating(&DaoHomeUIHandler::HandleFinishConnector,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "resolveHomeMedia",
      base::BindRepeating(&DaoHomeUIHandler::HandleResolveMedia,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "completeHomeAgentConnector",
      base::BindRepeating(&DaoHomeUIHandler::HandleCompleteAgentConnector,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "notifyHomeAgentPreviewLoaded",
      base::BindRepeating(&DaoHomeUIHandler::HandleNotifyAgentPreviewLoaded,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "recordHomeRuntimeError",
      base::BindRepeating(&DaoHomeUIHandler::HandleRecordRuntimeError,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "cancelHomeSession",
      base::BindRepeating(&DaoHomeUIHandler::HandleCancelSession,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setHomeSelection",
      base::BindRepeating(&DaoHomeUIHandler::HandleSetSelection,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "openHomeNavigation",
      base::BindRepeating(&DaoHomeUIHandler::HandleOpenNavigation,
                          base::Unretained(this)));
  project_changed_subscription_ =
      service_->AddProjectChangedObserver(base::BindRepeating(
          &DaoHomeUIHandler::OnProjectChanged, base::Unretained(this)));
  permission_subscription_ =
      service_->AddPermissionObserver(base::BindRepeating(
          &DaoHomeUIHandler::OnPermissionChanged, base::Unretained(this)));
  bootstrap_permission_subscription_ = service_->AddBootstrapPermissionObserver(
      base::BindRepeating(&DaoHomeUIHandler::OnBootstrapPermissionChanged,
                          base::Unretained(this)));
}

void DaoHomeUIHandler::CollectConnectorForAgent(std::string draft_id,
                                                std::string connector_id,
                                                base::Value input,
                                                ConnectorCallback callback) {
  if (!IsJavascriptAllowed() || !HasActiveHomeOwner()) {
    std::move(callback).Run(
        ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  if (agent_connector_callback_) {
    std::move(callback).Run(
        ConnectorError("temporarily_unavailable",
                       "Another Agent connector sample is already running."));
    return;
  }
  agent_connector_request_id_ =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  agent_connector_callback_ = std::move(callback);
  agent_connector_timeout_.Start(
      FROM_HERE, base::Seconds(25),
      base::BindOnce(&DaoHomeUIHandler::CancelAgentConnector,
                     weak_factory_.GetWeakPtr(), "timed_out",
                     "The Agent connector sample timed out."));
  FireWebUIListener(
      "dao-home-agent-connector-request",
      base::Value(base::DictValue()
                      .Set("requestId", agent_connector_request_id_)
                      .Set("draftId", std::move(draft_id))
                      .Set("connectorId", std::move(connector_id))
                      .Set("input", std::move(input))));
}

void DaoHomeUIHandler::PreviewDraftForAgent(
    std::string draft_id,
    std::string entry,
    HomePreviewRequirements requirements,
    PreviewCallback callback) {
  if (!IsJavascriptAllowed() || !HasActiveHomeOwner()) {
    std::move(callback).Run(
        ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  if (agent_preview_callback_) {
    std::move(callback).Run(ConnectorError(
        "temporarily_unavailable", "Another Home preview is already running."));
    return;
  }
  if (!base::Uuid::ParseLowercase(draft_id).is_valid() ||
      !IsValidHomeRelativePath(entry)) {
    std::move(callback).Run(
        ConnectorError("invalid_draft", "The Home preview is invalid."));
    return;
  }
  agent_preview_request_id_ =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  agent_preview_draft_id_ = draft_id;
  agent_preview_entry_ = entry;
  agent_preview_requirements_ = std::move(requirements);
  agent_preview_frame_tree_node_id_ = {};
  agent_preview_runtime_failed_ = false;
  agent_preview_callback_ = std::move(callback);
  service_->BeginDraftPreview(draft_id);
  agent_preview_timeout_.Start(
      FROM_HERE, base::Seconds(15),
      base::BindOnce(&DaoHomeUIHandler::CancelAgentPreview,
                     weak_factory_.GetWeakPtr(), "timed_out",
                     "The Home preview timed out."));
  FireWebUIListener("dao-home-agent-preview-request",
                    base::Value(base::DictValue()
                                    .Set("requestId", agent_preview_request_id_)
                                    .Set("draftId", std::move(draft_id))
                                    .Set("entry", std::move(entry))));
}

void DaoHomeUIHandler::HandleGetSnapshot(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  service_->GetSnapshot(base::BindOnce(&DaoHomeUIHandler::ReplySnapshot,
                                       weak_factory_.GetWeakPtr(),
                                       args[0].GetString()));
}

void DaoHomeUIHandler::HandleGetVersions(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  service_->ListVersions(base::BindOnce(&DaoHomeUIHandler::ReplyVersions,
                                        weak_factory_.GetWeakPtr(),
                                        args[0].GetString()));
}

void DaoHomeUIHandler::HandleGetFiles(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  service_->ListFiles(
      args[1].GetString(),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::expected<std::vector<std::string>, HomeError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError("read_failed", "Unable to list Home files."));
              return;
            }
            base::ListValue files;
            for (std::string& path : result.value()) {
              files.Append(std::move(path));
            }
            self->FireWebUIListener(callback_event,
                                    base::Value(std::move(files)));
          },
          weak_factory_.GetWeakPtr(), callback_event));
}

void DaoHomeUIHandler::HandleReadFile(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  service_->ReadFile(
      args[1].GetString(), args[2].GetString(),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::expected<std::string, HomeError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError("read_failed", "Unable to read Home file."));
              return;
            }
            self->FireWebUIListener(callback_event,
                                    base::Value(std::move(result.value())));
          },
          weak_factory_.GetWeakPtr(), callback_event));
}

void DaoHomeUIHandler::HandleGetPermission(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  if (HasActiveHomeOwner()) {
    std::optional<HomePermissionBatchRequest> bootstrap_request =
        service_->GetPendingBootstrapPermission(web_ui()->GetWebContents());
    if (bootstrap_request) {
      FireWebUIListener(
          args[0].GetString(),
          base::Value(BootstrapPermissionToValue(*bootstrap_request)));
      return;
    }
  }
  std::optional<HomePermissionRequest> request =
      service_->GetPendingPermission();
  FireWebUIListener(
      args[0].GetString(),
      request ? base::Value(PermissionToValue(*request)) : base::Value());
}

void DaoHomeUIHandler::HandleApprovePermission(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  if (!HasActiveHomeOwner()) {
    FireWebUIListener(callback_event,
                      ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  service_->ApprovePermission(
      args[1].GetString(),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::expected<void, HomeError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError("approval_failed",
                                 "Unable to approve the source scope."));
              return;
            }
            self->FireWebUIListener(callback_event, base::Value(true));
          },
          weak_factory_.GetWeakPtr(), callback_event));
}

void DaoHomeUIHandler::HandleCancelPermission(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const bool cancelled = service_->CancelPermission(args[1].GetString());
  FireWebUIListener(args[0].GetString(), base::Value(cancelled));
}

void DaoHomeUIHandler::HandleResolveBootstrapPermission(
    const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  if (args.size() != 3 || !args[1].is_string() || !args[2].is_list() ||
      !HasActiveHomeOwner() ||
      !IsValidBootstrapPermissionRequestId(args[1].GetString())) {
    FireWebUIListener(
        callback_event,
        ConnectorError("invalid_argument",
                       "Invalid Home bootstrap permission decision."));
    return;
  }

  const base::ListValue& selected_values = args[2].GetList();
  if (selected_values.size() > kMaxBootstrapPermissionSelections) {
    FireWebUIListener(
        callback_event,
        ConnectorError("invalid_argument",
                       "Invalid Home bootstrap permission decision."));
    return;
  }
  base::flat_set<std::string> selected_connector_ids;
  for (const base::Value& selected_value : selected_values) {
    if (!selected_value.is_string() ||
        !IsValidBootstrapConnectorId(selected_value.GetString()) ||
        !selected_connector_ids.insert(selected_value.GetString()).second) {
      FireWebUIListener(
          callback_event,
          ConnectorError("invalid_argument",
                         "Invalid Home bootstrap permission decision."));
      return;
    }
  }

  content::WebContents* owner = web_ui()->GetWebContents();
  std::optional<HomePermissionBatchRequest> request =
      service_->GetPendingBootstrapPermission(owner);
  if (!request || request->id != args[1].GetString()) {
    FireWebUIListener(
        callback_event,
        ConnectorError("not_found",
                       "No matching Home bootstrap permission is pending."));
    return;
  }
  base::flat_set<std::string> available_connector_ids;
  for (const HomePermissionBatchItem& item : request->items) {
    available_connector_ids.insert(item.connector_id);
  }
  if (!std::ranges::all_of(selected_connector_ids,
                           [&available_connector_ids](const std::string& id) {
                             return available_connector_ids.contains(id);
                           })) {
    FireWebUIListener(
        callback_event,
        ConnectorError("invalid_argument",
                       "Invalid Home bootstrap permission decision."));
    return;
  }

  service_->ResolveBootstrapPermissions(
      owner, request->id, std::move(selected_connector_ids),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::expected<void, HomeError> result) {
            if (!self) {
              return;
            }
            if (!self->HasActiveHomeOwner()) {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError("cancelled", "Dao Home is not active."));
              return;
            }
            if (!result.has_value()) {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError(
                      "approval_failed",
                      "Unable to resolve the Home source permissions."));
              return;
            }
            self->FireWebUIListener(callback_event, base::Value(true));
          },
          weak_factory_.GetWeakPtr(), callback_event));
}

void DaoHomeUIHandler::HandleOpenAgent(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string() || !HasActiveHomeOwner()) {
    return;
  }
  const std::string& mode = args[0].GetString();
  if (mode != "create" && mode != "history" && mode != "repair") {
    return;
  }
  if (mode == "history") {
    QueryHistoryAndOpenAgent();
    return;
  }
  OpenAgentWithMode(mode);
}

void DaoHomeUIHandler::QueryHistoryAndOpenAgent() {
  Profile* profile = Profile::FromWebUI(web_ui());
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    std::string claim_token =
        base::Uuid::GenerateRandomV4().AsLowercaseString();
    HomeBootstrapBrief brief;
    brief.locale = g_browser_process->GetApplicationLocale();
    service_->SetHistoryBootstrapBrief(web_ui()->GetWebContents()->GetWeakPtr(),
                                       claim_token, std::move(brief));
    OpenAgentWithMode("history", std::move(claim_token));
    return;
  }
  const base::Time now = base::Time::Now();
  history::QueryOptions options;
  options.begin_time = now - base::Days(30);
  options.end_time = now;
  options.max_count = 0;
  options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
  const std::string locale = g_browser_process->GetApplicationLocale();
  history->QueryHistory(
      std::u16string(), options,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, base::Time now,
             std::string locale, history::QueryResults results) {
            if (!self || !self->HasActiveHomeOwner()) {
              return;
            }
            std::string claim_token =
                base::Uuid::GenerateRandomV4().AsLowercaseString();
            self->service_->SetHistoryBootstrapBrief(
                self->web_ui()->GetWebContents()->GetWeakPtr(), claim_token,
                BuildHomeBootstrapBrief(results, now, std::move(locale)));
            self->OpenAgentWithMode("history", std::move(claim_token));
          },
          weak_factory_.GetWeakPtr(), now, locale),
      &history_tracker_);
}

void DaoHomeUIHandler::OpenAgentWithMode(const std::string& mode,
                                         std::string history_claim_token) {
  if (!HasActiveHomeOwner()) {
    service_->ClearHistoryBootstrapForClaim(history_claim_token);
    return;
  }
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  if (!browser_view || !browser_view->dao_agent_sidebar()) {
    service_->ClearHistoryBootstrapForClaim(history_claim_token);
    return;
  }
  const int prompt_id = mode == "history"  ? IDS_DAO_HOME_HISTORY_AGENT_PROMPT
                        : mode == "repair" ? IDS_DAO_HOME_REPAIR_AGENT_PROMPT
                                           : IDS_DAO_HOME_CREATE_AGENT_PROMPT;
  base::OnceClosure on_prompt_abandoned;
  if (!history_claim_token.empty()) {
    on_prompt_abandoned = base::BindOnce(
        [](base::WeakPtr<DaoHomeProjectService> service,
           std::string claim_token) {
          if (service) {
            service->ClearHistoryBootstrapForClaim(claim_token);
          }
        },
        service_->GetWeakPtr(), history_claim_token);
  }
  browser_view->dao_agent_sidebar()->ExpandAndSubmitPrompt(
      l10n_util::GetStringUTF16(prompt_id), /*include_page_context=*/false,
      std::move(history_claim_token), std::move(on_prompt_abandoned));
  browser_view->InvalidateLayout();
}

void DaoHomeUIHandler::HandleRollback(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  scoped_refptr<DaoHomeMutationLease> authorization = CreateMutationLease();
  if (!authorization) {
    FireWebUIListener(callback_event,
                      ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  service_->Rollback(
      args[1].GetString(), args[2].GetString(), "Restore Home version",
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::expected<HomeVersion, HomeError> result) {
            if (!self) {
              return;
            }
            if (result.has_value()) {
              self->ReplyAfterMutation(callback_event);
            } else {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError("rollback_failed",
                                 "Unable to restore the Home version."));
            }
          },
          weak_factory_.GetWeakPtr(), callback_event),
      std::move(authorization));
}

void DaoHomeUIHandler::HandleReset(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  scoped_refptr<DaoHomeMutationLease> authorization = CreateMutationLease();
  if (!authorization) {
    FireWebUIListener(callback_event,
                      ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  CancelAllSessions();
  service_->Reset(
      args[1].GetString(),
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::expected<void, HomeError> result) {
            if (!self) {
              return;
            }
            if (result.has_value()) {
              self->ReplyAfterMutation(callback_event);
            } else {
              self->FireWebUIListener(
                  callback_event,
                  ConnectorError("reset_failed",
                                 "Unable to reset the Home project."));
            }
          },
          weak_factory_.GetWeakPtr(), callback_event),
      std::move(authorization));
}

void DaoHomeUIHandler::HandleExport(const base::ListValue& args) {
  AllowJavascript();
  if (args.empty() || !args[0].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  service_->ExportProject(base::BindOnce(
      [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
         base::expected<std::string, HomeError> result) {
        if (self) {
          self->FireWebUIListener(callback_event,
                                  base::Value(result.value_or("")));
        }
      },
      weak_factory_.GetWeakPtr(), callback_event));
}

void DaoHomeUIHandler::HandleImport(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  const std::string package_json = args[1].GetString();
  scoped_refptr<DaoHomeMutationLease> authorization = CreateMutationLease();
  if (!authorization) {
    FireWebUIListener(callback_event,
                      ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  base::WeakPtr<DaoHomeProjectService> service = service_->GetWeakPtr();
  service_->GetSnapshot(base::BindOnce(
      [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
         std::string package_json,
         scoped_refptr<DaoHomeMutationLease> authorization,
         base::WeakPtr<DaoHomeProjectService> service, HomeSnapshot snapshot) {
        if (!self) {
          return;
        }
        if (!authorization->IsValid()) {
          self->FireWebUIListener(
              callback_event,
              ConnectorError("cancelled", "Dao Home is not active."));
          return;
        }
        self->service_->ImportProject(
            snapshot.revision, package_json, "Import Home project",
            base::BindOnce(
                [](base::WeakPtr<DaoHomeUIHandler> self,
                   std::string callback_event,
                   scoped_refptr<DaoHomeMutationLease> authorization,
                   base::WeakPtr<DaoHomeProjectService> service,
                   base::expected<HomeDraft, HomeError> draft) {
                  if (!self) {
                    if (draft.has_value()) {
                      DiscardDraftWithoutReply(service, draft->id);
                    }
                    return;
                  }
                  if (!draft.has_value()) {
                    self->FireWebUIListener(
                        callback_event,
                        ConnectorError("import_failed",
                                       "Unable to import the Home project."));
                    return;
                  }
                  HomeDraft imported_draft = std::move(draft.value());
                  if (!authorization->IsValid()) {
                    self->DiscardDraftAndReply(
                        imported_draft.id, std::move(callback_event),
                        ConnectorError("cancelled", "Dao Home is not active."));
                    return;
                  }
                  self->PreviewDraftForAgent(
                      imported_draft.id, imported_draft.manifest.entry,
                      HomePreviewRequirements(),
                      base::BindOnce(
                          [](base::WeakPtr<DaoHomeUIHandler> self,
                             std::string callback_event,
                             scoped_refptr<DaoHomeMutationLease> authorization,
                             base::WeakPtr<DaoHomeProjectService> service,
                             HomeDraft imported_draft,
                             base::Value preview_result) {
                            if (!self) {
                              DiscardDraftWithoutReply(service,
                                                       imported_draft.id);
                              return;
                            }
                            const base::DictValue* preview =
                                preview_result.GetIfDict();
                            if (!authorization->IsValid()) {
                              self->DiscardDraftAndReply(
                                  imported_draft.id, std::move(callback_event),
                                  ConnectorError("cancelled",
                                                 "Dao Home is not active."));
                              return;
                            }
                            if (!preview ||
                                !preview->FindBool("valid").value_or(false)) {
                              self->DiscardDraftAndReply(
                                  imported_draft.id, std::move(callback_event),
                                  std::move(preview_result));
                              return;
                            }
                            self->service_->PublishPreviewedDraft(
                                imported_draft, HomeRevisionKind::kImport,
                                base::BindOnce(
                                    [](base::WeakPtr<DaoHomeUIHandler> self,
                                       std::string callback_event,
                                       std::string draft_id,
                                       base::WeakPtr<DaoHomeProjectService>
                                           service,
                                       base::expected<HomeVersion, HomeError>
                                           result) {
                                      if (!self) {
                                        if (!result.has_value()) {
                                          DiscardDraftWithoutReply(service,
                                                                   draft_id);
                                        }
                                        return;
                                      }
                                      if (result.has_value()) {
                                        self->ReplyAfterMutation(
                                            callback_event);
                                      } else {
                                        self->DiscardDraftAndReply(
                                            std::move(draft_id),
                                            std::move(callback_event),
                                            ConnectorError(
                                                "import_failed",
                                                "Unable to publish the "
                                                "imported Home project."));
                                      }
                                    },
                                    self, callback_event, imported_draft.id,
                                    service),
                                authorization);
                          },
                          self, callback_event, authorization, service,
                          std::move(imported_draft)));
                },
                self, callback_event, authorization, service));
      },
      weak_factory_.GetWeakPtr(), callback_event, package_json, authorization,
      service));
}

bool DaoHomeUIHandler::HasActiveHomeOwner() {
  content::WebContents* owner = web_ui()->GetWebContents();
  Browser* browser = owner ? chrome::FindBrowserWithTab(owner) : nullptr;
  return owner && browser &&
         owner->GetVisibility() != content::Visibility::HIDDEN &&
         browser->tab_strip_model()->GetActiveWebContents() == owner &&
         owner->GetLastCommittedURL().SchemeIs(content::kChromeUIScheme) &&
         owner->GetLastCommittedURL().host() == kHomeHost;
}

void DaoHomeUIHandler::HandleStartConnector(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  const std::string revision = args[1].GetString();
  const std::string connector_id = args[2].GetString();
  base::Value input = args[3].Clone();
  if (!HasActiveHomeOwner()) {
    ReplyConnectorWithDiagnostic(
        callback_event, revision, connector_id, "start",
        ConnectorError("cancelled", "Dao Home is not active."));
    return;
  }
  base::WeakPtr<content::WebContents> owner =
      web_ui()->GetWebContents()->GetWeakPtr();
  service_->GetConnectorBundle(
      revision, connector_id,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             std::string revision, base::WeakPtr<content::WebContents> owner,
             base::Value input, std::string connector_id,
             base::expected<HomeConnectorBundle, HomeError> bundle) {
            if (!self) {
              return;
            }
            if (!owner || !self->HasActiveHomeOwner() ||
                self->web_ui()->GetWebContents() != owner.get()) {
              self->ReplyConnectorWithDiagnostic(
                  callback_event, revision, connector_id, "start",
                  ConnectorError("cancelled", "Dao Home is not active."));
              return;
            }
            if (!bundle.has_value()) {
              self->ReplyConnectorWithDiagnostic(
                  callback_event, revision, connector_id, "start",
                  ConnectorError(
                      "stale_revision",
                      "The Home connector revision is unavailable."));
              return;
            }
            if (!bundle->granted) {
              self->ReplyConnectorWithDiagnostic(
                  callback_event, revision, connector_id, "start",
                  ConnectorError("permission_required",
                                 "This Home source has not been connected."));
              return;
            }
            Profile* profile = Profile::FromWebUI(self->web_ui());
            self->PrepareConnectorExecutorForStart();
            self->connector_executor_->Start(
                owner.get(), profile, std::move(revision),
                std::move(bundle->connector), bundle->limits,
                std::move(bundle->module_source),
                std::move(bundle->schema_source), std::move(input),
                base::BindOnce(&DaoHomeUIHandler::ReplyConnectorWithDiagnostic,
                               self, std::move(callback_event), revision,
                               std::move(connector_id), "start"));
          },
          weak_factory_.GetWeakPtr(), callback_event, revision,
          std::move(owner), std::move(input), connector_id));
}

void DaoHomeUIHandler::HandleStartDraftConnector(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  const std::string draft_id = args[1].GetString();
  const std::string connector_id = args[2].GetString();
  base::Value input = args[3].Clone();
  if (!HasActiveHomeOwner() ||
      !service_->IsDraftConnectorApproved(draft_id, connector_id)) {
    ReplyConnectorWithDiagnostic(
        callback_event, draft_id, connector_id, "draft_start",
        ConnectorError("permission_required",
                       "This exact Home draft source has not been approved."));
    return;
  }
  base::WeakPtr<content::WebContents> owner =
      web_ui()->GetWebContents()->GetWeakPtr();
  service_->GetApprovedDraftConnectorBundle(
      draft_id, connector_id,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             std::string draft_id, base::WeakPtr<content::WebContents> owner,
             base::Value input, std::string connector_id,
             base::expected<HomeConnectorBundle, HomeError> bundle) {
            if (!self) {
              return;
            }
            if (!owner || !self->HasActiveHomeOwner() ||
                self->web_ui()->GetWebContents() != owner.get() ||
                !self->service_->IsDraftConnectorApproved(draft_id,
                                                          connector_id)) {
              self->ReplyConnectorWithDiagnostic(
                  callback_event, draft_id, connector_id, "draft_start",
                  ConnectorError("cancelled",
                                 "The Home draft approval expired."));
              return;
            }
            if (!bundle.has_value()) {
              self->ReplyConnectorWithDiagnostic(
                  callback_event, draft_id, connector_id, "draft_start",
                  ConnectorError("invalid_draft",
                                 "The Home draft connector is unavailable."));
              return;
            }
            self->service_->GetSnapshot(base::BindOnce(
                [](base::WeakPtr<DaoHomeUIHandler> self,
                   std::string callback_event, std::string draft_id,
                   base::WeakPtr<content::WebContents> owner, base::Value input,
                   std::string connector_id, HomeConnectorBundle bundle,
                   HomeSnapshot snapshot) {
                  if (!self || !owner || !self->HasActiveHomeOwner() ||
                      self->web_ui()->GetWebContents() != owner.get() ||
                      !self->service_->IsDraftConnectorApproved(draft_id,
                                                                connector_id)) {
                    if (self) {
                      self->ReplyConnectorWithDiagnostic(
                          callback_event, snapshot.revision, connector_id,
                          "draft_start",
                          ConnectorError("cancelled",
                                         "The Home draft approval expired."));
                    }
                    return;
                  }
                  Profile* profile = Profile::FromWebUI(self->web_ui());
                  self->PrepareDraftConnectorExecutorForStart();
                  self->draft_connector_executor_->Start(
                      owner.get(), profile, std::move(snapshot.revision),
                      std::move(bundle.connector), bundle.limits,
                      std::move(bundle.module_source),
                      std::move(bundle.schema_source), std::move(input),
                      base::BindOnce(
                          &DaoHomeUIHandler::ReplyConnectorWithDiagnostic, self,
                          std::move(callback_event), draft_id,
                          std::move(connector_id), "draft_start"));
                },
                self, callback_event, draft_id, owner, std::move(input),
                connector_id, std::move(bundle.value())));
          },
          weak_factory_.GetWeakPtr(), callback_event, draft_id,
          std::move(owner), std::move(input), connector_id));
}

void DaoHomeUIHandler::HandleCallConnectorPage(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 4 || !args[0].is_string() || !args[1].is_string() ||
      !args[2].is_string() || !args[3].is_list()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  const std::string execution_id = args[1].GetString();
  const std::string operation = args[2].GetString();
  DaoHomeConnectorExecutor* executor = FindConnectorExecutor(execution_id);
  if (!executor) {
    ReplyConnector(
        callback_event,
        ConnectorError("cancelled", "The Home connector session ended."));
    return;
  }
  const std::string revision = executor->revision();
  const std::string connector_id = executor->connector_id();
  base::ListValue arguments = args[3].GetList().Clone();
  service_->GetSnapshot(base::BindOnce(
      [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
         std::string execution_id, std::string operation,
         base::ListValue arguments, std::string revision,
         std::string connector_id, HomeSnapshot snapshot) {
        if (!self) {
          return;
        }
        DaoHomeConnectorExecutor* executor =
            self->FindConnectorExecutor(execution_id);
        if (!self->HasActiveHomeOwner() || !executor ||
            snapshot.revision != executor->revision()) {
          if (executor) {
            executor->Cancel();
          }
          self->ReplyConnectorWithDiagnostic(
              callback_event, revision, connector_id, "page_call",
              ConnectorError("cancelled", "The Home revision changed."));
          return;
        }
        executor->CallPage(
            execution_id, operation, std::move(arguments),
            base::BindOnce(&DaoHomeUIHandler::ReplyConnectorWithDiagnostic,
                           self, std::move(callback_event), std::move(revision),
                           std::move(connector_id), "page_call"));
      },
      weak_factory_.GetWeakPtr(), callback_event, execution_id, operation,
      std::move(arguments), revision, connector_id));
}

void DaoHomeUIHandler::HandleFinishConnector(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 3 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  const std::string execution_id = args[1].GetString();
  DaoHomeConnectorExecutor* executor = FindConnectorExecutor(execution_id);
  if (!executor) {
    ReplyConnector(
        callback_event,
        ConnectorError("cancelled", "The Home connector session ended."));
    return;
  }
  const std::string revision = executor->revision();
  const std::string connector_id = executor->connector_id();
  base::Value result = args[2].Clone();
  service_->GetSnapshot(base::BindOnce(
      [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
         std::string execution_id, base::Value result, std::string revision,
         std::string connector_id, HomeSnapshot snapshot) {
        if (!self) {
          return;
        }
        DaoHomeConnectorExecutor* executor =
            self->FindConnectorExecutor(execution_id);
        if (!self->HasActiveHomeOwner() || !executor ||
            snapshot.revision != executor->revision()) {
          if (executor) {
            executor->Cancel();
          }
          self->ReplyConnectorWithDiagnostic(
              callback_event, revision, connector_id, "finish",
              ConnectorError("cancelled", "The Home revision changed."));
          return;
        }
        executor->Finish(
            execution_id, std::move(result),
            base::BindOnce(&DaoHomeUIHandler::ReplyConnectorWithDiagnostic,
                           self, std::move(callback_event), std::move(revision),
                           std::move(connector_id), "finish"));
      },
      weak_factory_.GetWeakPtr(), callback_event, execution_id,
      std::move(result), revision, connector_id));
}

void DaoHomeUIHandler::HandleResolveMedia(const base::ListValue& args) {
  AllowJavascript();
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string()) {
    return;
  }
  const std::string callback_event = args[0].GetString();
  const std::string handle = args[1].GetString();
  DaoHomeConnectorExecutor* executor = FindMediaExecutor(handle);
  if (!executor) {
    ReplyConnector(
        callback_event,
        ConnectorError("not_found", "The Home media handle is unavailable."));
    return;
  }
  const std::string revision = executor->revision();
  const std::string connector_id = executor->connector_id();
  service_->GetSnapshot(base::BindOnce(
      [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
         std::string handle, std::string revision, std::string connector_id,
         HomeSnapshot snapshot) {
        if (!self) {
          return;
        }
        DaoHomeConnectorExecutor* executor = self->FindMediaExecutor(handle);
        if (!self->HasActiveHomeOwner() || snapshot.revision != revision ||
            !executor || executor->revision() != revision) {
          self->ReplyConnectorWithDiagnostic(
              callback_event, revision, connector_id, "media",
              ConnectorError("cancelled", "The Home revision changed."));
          return;
        }
        executor->ResolveMedia(
            handle,
            base::BindOnce(&DaoHomeUIHandler::ReplyConnectorWithDiagnostic,
                           self, std::move(callback_event), std::move(revision),
                           std::move(connector_id), "media"));
      },
      weak_factory_.GetWeakPtr(), callback_event, handle, revision,
      connector_id));
}

void DaoHomeUIHandler::HandleCompleteAgentConnector(
    const base::ListValue& args) {
  if (args.size() < 2 || !args[0].is_string() ||
      args[0].GetString() != agent_connector_request_id_ ||
      !agent_connector_callback_) {
    return;
  }
  agent_connector_timeout_.Stop();
  agent_connector_request_id_.clear();
  draft_connector_executor_->Cancel();
  draft_connector_executor_ = std::make_unique<DaoHomeConnectorExecutor>();
  ConnectorCallback callback = std::move(agent_connector_callback_);
  std::move(callback).Run(args[1].Clone());
}

void DaoHomeUIHandler::HandleNotifyAgentPreviewLoaded(
    const base::ListValue& args) {
  if (args.empty() || !args[0].is_string() ||
      args[0].GetString() != agent_preview_request_id_ ||
      !agent_preview_callback_) {
    return;
  }
  if (!HasActiveHomeOwner()) {
    CancelAgentPreview("cancelled", "Dao Home is not active.");
    return;
  }
  agent_preview_settle_timer_.Start(
      FROM_HERE, base::Milliseconds(100),
      base::BindOnce(&DaoHomeUIHandler::FinishAgentPreviewAfterLoad,
                     weak_factory_.GetWeakPtr()));
}

void DaoHomeUIHandler::OnDidAddMessageToConsole(
    content::RenderFrameHost* source_frame,
    blink::mojom::ConsoleMessageLevel log_level,
    const std::u16string&,
    int32_t,
    const std::u16string&,
    const std::optional<std::u16string>&) {
  if (!agent_preview_callback_ || !source_frame ||
      log_level != blink::mojom::ConsoleMessageLevel::kError) {
    return;
  }
  const GURL& url = source_frame->GetLastCommittedURL();
  if (!url.SchemeIs(content::kChromeUIUntrustedScheme) ||
      url.host() != kHomeAppHost || url.path().size() < 2) {
    return;
  }
  std::optional<ProjectResourcePath> parsed =
      ParseResourcePath(url.path().substr(1));
  if (parsed && parsed->preview && parsed->id == agent_preview_draft_id_) {
    agent_preview_runtime_failed_ = true;
  }
}

void DaoHomeUIHandler::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (!agent_preview_callback_ || !navigation ||
      navigation->IsInPrimaryMainFrame()) {
    return;
  }
  const GURL& url = navigation->GetURL();
  std::optional<ProjectResourcePath> parsed;
  if (url.SchemeIs(content::kChromeUIUntrustedScheme) &&
      url.host() == kHomeAppHost && url.path().size() >= 2) {
    parsed = ParseResourcePath(url.path().substr(1));
  }
  const bool is_expected_preview =
      parsed && parsed->preview && parsed->id == agent_preview_draft_id_ &&
      base::UnescapeURLComponent(
          parsed->resource,
          base::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
              base::UnescapeRule::PATH_SEPARATORS |
              base::UnescapeRule::SPACES) == agent_preview_entry_;
  if (!agent_preview_frame_tree_node_id_) {
    if (!is_expected_preview) {
      return;
    }
    agent_preview_frame_tree_node_id_ = navigation->GetFrameTreeNodeId();
    agent_preview_runtime_failed_ =
        !navigation->HasCommitted() || navigation->IsErrorPage();
    return;
  }
  if (navigation->GetFrameTreeNodeId() == agent_preview_frame_tree_node_id_ &&
      !navigation->IsSameDocument()) {
    agent_preview_runtime_failed_ = true;
  }
}

void DaoHomeUIHandler::PrimaryPageChanged(content::Page&) {
  InvalidateDocumentMutationLease();
  CancelAllSessions();
}

void DaoHomeUIHandler::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN) {
    InvalidateDocumentMutationLease();
    CancelAllSessions();
  }
}

void DaoHomeUIHandler::WebContentsDestroyed() {
  InvalidateDocumentMutationLease();
  CancelAllSessions();
}

void DaoHomeUIHandler::HandleRecordRuntimeError(const base::ListValue& args) {
  if (args.size() < 2 || !args[0].is_string() || !args[1].is_string() ||
      !HasActiveHomeOwner()) {
    return;
  }
  const std::string revision = args[0].GetString();
  const std::string kind = args[1].GetString();
  if (kind != "error" && kind != "unhandled_rejection") {
    return;
  }
  service_->GetSnapshot(base::BindOnce(
      [](base::WeakPtr<DaoHomeUIHandler> self, std::string revision,
         std::string kind, HomeSnapshot snapshot) {
        if (!self || snapshot.revision != revision ||
            !self->HasActiveHomeOwner()) {
          return;
        }
        HomeDiagnostic diagnostic;
        diagnostic.revision = std::move(revision);
        diagnostic.stage = "runtime";
        diagnostic.code = std::move(kind);
        diagnostic.detail = "The generated Home application failed.";
        self->service_->RecordDiagnostic(std::move(diagnostic));
      },
      weak_factory_.GetWeakPtr(), revision, kind));
}

void DaoHomeUIHandler::HandleCancelSession(const base::ListValue& args) {
  InvalidateDocumentMutationLease();
  CancelAllSessions();
  service_->ClearSelectedNode();
}

void DaoHomeUIHandler::CancelAgentSession() {
  CancelAgentConnector("cancelled", "The Home connector session ended.");
  CancelAgentPreview("cancelled", "The Home preview session ended.");
}

void DaoHomeUIHandler::CancelAllSessions() {
  connector_executor_->Cancel();
  for (const auto& executor : retained_connector_executors_) {
    executor->Cancel();
  }
  retained_connector_executors_.clear();
  CancelAgentSession();
}

void DaoHomeUIHandler::HandleSetSelection(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string() || !HasActiveHomeOwner()) {
    return;
  }
  const std::string& node_id = args[0].GetString();
  if (node_id.empty() || node_id.size() > 128 ||
      !std::ranges::all_of(node_id, [](unsigned char character) {
        return base::IsAsciiAlpha(character) || base::IsAsciiDigit(character) ||
               character == '-' || character == '_' || character == '.';
      })) {
    return;
  }
  service_->SetSelectedNode(node_id);
}

void DaoHomeUIHandler::HandleOpenNavigation(const base::ListValue& args) {
  if (args.empty() || !args[0].is_string() || !HasActiveHomeOwner()) {
    return;
  }
  GURL url(args[0].GetString());
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  Browser* browser = chrome::FindBrowserWithTab(web_ui()->GetWebContents());
  if (!browser) {
    return;
  }
  NavigateParams params(browser, url, ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void DaoHomeUIHandler::ReplyConnector(const std::string& callback_event,
                                      base::Value result) {
  FireWebUIListener(callback_event, std::move(result));
}

void DaoHomeUIHandler::ReplyConnectorWithDiagnostic(
    const std::string& callback_event,
    const std::string& revision,
    const std::string& connector_id,
    const std::string& stage,
    base::Value result) {
  const base::DictValue* value = result.GetIfDict();
  const std::string* code = value ? value->FindString("code") : nullptr;
  const std::string* detail = value ? value->FindString("error") : nullptr;
  if (code && detail) {
    HomeDiagnostic diagnostic;
    diagnostic.revision = revision;
    diagnostic.connector_id = connector_id;
    diagnostic.stage = stage;
    diagnostic.code = *code;
    diagnostic.detail = *detail;
    service_->RecordDiagnostic(std::move(diagnostic));
  }
  ReplyConnector(callback_event, std::move(result));
}

void DaoHomeUIHandler::ReplySnapshot(const std::string& callback_event,
                                     HomeSnapshot snapshot) {
  FireWebUIListener(callback_event, base::Value(SnapshotToValue(snapshot)));
}

void DaoHomeUIHandler::ReplyVersions(const std::string& callback_event,
                                     std::vector<HomeVersion> versions) {
  base::ListValue values;
  for (const HomeVersion& version : versions) {
    values.Append(VersionToValue(version));
  }
  FireWebUIListener(callback_event, base::Value(std::move(values)));
}

void DaoHomeUIHandler::ReplyAfterMutation(const std::string& callback_event) {
  service_->GetSnapshot(base::BindOnce(&DaoHomeUIHandler::ReplySnapshot,
                                       weak_factory_.GetWeakPtr(),
                                       callback_event));
}

void DaoHomeUIHandler::DiscardDraftAndReply(std::string draft_id,
                                            std::string callback_event,
                                            base::Value result) {
  service_->DiscardDraft(
      draft_id,
      base::BindOnce(
          [](base::WeakPtr<DaoHomeUIHandler> self, std::string callback_event,
             base::Value result, base::expected<void, HomeError>) {
            if (self) {
              self->FireWebUIListener(callback_event, std::move(result));
            }
          },
          weak_factory_.GetWeakPtr(), std::move(callback_event),
          std::move(result)));
}

void DaoHomeUIHandler::OnProjectChanged() {
  CancelAllSessions();
  if (IsJavascriptAllowed()) {
    FireWebUIListener("dao-home-project-changed");
  }
}

void DaoHomeUIHandler::PrepareConnectorExecutorForStart() {
  if (connector_executor_->collection_finished()) {
    retained_connector_executors_.push_back(std::move(connector_executor_));
    if (retained_connector_executors_.size() > kMaxRetainedConnectorExecutors) {
      retained_connector_executors_.erase(
          retained_connector_executors_.begin());
    }
    connector_executor_ = std::make_unique<DaoHomeConnectorExecutor>();
    return;
  }
  if (connector_executor_->running()) {
    connector_executor_->Cancel();
  }
}

void DaoHomeUIHandler::PrepareDraftConnectorExecutorForStart() {
  if (draft_connector_executor_->running()) {
    draft_connector_executor_->Cancel();
  }
  if (draft_connector_executor_->collection_finished()) {
    draft_connector_executor_ = std::make_unique<DaoHomeConnectorExecutor>();
  }
}

DaoHomeConnectorExecutor* DaoHomeUIHandler::FindConnectorExecutor(
    const std::string& execution_id) {
  if (connector_executor_->OwnsExecution(execution_id)) {
    return connector_executor_.get();
  }
  if (draft_connector_executor_->OwnsExecution(execution_id)) {
    return draft_connector_executor_.get();
  }
  return nullptr;
}

DaoHomeConnectorExecutor* DaoHomeUIHandler::FindMediaExecutor(
    const std::string& handle) {
  if (connector_executor_->OwnsMediaHandle(handle)) {
    return connector_executor_.get();
  }
  if (draft_connector_executor_->OwnsMediaHandle(handle)) {
    return draft_connector_executor_.get();
  }
  const auto found = std::ranges::find_if(
      retained_connector_executors_,
      [&](const auto& executor) { return executor->OwnsMediaHandle(handle); });
  return found == retained_connector_executors_.end() ? nullptr : found->get();
}

void DaoHomeUIHandler::CancelAgentConnector(std::string code,
                                            std::string message) {
  agent_connector_timeout_.Stop();
  agent_connector_request_id_.clear();
  draft_connector_executor_->Cancel();
  draft_connector_executor_ = std::make_unique<DaoHomeConnectorExecutor>();
  if (!agent_connector_callback_) {
    return;
  }
  ConnectorCallback callback = std::move(agent_connector_callback_);
  std::move(callback).Run(ConnectorError(std::move(code), std::move(message)));
}

void DaoHomeUIHandler::CancelAgentPreview(std::string code,
                                          std::string message) {
  agent_preview_timeout_.Stop();
  agent_preview_settle_timer_.Stop();
  const std::string request_id = agent_preview_request_id_;
  service_->EndDraftPreview(agent_preview_draft_id_);
  agent_preview_request_id_.clear();
  agent_preview_draft_id_.clear();
  agent_preview_entry_.clear();
  agent_preview_requirements_ = HomePreviewRequirements();
  agent_preview_frame_tree_node_id_ = {};
  agent_preview_runtime_failed_ = false;
  if (!request_id.empty() && IsJavascriptAllowed()) {
    FireWebUIListener("dao-home-agent-preview-ended", base::Value(request_id));
  }
  if (!agent_preview_callback_) {
    return;
  }
  PreviewCallback callback = std::move(agent_preview_callback_);
  std::move(callback).Run(ConnectorError(std::move(code), std::move(message)));
}

void DaoHomeUIHandler::FinishAgentPreviewAfterLoad() {
  if (!agent_preview_callback_) {
    return;
  }
  if (!HasActiveHomeOwner()) {
    CancelAgentPreview("cancelled", "Dao Home is not active.");
    return;
  }
  if (agent_preview_runtime_failed_) {
    CancelAgentPreview("runtime_error",
                       "The generated Home application failed during preview.");
    return;
  }
  if (!agent_preview_requirements_.experience) {
    CompleteAgentPreview();
    return;
  }
  content::WebContents* contents = web_ui()->GetWebContents();
  content::RenderFrameHost* frame = contents->UnsafeFindFrameByFrameTreeNodeId(
      agent_preview_frame_tree_node_id_);
  if (!IsExpectedAgentPreviewFrame(frame, contents, agent_preview_draft_id_,
                                   agent_preview_entry_)) {
    CancelAgentPreview("runtime_error",
                       "The generated Home preview frame changed.");
    return;
  }
  const std::string request_id = agent_preview_request_id_;
  const content::FrameTreeNodeId frame_tree_node_id =
      agent_preview_frame_tree_node_id_;
  frame->ExecuteJavaScriptInIsolatedWorld(
      kHistoryBootstrapPreviewInspectionScript,
      base::BindOnce(&DaoHomeUIHandler::FinishAgentPreviewWithSemantics,
                     weak_factory_.GetWeakPtr(), request_id,
                     frame_tree_node_id),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoHomeUIHandler::FinishAgentPreviewWithSemantics(
    std::string request_id,
    content::FrameTreeNodeId frame_tree_node_id,
    base::Value result) {
  if (!agent_preview_callback_ || request_id != agent_preview_request_id_ ||
      frame_tree_node_id != agent_preview_frame_tree_node_id_) {
    return;
  }
  if (!HasActiveHomeOwner()) {
    CancelAgentPreview("cancelled", "Dao Home is not active.");
    return;
  }
  content::WebContents* contents = web_ui()->GetWebContents();
  content::RenderFrameHost* frame =
      contents->UnsafeFindFrameByFrameTreeNodeId(frame_tree_node_id);
  if (agent_preview_runtime_failed_ ||
      !IsExpectedAgentPreviewFrame(frame, contents, agent_preview_draft_id_,
                                   agent_preview_entry_)) {
    CancelAgentPreview("runtime_error",
                       "The generated Home preview frame changed.");
    return;
  }
  if (!ValidateAgentPreviewSemantics(result)) {
    CancelAgentPreview(
        "invalid_experience",
        "The generated Home does not match its start-surface contract.");
    return;
  }
  CompleteAgentPreview();
}

bool DaoHomeUIHandler::ValidateAgentPreviewSemantics(
    const base::Value& result) const {
  if (!agent_preview_requirements_.experience) {
    return true;
  }
  const base::DictValue* value = result.GetIfDict();
  const base::ListValue* actions = value ? value->FindList("actions") : nullptr;
  const base::ListValue* source_slots =
      value ? value->FindList("sourceSlots") : nullptr;
  const std::optional<bool> has_direct_external_navigation =
      value ? value->FindBool("hasDirectExternalNavigation") : std::nullopt;
  const std::optional<bool> has_feed =
      value ? value->FindBool("hasFeed") : std::nullopt;
  const HomeExperience& experience = *agent_preview_requirements_.experience;
  if (!actions || !source_slots || !has_direct_external_navigation ||
      !has_feed || *has_direct_external_navigation || !*has_feed ||
      source_slots->size() != experience.source_slots.size()) {
    return false;
  }

  const std::set<std::string> declared_actions(
      experience.primary_actions.begin(), experience.primary_actions.end());
  std::set<std::string> seen_actions;
  size_t first_viewport_action_count = 0;
  for (const base::Value& action_value : *actions) {
    const base::DictValue* action = action_value.GetIfDict();
    const std::string* id = action ? action->FindString("id") : nullptr;
    const std::string* url = action ? action->FindString("url") : nullptr;
    const std::optional<bool> focusable =
        action ? action->FindBool("focusable") : std::nullopt;
    const std::optional<bool> visible =
        action ? action->FindBool("visible") : std::nullopt;
    const auto launch_url =
        id ? agent_preview_requirements_.launch_urls.find(*id)
           : agent_preview_requirements_.launch_urls.end();
    if (!id || !url || !focusable || !visible ||
        !seen_actions.insert(*id).second ||
        launch_url == agent_preview_requirements_.launch_urls.end() ||
        *url != launch_url->second.spec()) {
      return false;
    }
    if (declared_actions.contains(*id) && *focusable && *visible) {
      ++first_viewport_action_count;
    }
  }
  if (seen_actions.size() != agent_preview_requirements_.launch_urls.size() ||
      !std::ranges::all_of(declared_actions, [&](const std::string& id) {
        return seen_actions.contains(id);
      }) ||
      first_viewport_action_count <
          std::min<size_t>(4, experience.primary_actions.size())) {
    return false;
  }

  const std::set<std::string> declared_source_slots(
      experience.source_slots.begin(), experience.source_slots.end());
  std::set<std::string> seen_source_slots;
  for (const base::Value& slot_value : *source_slots) {
    const base::DictValue* slot = slot_value.GetIfDict();
    const std::string* id = slot ? slot->FindString("id") : nullptr;
    const std::string* connector_id =
        slot ? slot->FindString("connectorId") : nullptr;
    if (!id || !connector_id || !declared_source_slots.contains(*id) ||
        *connector_id != *id || !seen_source_slots.insert(*id).second ||
        !agent_preview_requirements_.tested_connector_ids.contains(
            *connector_id)) {
      return false;
    }
  }
  return seen_source_slots == declared_source_slots;
}

void DaoHomeUIHandler::CompleteAgentPreview() {
  agent_preview_timeout_.Stop();
  agent_preview_settle_timer_.Stop();
  const std::string request_id = agent_preview_request_id_;
  const std::string draft_id = agent_preview_draft_id_;
  service_->EndDraftPreview(draft_id);
  service_->MarkDraftPreviewed(draft_id);
  agent_preview_request_id_.clear();
  agent_preview_draft_id_.clear();
  agent_preview_entry_.clear();
  agent_preview_requirements_ = HomePreviewRequirements();
  agent_preview_frame_tree_node_id_ = {};
  agent_preview_runtime_failed_ = false;
  PreviewCallback callback = std::move(agent_preview_callback_);
  if (!request_id.empty() && IsJavascriptAllowed()) {
    FireWebUIListener("dao-home-agent-preview-ended", base::Value(request_id));
  }
  std::move(callback).Run(base::Value(base::DictValue().Set("valid", true)));
}

scoped_refptr<DaoHomeMutationLease> DaoHomeUIHandler::CreateMutationLease() {
  if (!HasActiveHomeOwner()) {
    return nullptr;
  }
  if (!document_mutation_lease_ || !document_mutation_lease_->IsValid()) {
    document_mutation_lease_ = base::MakeRefCounted<DaoHomeMutationLease>();
  }
  return base::MakeRefCounted<DaoHomeMutationLease>(document_mutation_lease_);
}

void DaoHomeUIHandler::ArmAgentConnectorForTesting(ConnectorCallback callback) {
  CHECK(!agent_connector_callback_);
  agent_connector_request_id_ = "test-held-connector";
  agent_connector_callback_ = std::move(callback);
}

bool DaoHomeUIHandler::HasPendingAgentConnectorForTesting() const {
  return static_cast<bool>(agent_connector_callback_);
}

void DaoHomeUIHandler::StartRuntimeConnectorForTesting(
    HomeConnector connector,
    ConnectorCallback callback) {
  PrepareConnectorExecutorForStart();
  connector_executor_->Start(
      web_ui()->GetWebContents(), Profile::FromWebUI(web_ui()),
      "test-runtime-revision", std::move(connector), HomeLimits(),
      "export default {}", R"({"type":"array","items":{"type":"string"}})",
      base::Value(base::DictValue()), std::move(callback));
}

bool DaoHomeUIHandler::HasActiveRuntimeConnectorForTesting() const {
  return connector_executor_->running();
}

void DaoHomeUIHandler::InvalidateDocumentMutationLease() {
  service_->ClearHistoryBootstrapForOwner(web_ui()->GetWebContents());
  if (document_mutation_lease_) {
    document_mutation_lease_->Invalidate();
    document_mutation_lease_.reset();
  }
}

void DaoHomeUIHandler::OnPermissionChanged(
    const std::optional<HomePermissionRequest>& request) {
  if (!IsJavascriptAllowed()) {
    return;
  }
  FireWebUIListener(
      "dao-home-permission-changed",
      request ? base::Value(PermissionToValue(*request)) : base::Value());
}

void DaoHomeUIHandler::OnBootstrapPermissionChanged() {
  if (!IsJavascriptAllowed()) {
    return;
  }
  std::optional<HomePermissionBatchRequest> request;
  if (HasActiveHomeOwner()) {
    request =
        service_->GetPendingBootstrapPermission(web_ui()->GetWebContents());
  }
  FireWebUIListener("dao-home-permission-changed",
                    request ? base::Value(BootstrapPermissionToValue(*request))
                            : base::Value());
}

DaoHomeUI::DaoHomeUI(content::WebUI* web_ui) : WebUIController(web_ui) {
  web_ui->AddRequestableScheme(content::kChromeUIUntrustedScheme);
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kHomeHost);
  source->AddResourcePaths(kDaoHomeResources);
  source->SetDefaultResource(IDR_DAO_HOME_HOME_HTML);
  source->UseStringsJs();
  source->AddLocalizedString("daoHomePageTitle", IDS_DAO_HOME_PAGE_TITLE);
  source->AddLocalizedString("daoHomeLoading", IDS_DAO_HOME_LOADING);
  source->AddLocalizedString("daoHomeEmptyTitle", IDS_DAO_HOME_EMPTY_TITLE);
  source->AddLocalizedString("daoHomeEmptyDescription",
                             IDS_DAO_HOME_EMPTY_DESCRIPTION);
  source->AddLocalizedString("daoHomeCreate", IDS_DAO_HOME_CREATE);
  source->AddLocalizedString("daoHomeCreateFromHistory",
                             IDS_DAO_HOME_CREATE_FROM_HISTORY);
  source->AddLocalizedString("daoHomeMenu", IDS_DAO_HOME_MENU);
  source->AddLocalizedString("daoHomeEditWithDao", IDS_DAO_HOME_EDIT_WITH_DAO);
  source->AddLocalizedString("daoHomeExport", IDS_DAO_HOME_EXPORT);
  source->AddLocalizedString("daoHomeImport", IDS_DAO_HOME_IMPORT);
  source->AddLocalizedString("daoHomeReset", IDS_DAO_HOME_RESET);
  source->AddLocalizedString("daoHomeRollback", IDS_DAO_HOME_ROLLBACK);
  source->AddLocalizedString("daoHomeRuntimeErrorTitle",
                             IDS_DAO_HOME_RUNTIME_ERROR_TITLE);
  source->AddLocalizedString("daoHomeRuntimeErrorDescription",
                             IDS_DAO_HOME_RUNTIME_ERROR_DESCRIPTION);
  source->AddLocalizedString("daoHomeRetry", IDS_DAO_HOME_RETRY);
  source->AddLocalizedString("daoHomeAskDaoToFix", IDS_DAO_HOME_ASK_DAO_TO_FIX);
  source->AddLocalizedString("daoHomeViewSource", IDS_DAO_HOME_VIEW_SOURCE);
  source->AddLocalizedString("daoHomeVersionHistory",
                             IDS_DAO_HOME_VERSION_HISTORY);
  source->AddLocalizedString("daoHomeClose", IDS_DAO_HOME_CLOSE);
  source->AddLocalizedString("daoHomeFiles", IDS_DAO_HOME_FILES);
  source->AddLocalizedString("daoHomeSelectFile", IDS_DAO_HOME_SELECT_FILE);
  source->AddLocalizedString("daoHomeCompareVersion",
                             IDS_DAO_HOME_COMPARE_VERSION);
  source->AddLocalizedString("daoHomeDiffCurrent", IDS_DAO_HOME_DIFF_CURRENT);
  source->AddLocalizedString("daoHomeCurrentVersion",
                             IDS_DAO_HOME_CURRENT_VERSION);
  source->AddLocalizedString("daoHomeRestoreVersion",
                             IDS_DAO_HOME_RESTORE_VERSION);
  source->AddLocalizedString("daoHomeConnectSource",
                             IDS_DAO_HOME_CONNECT_SOURCE);
  source->AddLocalizedString("daoHomePermissionDescription",
                             IDS_DAO_HOME_PERMISSION_DESCRIPTION);
  source->AddLocalizedString("daoHomeConnectSourcesTitle",
                             IDS_DAO_HOME_CONNECT_SOURCES_TITLE);
  source->AddLocalizedString("daoHomeConnectSourcesDescription",
                             IDS_DAO_HOME_CONNECT_SOURCES_DESCRIPTION);
  source->AddLocalizedString("daoHomeAuthenticationMayBeRequired",
                             IDS_DAO_HOME_AUTHENTICATION_MAY_BE_REQUIRED);
  source->AddLocalizedString("daoHomeConnectSelected",
                             IDS_DAO_HOME_CONNECT_SELECTED);
  source->AddLocalizedString("daoHomeContinueWithoutSources",
                             IDS_DAO_HOME_CONTINUE_WITHOUT_SOURCES);
  source->AddLocalizedString("daoHomeWebsite", IDS_DAO_HOME_WEBSITE);
  source->AddLocalizedString("daoHomePageScope", IDS_DAO_HOME_PAGE_SCOPE);
  source->AddLocalizedString("daoHomeCapabilities", IDS_DAO_HOME_CAPABILITIES);
  source->AddLocalizedString("daoHomeRuns", IDS_DAO_HOME_RUNS);
  source->AddLocalizedString("daoHomeActiveOnly", IDS_DAO_HOME_ACTIVE_ONLY);
  source->AddLocalizedString("daoHomeWriteAccess", IDS_DAO_HOME_WRITE_ACCESS);
  source->AddLocalizedString("daoHomeNotAllowed", IDS_DAO_HOME_NOT_ALLOWED);
  source->AddLocalizedString("daoHomeResourceLimits",
                             IDS_DAO_HOME_RESOURCE_LIMITS);
  source->AddLocalizedString("daoHomeResultBytesLimit",
                             IDS_DAO_HOME_RESULT_BYTES_LIMIT);
  source->AddLocalizedString("daoHomeResultBytesIncrease",
                             IDS_DAO_HOME_RESULT_BYTES_INCREASE);
  source->AddLocalizedString("daoHomeItemLimit", IDS_DAO_HOME_ITEM_LIMIT);
  source->AddLocalizedString("daoHomeItemLimitIncrease",
                             IDS_DAO_HOME_ITEM_LIMIT_INCREASE);
  source->AddLocalizedString("daoHomeCapabilityReadContent",
                             IDS_DAO_HOME_CAPABILITY_READ_CONTENT);
  source->AddLocalizedString("daoHomeCapabilityReadStyle",
                             IDS_DAO_HOME_CAPABILITY_READ_STYLE);
  source->AddLocalizedString("daoHomeCapabilityScroll",
                             IDS_DAO_HOME_CAPABILITY_SCROLL);
  source->AddLocalizedString("daoHomeCancel", IDS_DAO_HOME_CANCEL);
  source->AddLocalizedString("daoHomeConnect", IDS_DAO_HOME_CONNECT);
  source->AddLocalizedString("daoHomeConfirm", IDS_DAO_HOME_CONFIRM);
  source->AddLocalizedString("daoHomeConfirmImportTitle",
                             IDS_DAO_HOME_CONFIRM_IMPORT_TITLE);
  source->AddLocalizedString("daoHomeConfirmImportDescription",
                             IDS_DAO_HOME_CONFIRM_IMPORT_DESCRIPTION);
  source->AddLocalizedString("daoHomeImportFailed", IDS_DAO_HOME_IMPORT_FAILED);
  source->AddLocalizedString("daoHomeConfirmResetTitle",
                             IDS_DAO_HOME_CONFIRM_RESET_TITLE);
  source->AddLocalizedString("daoHomeConfirmResetDescription",
                             IDS_DAO_HOME_CONFIRM_RESET_DESCRIPTION);
  source->AddLocalizedString("daoHomeResetFailed",
                             IDS_DAO_HOME_RESET_FAILED);
  source->AddLocalizedString("daoHomeConfirmRestoreTitle",
                             IDS_DAO_HOME_CONFIRM_RESTORE_TITLE);
  source->AddLocalizedString("daoHomeConfirmRestoreDescription",
                             IDS_DAO_HOME_CONFIRM_RESTORE_DESCRIPTION);
  source->AddLocalizedString("daoHomeConfirmNavigationTitle",
                             IDS_DAO_HOME_CONFIRM_NAVIGATION_TITLE);
  source->AddLocalizedString("daoHomeConfirmNavigationDescription",
                             IDS_DAO_HOME_CONFIRM_NAVIGATION_DESCRIPTION);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ChildSrc,
      "child-src chrome-untrusted://dao-home-app "
      "chrome-untrusted://dao-home-connector;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      "frame-src chrome-untrusted://dao-home-app "
      "chrome-untrusted://dao-home-connector;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::TrustedTypes,
      "trusted-types default lit-html-desktop;");
  auto handler = std::make_unique<DaoHomeUIHandler>(
      DaoHomeProjectServiceFactory::GetForProfile(profile));
  handler_ = handler.get();
  web_ui->AddMessageHandler(std::move(handler));
}

DaoHomeUI::~DaoHomeUI() = default;

void DaoHomeUI::CollectConnectorForAgent(std::string draft_id,
                                         std::string connector_id,
                                         base::Value input,
                                         ConnectorCallback callback) {
  if (!handler_) {
    std::move(callback).Run(
        ConnectorError("cancelled", "Dao Home is unavailable."));
    return;
  }
  handler_->CollectConnectorForAgent(std::move(draft_id),
                                     std::move(connector_id), std::move(input),
                                     std::move(callback));
}

void DaoHomeUI::PreviewDraftForAgent(std::string draft_id,
                                     std::string entry,
                                     HomePreviewRequirements requirements,
                                     PreviewCallback callback) {
  if (!handler_) {
    std::move(callback).Run(
        ConnectorError("cancelled", "Dao Home is unavailable."));
    return;
  }
  handler_->PreviewDraftForAgent(std::move(draft_id), std::move(entry),
                                 std::move(requirements), std::move(callback));
}

void DaoHomeUI::CancelAgentSession() {
  if (handler_) {
    handler_->CancelAgentSession();
  }
}

scoped_refptr<DaoHomeMutationLease> DaoHomeUI::CreateMutationLease() {
  return handler_ ? handler_->CreateMutationLease() : nullptr;
}

void DaoHomeUI::ArmAgentConnectorForTesting(ConnectorCallback callback) {
  CHECK(handler_);
  handler_->ArmAgentConnectorForTesting(std::move(callback));
}

bool DaoHomeUI::HasPendingAgentConnectorForTesting() const {
  return handler_ && handler_->HasPendingAgentConnectorForTesting();
}

void DaoHomeUI::StartRuntimeConnectorForTesting(HomeConnector connector,
                                                ConnectorCallback callback) {
  CHECK(handler_);
  handler_->StartRuntimeConnectorForTesting(std::move(connector),
                                            std::move(callback));
}

bool DaoHomeUI::HasActiveRuntimeConnectorForTesting() const {
  return handler_ && handler_->HasActiveRuntimeConnectorForTesting();
}

DaoHomeAppUI::DaoHomeAppUI(content::WebUI* web_ui)
    : UntrustedWebUIController(web_ui) {
  web_ui->SetBindings(content::BindingsPolicySet());
  Profile* profile = Profile::FromWebUI(web_ui);
  DaoHomeProjectService* service =
      DaoHomeProjectServiceFactory::GetForProfile(profile);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kHomeAppUrl);
  source->AddFrameAncestor(GURL("dao://home"));
  source->AddResourcePath("generated_runtime.js",
                          IDR_DAO_HOME_GENERATED_RUNTIME_JS);
  source->SetRequestFilter(
      base::BindRepeating(&DaoHomeAppUI::ShouldHandleProjectRequest),
      base::BindRepeating(&DaoHomeAppUI::HandleProjectRequest, service));
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::DefaultSrc, "default-src 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc, "script-src 'self';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::StyleSrc,
      "style-src 'self' 'unsafe-inline';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ImgSrc, "img-src 'self' data: blob:;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc, "connect-src 'none';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ChildSrc, "child-src 'none';");
}

DaoHomeAppUI::~DaoHomeAppUI() = default;

DaoHomeConnectorUI::DaoHomeConnectorUI(content::WebUI* web_ui)
    : UntrustedWebUIController(web_ui) {
  web_ui->SetBindings(content::BindingsPolicySet());
  Profile* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource* source =
      content::WebUIDataSource::CreateAndAdd(profile, kHomeConnectorUrl);
  source->AddFrameAncestor(GURL("dao://home"));
  source->AddResourcePath("connector_sandbox.js",
                          IDR_DAO_HOME_CONNECTOR_SANDBOX_JS);
  source->SetDefaultResource(IDR_DAO_HOME_CONNECTOR_SANDBOX_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::DefaultSrc, "default-src 'none';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ScriptSrc, "script-src 'self' blob:;");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ConnectSrc, "connect-src 'none';");
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::ChildSrc, "child-src 'none';");
}

DaoHomeConnectorUI::~DaoHomeConnectorUI() = default;

// static
bool DaoHomeAppUI::ShouldHandleProjectRequest(const std::string& path) {
  return path == "generated_action_runtime.js" ||
         (path != "generated_runtime.js" &&
          ParseResourcePath(path).has_value());
}

// static
void DaoHomeAppUI::HandleProjectRequest(
    DaoHomeProjectService* service,
    const std::string& path,
    content::WebUIDataSource::GotDataCallback callback) {
  if (path == "generated_action_runtime.js") {
    std::move(callback).Run(base::MakeRefCounted<base::RefCountedString>(
        std::string(kGeneratedActionRuntime)));
    return;
  }
  std::optional<ProjectResourcePath> parsed = ParseResourcePath(path);
  if (!parsed) {
    std::move(callback).Run(
        base::MakeRefCounted<base::RefCountedString>(std::string()));
    return;
  }
  const bool inject_runtime = base::EndsWith(
      parsed->resource, ".html", base::CompareCase::INSENSITIVE_ASCII);
  auto reply = base::BindOnce(&DaoHomeAppUI::ReplyProjectResource,
                              inject_runtime, std::move(callback));
  if (parsed->preview) {
    service->ReadActivePreviewFile(parsed->id, parsed->resource,
                                   std::move(reply));
  } else {
    service->ReadFile(parsed->id, parsed->resource, std::move(reply));
  }
}

// static
void DaoHomeAppUI::ReplyProjectResource(
    bool inject_runtime,
    content::WebUIDataSource::GotDataCallback callback,
    base::expected<std::string, HomeError> contents) {
  if (!contents.has_value()) {
    std::move(callback).Run(
        base::MakeRefCounted<base::RefCountedString>(std::string()));
    return;
  }
  std::string body = std::move(contents.value());
  if (inject_runtime) {
    constexpr std::string_view prelude =
        "<style>html,body{margin:0}</style>"
        "<script src=\"/generated_action_runtime.js\"></script>"
        "<script type=\"module\" src=\"/generated_runtime.js\"></script>";
    body.insert(FindGeneratedRuntimeInjectionOffset(body), prelude);
  }
  std::move(callback).Run(
      base::MakeRefCounted<base::RefCountedString>(std::move(body)));
}

}  // namespace dao
