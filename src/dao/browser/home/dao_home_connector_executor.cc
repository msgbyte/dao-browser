// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_connector_executor.h"

#include <algorithm>
#include <array>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "base/base64.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/uuid.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/visibility.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "content/public/common/url_constants.h"
#include "net/http/http_response_headers.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace dao {
namespace {

constexpr base::TimeDelta kExecutionTimeout = base::Seconds(20);
constexpr int kMaxOperations = 100;
constexpr int kMaxScrolls = 10;
constexpr size_t kMaxMediaHandles = 100;
constexpr size_t kMaxMediaBytes = 5 * 1024 * 1024;

base::Value Error(std::string code, std::string message) {
  return base::Value(base::DictValue()
                         .Set("error", std::move(message))
                         .Set("code", std::move(code)));
}

bool HasCapability(const HomeConnector& connector,
                   HomePageCapability capability) {
  return connector.permissions.capabilities.contains(capability);
}

bool IsSensitiveSelector(std::string_view selector) {
  std::string lower = base::ToLowerASCII(selector);
  return lower.find("password") != std::string::npos ||
         lower.find("current-password") != std::string::npos ||
         lower.find("new-password") != std::string::npos;
}

bool ArgumentsContainSensitiveSelector(const base::Value& value,
                                       int depth = 0) {
  if (depth > 5) {
    return true;
  }
  if (value.is_string()) {
    return IsSensitiveSelector(value.GetString());
  }
  if (value.is_list()) {
    for (const base::Value& child : value.GetList()) {
      if (ArgumentsContainSensitiveSelector(child, depth + 1)) {
        return true;
      }
    }
  } else if (value.is_dict()) {
    for (const auto [key, child] : value.GetDict()) {
      if (ArgumentsContainSensitiveSelector(child, depth + 1)) {
        return true;
      }
    }
  }
  return false;
}

bool ValidateSchemaValue(const base::Value& schema,
                         const base::Value& value,
                         int depth,
                         int max_items) {
  if (depth > 12 || !schema.is_dict()) {
    return false;
  }
  const base::DictValue& dict = schema.GetDict();
  const std::string* type = dict.FindString("type");
  if (!type) {
    return false;
  }
  if (*type == "null") {
    return value.is_none();
  }
  if (*type == "boolean") {
    return value.is_bool();
  }
  if (*type == "string") {
    return value.is_string() && value.GetString().size() <= 64 * 1024;
  }
  if (*type == "number") {
    return value.is_int() || value.is_double();
  }
  if (*type == "integer") {
    return value.is_int();
  }
  if (*type == "array") {
    if (!value.is_list() ||
        value.GetList().size() > static_cast<size_t>(max_items)) {
      return false;
    }
    const base::Value* items = dict.Find("items");
    if (!items) {
      return false;
    }
    for (const base::Value& item : value.GetList()) {
      if (!ValidateSchemaValue(*items, item, depth + 1, max_items)) {
        return false;
      }
    }
    return true;
  }
  if (*type == "object") {
    if (!value.is_dict()) {
      return false;
    }
    const base::DictValue* properties = dict.FindDict("properties");
    const base::ListValue* required = dict.FindList("required");
    if (required) {
      for (const base::Value& name : *required) {
        if (!name.is_string() || !value.GetDict().Find(name.GetString())) {
          return false;
        }
      }
    }
    if (!properties) {
      return true;
    }
    for (const auto [name, child] : value.GetDict()) {
      const base::Value* child_schema = properties->Find(name);
      if (child_schema &&
          !ValidateSchemaValue(*child_schema, child, depth + 1, max_items)) {
        return false;
      }
    }
    return true;
  }
  return false;
}

}  // namespace

class DaoHomeConnectorExecutor::OwnerObserver
    : public content::WebContentsObserver {
 public:
  OwnerObserver(content::WebContents* owner,
                base::WeakPtr<DaoHomeConnectorExecutor> executor)
      : content::WebContentsObserver(owner), executor_(std::move(executor)) {}

  void PrimaryPageChanged(content::Page& page) override { ScheduleCancel(); }
  void WebContentsDestroyed() override { ScheduleCancel(); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE) {
      ScheduleCancel();
    }
  }

 private:
  void ScheduleCancel() {
    Observe(nullptr);
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(
                       [](base::WeakPtr<DaoHomeConnectorExecutor> executor) {
                         if (executor) {
                           executor->Cancel();
                         }
                       },
                       executor_));
  }

  base::WeakPtr<DaoHomeConnectorExecutor> executor_;
};

DaoHomeConnectorExecutor::DaoHomeConnectorExecutor() = default;

DaoHomeConnectorExecutor::~DaoHomeConnectorExecutor() {
  Reset();
}

bool DaoHomeConnectorExecutor::running() const {
  return !execution_id_.empty();
}

void DaoHomeConnectorExecutor::Start(content::WebContents* owner,
                                     Profile* profile,
                                     std::string revision,
                                     HomeConnector connector,
                                     HomeLimits limits,
                                     std::string module_source,
                                     std::string schema_source,
                                     base::Value input,
                                     Callback callback) {
  Cancel();
  if (!owner || !profile || profile->IsOffTheRecord() ||
      connector.permissions.origins.empty() ||
      connector.permissions.paths.empty()) {
    std::move(callback).Run(
        Error("invalid_request", "Invalid Home connector request."));
    return;
  }
  std::optional<base::Value> schema =
      base::JSONReader::Read(schema_source, base::JSON_PARSE_RFC);
  if (!schema || !schema->is_dict()) {
    std::move(callback).Run(
        Error("invalid_schema", "The connector result schema is invalid."));
    return;
  }

  GURL start_url(connector.permissions.origins.front().GetURL().Resolve(
      connector.permissions.paths.front()));
  if (!start_url.is_valid()) {
    std::move(callback).Run(
        Error("invalid_scope", "The connector start URL is invalid."));
    return;
  }

  owner_ = owner->GetWeakPtr();
  profile_ = profile;
  revision_ = std::move(revision);
  connector_ = std::move(connector);
  limits_ = limits;
  module_source_ = std::move(module_source);
  schema_ = std::move(*schema);
  input_ = std::move(input);
  execution_id_ = base::Uuid::GenerateRandomV4().AsLowercaseString();
  start_callback_ = std::move(callback);
  owner_observer_ =
      std::make_unique<OwnerObserver>(owner, weak_factory_.GetWeakPtr());
  source_ =
      content::WebContents::Create(content::WebContents::CreateParams(profile));
  Observe(source_.get());
  timeout_.Start(FROM_HERE, kExecutionTimeout,
                 base::BindOnce(&DaoHomeConnectorExecutor::FailStart,
                                weak_factory_.GetWeakPtr(), "timed_out",
                                "The Home connector timed out."));
  content::NavigationController::LoadURLParams params(start_url);
  params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
  source_->GetController().LoadURLWithParams(params);
}

bool DaoHomeConnectorExecutor::OwnerIsActive() const {
  if (!owner_ || !profile_ || owner_->GetBrowserContext() != profile_ ||
      owner_->GetVisibility() == content::Visibility::HIDDEN) {
    return false;
  }
  Browser* browser = chrome::FindBrowserWithTab(owner_.get());
  return browser && browser->profile() == profile_ &&
         browser->tab_strip_model()->GetActiveWebContents() == owner_.get() &&
         owner_->GetLastCommittedURL().SchemeIs(content::kChromeUIScheme) &&
         owner_->GetLastCommittedURL().host() == "home";
}

bool DaoHomeConnectorExecutor::UrlIsAllowed(const GURL& url) const {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return false;
  }
  const url::Origin candidate = url::Origin::Create(url);
  const bool origin_allowed = std::ranges::any_of(
      connector_.permissions.origins, [&](const url::Origin& origin) {
        return origin.IsSameOriginWith(candidate);
      });
  if (!origin_allowed) {
    return false;
  }
  const std::string path =
      url.path().empty() ? std::string("/") : std::string(url.path());
  return std::ranges::any_of(
      connector_.permissions.paths, [&](const std::string& allowed) {
        if (allowed == "/") {
          return true;
        }
        if (!base::StartsWith(path, allowed, base::CompareCase::SENSITIVE)) {
          return false;
        }
        return path.size() == allowed.size() || allowed.back() == '/' ||
               path[allowed.size()] == '/';
      });
}

bool DaoHomeConnectorExecutor::OperationIsAllowed(
    const std::string& operation) const {
  if (operation == "getComputedStyle") {
    return HasCapability(connector_, HomePageCapability::kReadStyle);
  }
  if (operation == "scroll") {
    return HasCapability(connector_, HomePageCapability::kScroll);
  }
  if (operation == "navigate") {
    return true;
  }
  constexpr std::array<std::string_view, 7> kReadOperations = {
      "waitFor", "exists",       "query",   "queryAll",
      "getText", "getAttribute", "snapshot"};
  return HasCapability(connector_, HomePageCapability::kReadDom) &&
         std::ranges::find(kReadOperations, operation) != kReadOperations.end();
}

void DaoHomeConnectorExecutor::CallPage(const std::string& execution_id,
                                        const std::string& operation,
                                        base::ListValue arguments,
                                        Callback callback) {
  if (execution_id != execution_id_ || !source_ ||
      !committed_allowed_document_ || collection_finished_ ||
      !OwnerIsActive()) {
    std::move(callback).Run(
        Error("cancelled", "The active Dao Home connector session ended."));
    Cancel();
    return;
  }
  if (page_callback_ || finish_callback_) {
    std::move(callback).Run(
        Error("temporarily_unavailable",
              "A Home connector page operation is already running."));
    return;
  }
  if (++operation_count_ > kMaxOperations) {
    std::move(callback).Run(Error(
        "quota_exceeded", "The connector used too many page operations."));
    Cancel();
    return;
  }
  if (!OperationIsAllowed(operation) ||
      ArgumentsContainSensitiveSelector(base::Value(arguments.Clone()))) {
    std::move(callback).Run(Error(
        "operation_forbidden", "The connector page operation is not allowed."));
    return;
  }
  if (operation == "scroll" && ++scroll_count_ > kMaxScrolls) {
    std::move(callback).Run(Error(
        "quota_exceeded", "The connector used too many scroll operations."));
    Cancel();
    return;
  }
  if (operation == "navigate") {
    const std::string* target =
        !arguments.empty() ? arguments[0].GetIfString() : nullptr;
    const GURL url(target ? *target : std::string());
    if (!target || !UrlIsAllowed(url)) {
      std::move(callback).Run(
          Error("navigation_forbidden",
                "Connector navigation left its granted scope."));
      return;
    }
    committed_allowed_document_ = false;
    navigation_pending_ = true;
    page_callback_ = std::move(callback);
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_AUTO_TOPLEVEL;
    source_->GetController().LoadURLWithParams(params);
    return;
  }

  content::RenderFrameHost* frame = source_->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    std::move(callback).Run(
        Error("temporarily_unavailable", "The connector page is unavailable."));
    return;
  }
  page_callback_ = std::move(callback);
  if (operation == "waitFor") {
    const std::string* selector =
        !arguments.empty() ? arguments[0].GetIfString() : nullptr;
    if (!selector) {
      Callback invalid_callback = std::move(page_callback_);
      std::move(invalid_callback)
          .Run(Error("invalid_argument", "waitFor requires a selector."));
      return;
    }
    int wait_ms = 5000;
    if (arguments.size() > 1) {
      if (std::optional<int> requested = arguments[1].GetIfInt()) {
        wait_ms = *requested;
      } else if (std::optional<double> requested_double =
                     arguments[1].GetIfDouble()) {
        wait_ms = static_cast<int>(*requested_double);
      }
    }
    wait_for_selector_ = *selector;
    wait_for_deadline_ = base::TimeTicks::Now() +
                         base::Milliseconds(std::clamp(wait_ms, 0, 5000));
    PollWaitFor();
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(BuildAdapterScript(operation, arguments)),
      base::BindOnce(&DaoHomeConnectorExecutor::OnPageResult,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

std::string DaoHomeConnectorExecutor::BuildAdapterScript(
    const std::string& operation,
    const base::ListValue& arguments) const {
  std::string args_json;
  base::JSONWriter::Write(arguments, &args_json);
  std::string script = R"js((() => {
    const args = __ARGS__;
    const operation = __OP__;
    const sensitive = element => {
      if (!element) return false;
      const tag = String(element.tagName || '').toLowerCase();
      const type = String(element.type || '').toLowerCase();
      const autocomplete = String(element.autocomplete || '').toLowerCase();
      return (tag === 'input' && type === 'password') ||
          autocomplete === 'current-password' ||
          autocomplete === 'new-password';
    };
    const select = (selector, root = document) => {
      const element = root.querySelector(String(selector));
      if (sensitive(element)) throw new Error('Sensitive control rejected');
      return element;
    };
    const selectField = (field, root) => {
      const candidates = Array.isArray(field) ? field : [field];
      for (const candidateValue of candidates) {
        const candidate = String(candidateValue || '');
        if (!candidate) continue;
        if (candidate === 'text' || candidate === '$text')
          return {element: root, mode: 'text'};
        if (candidate === 'href' || candidate === '$href') {
          const element = root.matches?.('a[href]') ? root :
              select('a[href]', root);
          if (element) return {element, mode: 'href'};
          continue;
        }
        if (candidate === 'media' || candidate === '$media' ||
            candidate === 'src' || candidate === 'data-src') {
          const selector = candidate === 'data-src' ?
              'img[data-src],video[data-src]' : 'img,video';
          const element = root.matches?.(selector) ? root :
              select(selector, root);
          if (element) return {element, mode: 'media'};
          continue;
        }
        const element = root.matches?.(candidate) ? root :
            select(candidate, root);
        if (element) return {element, mode: 'auto'};
      }
      return {element: null, mode: 'auto'};
    };
    const text = element => element ? String(element.textContent || '').trim().slice(0, 65536) : null;
    const media = element => {
      if (!(element instanceof HTMLImageElement) &&
          !(element instanceof HTMLVideoElement)) return null;
      const url = String(element.currentSrc || element.src || '');
      return url ? {__dao_media_url: url} : null;
    };
    const snapshot = element => element ? {
      text: text(element),
      tag: String(element.tagName || '').toLowerCase(),
      href: element instanceof HTMLAnchorElement ? element.href : null,
      media: media(element),
    } : null;
    switch (operation) {
      case 'exists': return !!select(args[0]);
      case 'getText': return text(select(args[0]));
      case 'getAttribute': {
        const element = select(args[0]);
        const name = String(args[1] || '').toLowerCase();
        if (!['href', 'src', 'alt', 'title', 'datetime', 'aria-label', 'role'].includes(name))
          throw new Error('Attribute rejected');
        if (name === 'src') {
          if (!element || (!(element instanceof HTMLImageElement) &&
              !(element instanceof HTMLVideoElement))) {
            throw new Error('Raw source attributes are unavailable');
          }
          return media(element);
        }
        return element ? element.getAttribute(name) : null;
      }
      case 'query': return snapshot(select(args[0]));
      case 'snapshot': return snapshot(select(args[0] || 'body'));
      case 'queryAll': {
        const roots = [...document.querySelectorAll(String(args[0]))].slice(0, 500);
        const fields = args[1] && typeof args[1] === 'object' ? args[1] : {};
        return roots.map(root => {
          if (sensitive(root)) throw new Error('Sensitive control rejected');
          const item = {};
          for (const [key, field] of Object.entries(fields)) {
            const {element, mode} = selectField(field, root);
            item[key] = mode === 'text' ? text(element) :
                mode === 'href' ?
                    (element instanceof HTMLAnchorElement ? element.href : null) :
                mode === 'media' ? media(element) :
                element instanceof HTMLAnchorElement ? element.href :
                element instanceof HTMLImageElement ||
                    element instanceof HTMLVideoElement ? media(element) :
                    text(element);
          }
          return Object.keys(fields).length ? item : snapshot(root);
        });
      }
      case 'getComputedStyle': {
        const element = select(args[0]);
        if (!element) return null;
        const allowed = new Set(['color', 'background-color', 'font-size',
          'font-weight', 'display', 'grid-template-columns']);
        const names = Array.isArray(args[1]) ? args[1].slice(0, 12) : [];
        const style = window.getComputedStyle(element);
        const out = {};
        for (const name of names) if (allowed.has(name)) out[name] = style.getPropertyValue(name);
        return out;
      }
      case 'scroll': {
        const amount = Math.max(-2000, Math.min(2000, Number(args[0]) || innerHeight));
        window.scrollBy({top: amount, behavior: 'instant'});
        return {scrollY};
      }
      case 'waitFor': {
        return !!select(args[0]);
      }
    }
    throw new Error('Unknown page operation');
  })())js";
  base::ReplaceSubstringsAfterOffset(&script, 0, "__ARGS__", args_json);
  std::string operation_json;
  base::JSONWriter::Write(operation, &operation_json);
  base::ReplaceSubstringsAfterOffset(&script, 0, "__OP__", operation_json);
  return script;
}

void DaoHomeConnectorExecutor::OnPageResult(base::Value result) {
  if (!page_callback_) {
    return;
  }
  Callback callback = std::move(page_callback_);
  ReplaceMediaReferences(result);
  std::string serialized;
  if (!base::JSONWriter::Write(result, &serialized) ||
      serialized.size() > static_cast<size_t>(limits_.max_result_bytes)) {
    std::move(callback).Run(
        Error("quota_exceeded", "The connector page result is too large."));
    Cancel();
    return;
  }
  std::move(callback).Run(std::move(result));
}

void DaoHomeConnectorExecutor::PollWaitFor() {
  if (!page_callback_) {
    return;
  }
  if (!source_ || !committed_allowed_document_ || collection_finished_ ||
      !OwnerIsActive()) {
    Callback callback = std::move(page_callback_);
    std::move(callback).Run(
        Error("cancelled", "The active Dao Home connector session ended."));
    Cancel();
    return;
  }
  content::RenderFrameHost* frame = source_->GetPrimaryMainFrame();
  if (!frame || !frame->IsRenderFrameLive()) {
    Callback callback = std::move(page_callback_);
    std::move(callback).Run(
        Error("temporarily_unavailable", "The connector page is unavailable."));
    return;
  }
  base::ListValue arguments;
  arguments.Append(wait_for_selector_);
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(BuildAdapterScript("waitFor", arguments)),
      base::BindOnce(&DaoHomeConnectorExecutor::OnWaitForResult,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoHomeConnectorExecutor::OnWaitForResult(base::Value result) {
  if (!page_callback_) {
    return;
  }
  if (result.is_bool() && result.GetBool()) {
    wait_for_selector_.clear();
    Callback callback = std::move(page_callback_);
    std::move(callback).Run(base::Value(true));
    return;
  }
  if (base::TimeTicks::Now() >= wait_for_deadline_) {
    wait_for_selector_.clear();
    Callback callback = std::move(page_callback_);
    std::move(callback).Run(base::Value(false));
    return;
  }
  wait_for_timer_.Start(FROM_HERE, base::Milliseconds(50),
                        base::BindOnce(&DaoHomeConnectorExecutor::PollWaitFor,
                                       weak_factory_.GetWeakPtr()));
}

void DaoHomeConnectorExecutor::ReplaceMediaReferences(base::Value& value,
                                                      int depth) {
  if (depth > 12) {
    return;
  }
  if (base::DictValue* dict = value.GetIfDict()) {
    const std::string* media_url = dict->FindString("__dao_media_url");
    if (media_url && dict->size() == 1) {
      GURL url(*media_url);
      if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() ||
          media_handles_.size() >= kMaxMediaHandles) {
        value = base::Value();
        return;
      }
      const std::string handle =
          "dao-media:" + base::Uuid::GenerateRandomV4().AsLowercaseString();
      media_handles_.emplace(handle, std::move(url));
      value = base::Value(handle);
      return;
    }
    for (auto [key, child] : *dict) {
      ReplaceMediaReferences(child, depth + 1);
    }
    return;
  }
  if (base::ListValue* list = value.GetIfList()) {
    for (base::Value& child : *list) {
      ReplaceMediaReferences(child, depth + 1);
    }
  }
}

void DaoHomeConnectorExecutor::Finish(const std::string& execution_id,
                                      base::Value result,
                                      Callback callback) {
  if (execution_id != execution_id_ || !OwnerIsActive() || page_callback_ ||
      finish_callback_ || collection_finished_) {
    std::move(callback).Run(
        Error("cancelled", "The active Dao Home connector session ended."));
    Cancel();
    return;
  }
  std::string serialized;
  if (!base::JSONWriter::Write(result, &serialized) ||
      serialized.size() > static_cast<size_t>(limits_.max_result_bytes)) {
    std::move(callback).Run(Error(
        "invalid_response", "The connector result exceeds its byte budget."));
    Cancel();
    return;
  }
  if (!ValidateSchemaValue(schema_, result, 0,
                           limits_.max_items_per_connector)) {
    std::move(callback).Run(Error(
        "invalid_response", "The connector result does not match its schema."));
    Cancel();
    return;
  }
  finish_result_ = std::move(result);
  finish_callback_ = std::move(callback);
  if (media_handles_.empty()) {
    CompleteFinish();
    return;
  }
  std::set<std::string> unique_urls;
  for (const auto& [handle, url] : media_handles_) {
    if (unique_urls.insert(url.spec()).second) {
      pending_media_urls_.push_back(url);
    }
  }
  SnapshotNextMedia();
}

void DaoHomeConnectorExecutor::SnapshotNextMedia() {
  if (!finish_callback_) {
    return;
  }
  if (next_media_snapshot_ >= pending_media_urls_.size()) {
    CompleteFinish();
    return;
  }
  const GURL& url = pending_media_urls_[next_media_snapshot_];
  const size_t remaining_bytes = kMaxMediaBytes - retained_media_bytes_;
  const size_t max_pixels = remaining_bytes / 4;
  if (max_pixels == 0) {
    resolved_media_.insert_or_assign(
        url.spec(), Error("quota_exceeded",
                          "The Home media exceeds its session byte budget."));
    ++next_media_snapshot_;
    SnapshotNextMedia();
    return;
  }
  content::RenderFrameHost* frame =
      source_ ? source_->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    FailStart("temporarily_unavailable",
              "The connector source page is unavailable.");
    return;
  }
  frame->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(BuildMediaSnapshotScript(url, max_pixels)),
      base::BindOnce(&DaoHomeConnectorExecutor::OnMediaSnapshotResult,
                     weak_factory_.GetWeakPtr(), url.spec()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoHomeConnectorExecutor::ResolveMedia(const std::string& handle,
                                            Callback callback) {
  const auto media = media_handles_.find(handle);
  const auto found = media == media_handles_.end()
                         ? resolved_media_.end()
                         : resolved_media_.find(media->second.spec());
  if (!collection_finished_ || !OwnerIsActive() ||
      media == media_handles_.end() || found == resolved_media_.end()) {
    std::move(callback).Run(
        Error("not_found", "The Home media handle is unavailable."));
    return;
  }
  std::move(callback).Run(found->second.Clone());
}

std::string DaoHomeConnectorExecutor::BuildMediaSnapshotScript(
    const GURL& url,
    size_t max_pixels) const {
  std::string url_json;
  base::JSONWriter::Write(url.spec(), &url_json);
  std::string script = R"js((() => {
    try {
      const url = __URL__;
      const media = [...document.images, ...document.querySelectorAll('video')]
          .find(element => String(element.currentSrc || element.src || '') === url);
      if (!media) return {__dao_media_error: 'not_found'};
      const width = Number(media.videoWidth || media.naturalWidth || media.width);
      const height = Number(media.videoHeight || media.naturalHeight || media.height);
      if (!Number.isFinite(width) || !Number.isFinite(height) ||
          width <= 0 || height <= 0 || width * height > __MAX_PIXELS__)
        return {__dao_media_error: 'quota_exceeded'};
      const canvas = document.createElement('canvas');
      canvas.width = width;
      canvas.height = height;
      canvas.getContext('2d').drawImage(media, 0, 0, width, height);
      const dataUrl = canvas.toDataURL('image/png');
      const separator = dataUrl.indexOf(',');
      if (separator < 0) return {__dao_media_error: 'read_failed'};
      return {mime: 'image/png',
              base64: dataUrl.slice(separator + 1)};
    } catch {
      return {__dao_media_error: 'temporarily_unavailable'};
    }
  })())js";
  base::ReplaceSubstringsAfterOffset(&script, 0, "__URL__", url_json);
  base::ReplaceSubstringsAfterOffset(&script, 0, "__MAX_PIXELS__",
                                     base::NumberToString(max_pixels));
  return script;
}

bool DaoHomeConnectorExecutor::OwnsMediaHandle(
    const std::string& handle) const {
  return media_handles_.contains(handle);
}

void DaoHomeConnectorExecutor::OnMediaSnapshotResult(std::string url,
                                                     base::Value result) {
  if (!finish_callback_) {
    return;
  }
  const base::DictValue* snapshot = result.GetIfDict();
  const std::string* error =
      snapshot ? snapshot->FindString("__dao_media_error") : nullptr;
  const std::string* mime = snapshot ? snapshot->FindString("mime") : nullptr;
  const std::string* encoded =
      snapshot ? snapshot->FindString("base64") : nullptr;
  std::string decoded;
  if (error || !mime || *mime != "image/png" || !encoded ||
      !base::Base64Decode(*encoded, &decoded)) {
    resolved_media_.insert_or_assign(
        url, Error(error ? *error : "invalid_response",
                   "The Home media could not be resolved."));
  } else if (decoded.size() > kMaxMediaBytes - retained_media_bytes_) {
    resolved_media_.insert_or_assign(
        url, Error("quota_exceeded",
                   "The Home media exceeds its session byte budget."));
  } else {
    retained_media_bytes_ += decoded.size();
    resolved_media_.insert_or_assign(
        url, base::Value(
                 base::DictValue().Set("mime", *mime).Set("base64", *encoded)));
  }
  ++next_media_snapshot_;
  SnapshotNextMedia();
}

void DaoHomeConnectorExecutor::CompleteFinish() {
  if (!finish_callback_) {
    return;
  }
  Callback callback = std::move(finish_callback_);
  base::Value result = std::move(finish_result_);
  collection_finished_ = true;
  timeout_.Stop();
  Observe(nullptr);
  source_.reset();
  pending_media_urls_.clear();
  std::move(callback).Run(base::Value(base::DictValue()
                                          .Set("execution_id", execution_id_)
                                          .Set("result", std::move(result))));
}

void DaoHomeConnectorExecutor::DidFinishNavigation(
    content::NavigationHandle* handle) {
  if (!handle->IsInPrimaryMainFrame() || !handle->HasCommitted()) {
    return;
  }
  if (handle->IsErrorPage() || !UrlIsAllowed(handle->GetURL())) {
    FailStart("site_changed", "The connector page left its granted scope.");
    return;
  }
  const net::HttpResponseHeaders* headers = handle->GetResponseHeaders();
  if (headers &&
      (headers->response_code() == 401 || headers->response_code() == 403)) {
    FailStart("auth_required", "Sign in to reconnect this Home source.");
    return;
  }
  committed_allowed_document_ = true;
}

void DaoHomeConnectorExecutor::DidStopLoading() {
  if ((start_callback_ || (page_callback_ && navigation_pending_)) &&
      committed_allowed_document_ && !auth_check_pending_) {
    CheckAuthState();
  }
}

void DaoHomeConnectorExecutor::CheckAuthState() {
  content::RenderFrameHost* frame =
      source_ ? source_->GetPrimaryMainFrame() : nullptr;
  if (!frame || !frame->IsRenderFrameLive()) {
    FailStart("temporarily_unavailable", "The connector page is unavailable.");
    return;
  }
  auth_check_pending_ = true;
  frame->ExecuteJavaScriptInIsolatedWorld(
      uR"js((() => Boolean(
        document.querySelector('input[type="password"], input[autocomplete="current-password"], form[action*="login" i], form[action*="signin" i]')
      ))())js",
      base::BindOnce(&DaoHomeConnectorExecutor::OnAuthState,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void DaoHomeConnectorExecutor::OnAuthState(base::Value result) {
  auth_check_pending_ = false;
  if (result.is_bool() && result.GetBool()) {
    FailStart("auth_required", "Sign in to reconnect this Home source.");
    return;
  }
  if (start_callback_) {
    ReplyStarted();
    return;
  }
  if (page_callback_ && navigation_pending_ && committed_allowed_document_) {
    navigation_pending_ = false;
    Callback callback = std::move(page_callback_);
    std::move(callback).Run(base::Value(
        base::DictValue().Set("url", source_->GetLastCommittedURL().spec())));
  }
}

void DaoHomeConnectorExecutor::WebContentsDestroyed() {
  Observe(nullptr);
  if (start_callback_ || page_callback_ || finish_callback_) {
    FailStart("temporarily_unavailable", "The connector page was destroyed.");
  } else {
    Reset();
  }
}

void DaoHomeConnectorExecutor::ReplyStarted() {
  if (!start_callback_) {
    return;
  }
  if (!OwnerIsActive()) {
    FailStart("cancelled", "Dao Home is no longer active.");
    return;
  }
  base::Value response(base::DictValue()
                           .Set("execution_id", execution_id_)
                           .Set("revision", revision_)
                           .Set("connector_id", connector_.id)
                           .Set("module", module_source_)
                           .Set("input", input_.Clone()));
  std::move(start_callback_).Run(std::move(response));
}

void DaoHomeConnectorExecutor::Fail(std::string code,
                                    std::string message,
                                    Callback callback) {
  std::move(callback).Run(Error(std::move(code), std::move(message)));
}

void DaoHomeConnectorExecutor::FailStart(std::string code,
                                         std::string message) {
  if (!start_callback_ && !page_callback_ && !finish_callback_) {
    Reset();
    return;
  }
  Callback callback = start_callback_  ? std::move(start_callback_)
                      : page_callback_ ? std::move(page_callback_)
                                       : std::move(finish_callback_);
  Reset();
  Fail(std::move(code), std::move(message), std::move(callback));
}

void DaoHomeConnectorExecutor::Cancel() {
  if (!running()) {
    return;
  }
  if (start_callback_ || page_callback_ || finish_callback_) {
    FailStart("cancelled", "The Home connector session was cancelled.");
    return;
  }
  Reset();
}

void DaoHomeConnectorExecutor::Reset() {
  wait_for_timer_.Stop();
  timeout_.Stop();
  weak_factory_.InvalidateWeakPtrs();
  Observe(nullptr);
  owner_observer_.reset();
  source_.reset();
  owner_.reset();
  profile_ = nullptr;
  execution_id_.clear();
  revision_.clear();
  connector_ = HomeConnector();
  module_source_.clear();
  schema_ = base::Value();
  input_ = base::Value();
  start_callback_.Reset();
  page_callback_.Reset();
  finish_callback_.Reset();
  finish_result_ = base::Value();
  media_handles_.clear();
  resolved_media_.clear();
  pending_media_urls_.clear();
  next_media_snapshot_ = 0;
  retained_media_bytes_ = 0;
  operation_count_ = 0;
  scroll_count_ = 0;
  committed_allowed_document_ = false;
  navigation_pending_ = false;
  auth_check_pending_ = false;
  collection_finished_ = false;
  wait_for_selector_.clear();
  wait_for_deadline_ = base::TimeTicks();
}

}  // namespace dao
