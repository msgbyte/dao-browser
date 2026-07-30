// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_page_tools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <set>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/ui/views/dao_tab_identity.h"

namespace dao {
namespace {

constexpr size_t kMaxPageTextBytes = 512 * 1024;
constexpr int kMaxHighlightCleanupAttempts = 2;
constexpr base::TimeDelta kHighlightCleanupTimeout = base::Seconds(2);
constexpr base::TimeDelta kHighlightCleanupRetryDelay = base::Milliseconds(50);

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
  let currentGeneration = '';
  function updatePos() {
    if (!currentEl || !currentEl.isConnected) {
      highlight.style.display = 'none';
      return;
    }
    const r = currentEl.getBoundingClientRect();
    highlight.style.left = r.left + 'px';
    highlight.style.top = r.top + 'px';
    highlight.style.width = r.width + 'px';
    highlight.style.height = r.height + 'px';
    highlight.style.borderRadius = getComputedStyle(currentEl).borderRadius || '0';
    rafId = requestAnimationFrame(updatePos);
  }
  window.__dao_agent__ = {
    showHighlight(selector, generation) {
      const el = document.querySelector(selector);
      if (!el) return false;
      currentGeneration = generation || '';
      currentEl = el;
      highlight.style.display = 'block';
      highlight.style.opacity = '1';
      cancelAnimationFrame(rafId);
      updatePos();
      return true;
    },
    clearHighlight(generation) {
      if (generation && generation !== currentGeneration) return false;
      highlight.style.opacity = '0';
      currentEl = null;
      currentGeneration = '';
      cancelAnimationFrame(rafId);
      setTimeout(() => {
        if (!currentEl) highlight.style.display = 'none';
      }, 150);
      return true;
    },
    hasHighlight() {
      return currentEl !== null;
    }
  };
})()
)js";

// This intentionally mirrors the Agent accessibility representation. It
// assigns stable-for-the-current-snapshot data-dao-ref attributes and returns a
// compact textual tree rather than the very large raw CDP AX payload.
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

std::string QuoteForJavaScript(std::string_view value) {
  std::string json;
  base::JSONWriter::Write(base::Value(std::string(value)), &json);
  return json;
}

std::string ClearHighlightScript(std::string_view generation) {
  return "window.__dao_agent__ && window.__dao_agent__.clearHighlight(" +
         QuoteForJavaScript(generation) + ")";
}

std::string NewHighlightGeneration() {
  return "dao-highlight-" + base::Uuid::GenerateRandomV4().AsLowercaseString();
}

bool HighlightCleanupSucceeded(const DaoDevToolsClient::CommandResult& result) {
  if (!result.has_value() || !result->is_dict()) {
    return false;
  }
  return !result->GetDict().Find("exceptionDetails");
}

DaoDevToolsClient::CommandResult DecodeRuntimeEvaluate(
    DaoDevToolsClient::CommandResult result) {
  if (!result.has_value()) {
    return result;
  }
  if (!result->is_dict()) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kInternalError,
                         "Runtime evaluation returned an invalid response."));
  }
  const base::DictValue* exception_details =
      result->GetDict().FindDict("exceptionDetails");
  if (!exception_details) {
    return result;
  }
  std::string message = "Runtime evaluation failed.";
  if (const std::string* description =
          exception_details->FindStringByDottedPath("exception.description")) {
    message = *description;
  } else if (const std::string* text = exception_details->FindString("text")) {
    message = *text;
  }
  return base::unexpected(
      MakeDaoToolError(DaoToolErrorCode::kInternalError, std::move(message)));
}

const base::Value* RemoteValue(const base::Value& response) {
  return response.is_dict()
             ? response.GetDict().FindByDottedPath("result.value")
             : nullptr;
}

std::optional<std::string> RemoteString(
    const DaoDevToolsClient::CommandResult& response) {
  if (!response.has_value()) {
    return std::nullopt;
  }
  const base::Value* value = RemoteValue(response.value());
  if (!value || !value->is_string()) {
    return std::nullopt;
  }
  return value->GetString();
}

DaoBrowserToolResult ErrorResult(DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  return result;
}

DaoToolError InternalError(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInternalError, std::move(message));
}

DaoToolError InvalidArgument(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                          std::move(message));
}

bool TruncatePageText(std::string* text) {
  if (text->size() <= kMaxPageTextBytes) {
    return false;
  }
  text->resize(kMaxPageTextBytes);
  return true;
}

}  // namespace

class DaoHighlightCleanupQueue
    : public base::RefCounted<DaoHighlightCleanupQueue> {
 public:
  DaoHighlightCleanupQueue();

  void Queue(base::WeakPtr<content::WebContents> target,
             std::string generation) {
    if (!target || generation.empty()) {
      return;
    }
    const auto already_queued = std::find_if(
        pending_.begin(), pending_.end(),
        [target, &generation](const Entry& entry) {
          return entry.target && entry.target.get() == target.get() &&
                 entry.generation == generation;
        });
    if (already_queued == pending_.end() &&
        (!active_ || active_->target.get() != target.get() ||
         active_->generation != generation)) {
      pending_.emplace_back(std::move(target), std::move(generation), 0);
    }
    Continue();
  }

 private:
  friend class base::RefCounted<DaoHighlightCleanupQueue>;

  struct Entry {
    Entry(base::WeakPtr<content::WebContents> target,
          std::string generation,
          int attempts);
    ~Entry();
    Entry(Entry&& other) noexcept;
    Entry& operator=(Entry&& other) noexcept;

    Entry(const Entry&) = delete;
    Entry& operator=(const Entry&) = delete;

    base::WeakPtr<content::WebContents> target;
    std::string generation;
    int attempts;
  };

  ~DaoHighlightCleanupQueue();

  void Continue() {
    if (active_ || retry_or_deadline_timer_.IsRunning()) {
      return;
    }
    while (!pending_.empty()) {
      Entry entry = std::move(pending_.front());
      pending_.erase(pending_.begin());
      if (!entry.target) {
        continue;
      }
      if (!devtools_client_.AttachTo(entry.target.get())) {
        RetryOrContinue(std::move(entry));
        return;
      }

      active_ = std::move(entry);
      const uint64_t cleanup_id = next_cleanup_id_++;
      active_cleanup_id_ = cleanup_id;
      base::DictValue params;
      params.Set("expression", ClearHighlightScript(active_->generation));
      params.Set("returnByValue", true);
      const int command_id = devtools_client_.SendCommand(
          "Runtime.evaluate", std::move(params),
          base::BindOnce(&DaoHighlightCleanupQueue::OnCommandComplete,
                         base::WrapRefCounted(this), cleanup_id));
      if (command_id > 0 && active_cleanup_id_ == cleanup_id) {
        retry_or_deadline_timer_.Start(
            FROM_HERE, kHighlightCleanupTimeout,
            base::BindOnce(&DaoHighlightCleanupQueue::OnDeadline,
                           base::WrapRefCounted(this), cleanup_id));
      }
      return;
    }
    devtools_client_.Detach();
  }

  void OnCommandComplete(uint64_t cleanup_id,
                         DaoDevToolsClient::CommandResult result) {
    if (cleanup_id != active_cleanup_id_ || !active_) {
      return;
    }
    retry_or_deadline_timer_.Stop();
    Entry completed = std::move(*active_);
    active_.reset();
    active_cleanup_id_ = 0;
    devtools_client_.Detach();
    if (HighlightCleanupSucceeded(result)) {
      Continue();
      return;
    }
    RetryOrContinue(std::move(completed));
  }

  void OnDeadline(uint64_t cleanup_id) {
    if (cleanup_id != active_cleanup_id_ || !active_) {
      return;
    }
    Entry timed_out = std::move(*active_);
    active_.reset();
    active_cleanup_id_ = 0;
    devtools_client_.Detach();
    RetryOrContinue(std::move(timed_out));
  }

  void RetryOrContinue(Entry entry) {
    if (++entry.attempts < kMaxHighlightCleanupAttempts && entry.target) {
      pending_.push_back(std::move(entry));
      retry_or_deadline_timer_.Start(
          FROM_HERE, kHighlightCleanupRetryDelay,
          base::BindOnce(&DaoHighlightCleanupQueue::Continue,
                         base::WrapRefCounted(this)));
      return;
    }
    Continue();
  }

  DaoDevToolsClient devtools_client_;
  std::vector<Entry> pending_;
  std::optional<Entry> active_;
  base::OneShotTimer retry_or_deadline_timer_;
  uint64_t next_cleanup_id_ = 1;
  uint64_t active_cleanup_id_ = 0;
};

DaoHighlightCleanupQueue::DaoHighlightCleanupQueue() = default;

DaoHighlightCleanupQueue::~DaoHighlightCleanupQueue() {
  retry_or_deadline_timer_.Stop();
  devtools_client_.Detach();
}

DaoHighlightCleanupQueue::Entry::Entry(
    base::WeakPtr<content::WebContents> target,
    std::string generation,
    int attempts)
    : target(std::move(target)),
      generation(std::move(generation)),
      attempts(attempts) {}

DaoHighlightCleanupQueue::Entry::~Entry() = default;

DaoHighlightCleanupQueue::Entry::Entry(Entry&& other) noexcept = default;

DaoHighlightCleanupQueue::Entry& DaoHighlightCleanupQueue::Entry::operator=(
    Entry&& other) noexcept = default;

struct DaoPageTools::Operation : public content::WebContentsObserver {
  Operation(std::string request_id,
            std::string name,
            content::WebContents* target,
            url::Origin committed_origin,
            int64_t document_sequence_number,
            TargetResolver target_resolver,
            base::DictValue arguments,
            ResultCallback callback,
            base::WeakPtr<DaoPageTools> owner)
      : content::WebContentsObserver(target),
        request_id(std::move(request_id)),
        name(std::move(name)),
        target(target ? target->GetWeakPtr() : nullptr),
        committed_origin(std::move(committed_origin)),
        document_sequence_number(document_sequence_number),
        target_resolver(std::move(target_resolver)),
        arguments(std::move(arguments)),
        callback(std::move(callback)),
        owner(std::move(owner)) {}

  ~Operation() override = default;

  void PrimaryPageChanged(content::Page& page) override {
    Observe(nullptr);
    base::WeakPtr<DaoPageTools> owner_copy = owner;
    const std::string request_id_copy = request_id;
    if (owner_copy && owner_copy->FindOperation(request_id_copy)) {
      owner_copy->FinishError(
          request_id_copy,
          MakeDaoToolError(DaoToolErrorCode::kTargetForbidden,
                           "The authorized page document changed during the "
                           "operation."));
    }
  }

  std::string request_id;
  std::string name;
  base::WeakPtr<content::WebContents> target;
  url::Origin committed_origin;
  int64_t document_sequence_number = -1;
  TargetResolver target_resolver;
  base::DictValue arguments;
  ResultCallback callback;
  std::set<int> command_ids;
  bool owns_lock = false;
  bool temporary_highlight = false;
  bool persistent_highlight_committed = false;
  std::string highlight_generation;
  base::WeakPtr<DaoPageTools> owner;
};

struct DaoPageTools::LockEntry {
  base::WeakPtr<content::WebContents> target;
  int owners = 0;
};

DaoPageTools::HighlightEntry::HighlightEntry(
    base::WeakPtr<content::WebContents> target,
    std::string generation)
    : target(std::move(target)), generation(std::move(generation)) {}

DaoPageTools::HighlightEntry::~HighlightEntry() = default;

DaoPageTools::HighlightEntry::HighlightEntry(HighlightEntry&& other) noexcept =
    default;

DaoPageTools::HighlightEntry& DaoPageTools::HighlightEntry::operator=(
    HighlightEntry&& other) noexcept = default;

DaoPageTools::DaoPageTools(DaoDevToolsClient* devtools_client,
                           UiDelegate* ui_delegate)
    : devtools_client_(devtools_client),
      ui_delegate_(ui_delegate),
      highlight_cleanup_queue_(
          base::MakeRefCounted<DaoHighlightCleanupQueue>()) {}

DaoPageTools::~DaoPageTools() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_shutting_down_ = true;
  // A callback may destroy this object from an outer cancellation drain. The
  // destructor owns the remaining drain and must not inherit that gate.
  is_cancelling_ = false;
  CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                             "Page tool dispatcher was destroyed."));
  ClearAllHighlights();
  ClearCursors();
  weak_factory_.InvalidateWeakPtrs();
}

size_t DaoPageTools::operation_count_for_testing() const {
  return operations_.size();
}

size_t DaoPageTools::lock_count_for_testing() const {
  return lock_entries_.size();
}

size_t DaoPageTools::highlight_count_for_testing() const {
  return highlighted_targets_.size();
}

size_t DaoPageTools::cursor_count_for_testing() const {
  return cursor_targets_.size();
}

void DaoPageTools::TrackCursorForTesting(content::WebContents* target) {
  TrackCursor(target);
}

bool DaoPageTools::Handles(std::string_view name) {
  constexpr std::array<std::string_view, 15> kNames = {
      "get_page_info",      "get_page_html", "get_accessibility_tree",
      "capture_screenshot", "click_element", "agent_click",
      "click_by_ref",       "move_cursor",   "highlight_element",
      "scroll_down",        "scroll_up",     "scroll_to_element",
      "press_key_chord",    "type_text",     "execute_script",
  };
  return std::ranges::find(kNames, name) != kNames.end();
}

void DaoPageTools::Execute(std::string request_id,
                           std::string name,
                           content::WebContents* target,
                           url::Origin committed_origin,
                           int64_t document_sequence_number,
                           TargetResolver target_resolver,
                           base::DictValue arguments,
                           ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "Page tool dispatcher is cancelling pending operations.")));
    return;
  }
  if (!target || !Handles(name) || operations_.contains(request_id)) {
    std::move(callback).Run(
        ErrorResult(InternalError("Page tool dispatch is unavailable.")));
    return;
  }

  base::WeakPtr<DaoPageTools> weak_this = weak_factory_.GetWeakPtr();
  base::WeakPtr<content::WebContents> weak_target = target->GetWeakPtr();
  auto resolved_target = target_resolver.Run();
  if (!weak_this) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "Page tool dispatcher was destroyed during target resolution.")));
    return;
  }
  if (!weak_target) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The page target was destroyed during target resolution.")));
    return;
  }
  target = weak_target.get();
  content::RenderFrameHost* primary_frame = target->GetPrimaryMainFrame();
  content::NavigationEntry* committed_entry =
      target->GetController().GetLastCommittedEntry();
  if (!resolved_target.has_value() || resolved_target.value() != target ||
      !primary_frame || !committed_entry) {
    DaoToolError error =
        resolved_target.has_value()
            ? MakeDaoToolError(
                  DaoToolErrorCode::kTargetGone,
                  "The page document is unavailable for this operation.")
            : std::move(resolved_target).error();
    std::move(callback).Run(ErrorResult(std::move(error)));
    return;
  }

  // Each operation authorizes the currently committed eligible document.
  // A completed navigation between operations is therefore a valid new
  // snapshot, while navigation after this point is rejected by the common
  // pre-dispatch and completion-side validation.
  committed_origin = primary_frame->GetLastCommittedOrigin();
  document_sequence_number =
      committed_entry->GetMainFrameDocumentSequenceNumber();

  auto operation = std::make_unique<Operation>(
      request_id, name, target, std::move(committed_origin),
      document_sequence_number, std::move(target_resolver),
      std::move(arguments), std::move(callback), weak_factory_.GetWeakPtr());
  operations_.emplace(request_id, std::move(operation));

  if (name != "move_cursor") {
    weak_this = weak_factory_.GetWeakPtr();
    const bool attached = devtools_client_->AttachTo(target);
    if (!weak_this) {
      return;
    }
    if (!attached) {
      FinishError(request_id,
                  MakeDaoToolError(DaoToolErrorCode::kDevToolsAttachFailed,
                                   "Could not attach to the page target."));
      return;
    }
  }
  if (!FindOperation(request_id)) {
    return;
  }

  if (name == "get_page_info") {
    ExecuteGetPageInfo(request_id);
  } else if (name == "get_page_html") {
    ExecuteGetPageHtml(request_id);
  } else if (name == "get_accessibility_tree") {
    ExecuteAccessibilityTree(request_id);
  } else if (name == "capture_screenshot") {
    ExecuteCaptureScreenshot(request_id);
  } else if (name == "click_element") {
    ExecuteClickElement(request_id);
  } else if (name == "agent_click") {
    Operation* op = FindOperation(request_id);
    const std::string* selector = op->arguments.FindString("selector");
    if (!selector || selector->empty()) {
      FinishError(request_id, InvalidArgument("Selector must not be empty."));
      return;
    }
    ExecuteAnimatedClick(request_id, *selector);
  } else if (name == "click_by_ref") {
    Operation* op = FindOperation(request_id);
    const std::string* ref_id = op->arguments.FindString("ref_id");
    if (!ref_id || ref_id->empty()) {
      FinishError(request_id, InvalidArgument("ref_id must not be empty."));
      return;
    }
    ExecuteAnimatedClick(request_id,
                         "[data-dao-ref=" + QuoteForJavaScript(*ref_id) + "]");
  } else if (name == "move_cursor") {
    ExecuteMoveCursor(request_id);
  } else if (name == "highlight_element") {
    ExecuteHighlightElement(request_id);
  } else if (name == "scroll_down" || name == "scroll_up") {
    ExecuteScroll(request_id, name == "scroll_up");
  } else if (name == "scroll_to_element") {
    ExecuteScrollToElement(request_id);
  } else if (name == "press_key_chord") {
    ExecutePressKeyChord(request_id);
  } else if (name == "type_text") {
    ExecuteTypeText(request_id);
  } else {
    ExecuteScript(request_id);
  }
}

bool DaoPageTools::Cancel(std::string_view request_id, DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_) {
    return false;
  }
  return CancelInternal(request_id, std::move(error));
}

bool DaoPageTools::CancelInternal(std::string_view request_id,
                                  DaoToolError error) {
  auto it = operations_.find(request_id);
  if (it == operations_.end()) {
    return false;
  }
  std::unique_ptr<Operation> operation = std::move(it->second);
  operations_.erase(it);
  CancelCursorState(operation.get());
  CleanupOperation(operation.get());
  DaoBrowserToolResult result;
  result.error = std::move(error);
  std::move(operation->callback).Run(std::move(result));
  return true;
}

void DaoPageTools::CancelAll(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_cancelling_) {
    return;
  }
  is_cancelling_ = true;
  base::WeakPtr<DaoPageTools> weak_this = weak_factory_.GetWeakPtr();
  std::vector<std::string> request_ids;
  request_ids.reserve(operations_.size());
  for (const auto& [request_id, _] : operations_) {
    request_ids.push_back(request_id);
  }
  for (const std::string& request_id : request_ids) {
    CancelInternal(request_id, error);
    if (!weak_this) {
      return;
    }
  }
  if (weak_this) {
    weak_this->is_cancelling_ = false;
  }
}

void DaoPageTools::ClearHighlights(content::WebContents* target) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!target) {
    return;
  }
  std::vector<HighlightEntry> retained;
  retained.reserve(highlighted_targets_.size());
  for (HighlightEntry& entry : highlighted_targets_) {
    if (!entry.target) {
      continue;
    }
    if (entry.target.get() == target) {
      QueueHighlightCleanup(entry.target, std::move(entry.generation));
    } else {
      retained.push_back(std::move(entry));
    }
  }
  highlighted_targets_ = std::move(retained);
}

void DaoPageTools::ClearAllHighlights() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<HighlightEntry> targets = std::move(highlighted_targets_);
  highlighted_targets_.clear();
  for (HighlightEntry& entry : targets) {
    if (entry.target) {
      QueueHighlightCleanup(entry.target, std::move(entry.generation));
    }
  }
}

void DaoPageTools::QueueHighlightCleanup(
    base::WeakPtr<content::WebContents> target,
    std::string generation) {
  highlight_cleanup_queue_->Queue(std::move(target), std::move(generation));
}

void DaoPageTools::ClearCursors() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  std::vector<base::WeakPtr<content::WebContents>> targets =
      std::move(cursor_targets_);
  cursor_targets_.clear();
  if (!ui_delegate_) {
    return;
  }
  for (const base::WeakPtr<content::WebContents>& target : targets) {
    if (target) {
      ui_delegate_->CancelCursor(target.get());
    }
  }
}

DaoPageTools::Operation* DaoPageTools::FindOperation(
    std::string_view request_id) {
  auto it = operations_.find(request_id);
  return it == operations_.end() ? nullptr : it->second.get();
}

int DaoPageTools::SendCommand(std::string_view request_id,
                              std::string method,
                              base::DictValue params,
                              DaoDevToolsClient::ResponseCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_) {
    return 0;
  }
  if (!FindOperation(request_id)) {
    return 0;
  }
  base::WeakPtr<DaoPageTools> weak_this = weak_factory_.GetWeakPtr();
  if (!ValidateOperationTarget(request_id)) {
    return 0;
  }
  if (!weak_this) {
    return 0;
  }
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return 0;
  }
  const bool is_runtime_evaluate = method == "Runtime.evaluate";
  DaoDevToolsClient::ResponseCallback guarded_callback = base::BindOnce(
      [](base::WeakPtr<DaoPageTools> self, std::string request_id,
         bool is_runtime_evaluate, DaoDevToolsClient::ResponseCallback callback,
         DaoDevToolsClient::CommandResult result) {
        if (!self || !self->ValidateOperationTarget(request_id)) {
          return;
        }
        if (is_runtime_evaluate) {
          result = DecodeRuntimeEvaluate(std::move(result));
        }
        std::move(callback).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr(), std::string(request_id), is_runtime_evaluate,
      std::move(callback));
  weak_this = weak_factory_.GetWeakPtr();
  DaoDevToolsClient* devtools_client = devtools_client_;
  int command_id = devtools_client_->SendCommand(
      std::move(method), std::move(params), std::move(guarded_callback));
  if (!weak_this) {
    if (command_id > 0) {
      devtools_client->CancelCommand(
          command_id,
          MakeDaoToolError(
              DaoToolErrorCode::kToolCancelled,
              "Page tool dispatcher was destroyed while sending a command."));
    }
    return command_id;
  }
  operation = FindOperation(request_id);
  if (operation && command_id > 0) {
    operation->command_ids.insert(command_id);
  }
  return command_id;
}

void DaoPageTools::Finish(std::string_view request_id,
                          DaoBrowserToolResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = operations_.find(request_id);
  if (it == operations_.end()) {
    return;
  }
  std::unique_ptr<Operation> operation = std::move(it->second);
  operations_.erase(it);
  if (result.error) {
    CancelCursorState(operation.get());
  }
  CleanupOperation(operation.get());
  result.ok = !result.error.has_value();
  std::move(operation->callback).Run(std::move(result));
}

void DaoPageTools::FinishSuccess(std::string_view request_id,
                                 base::Value data) {
  DaoBrowserToolResult result;
  result.ok = true;
  result.data = std::move(data);
  Finish(request_id, std::move(result));
}

void DaoPageTools::FinishError(std::string_view request_id,
                               DaoToolError error) {
  Finish(request_id, ErrorResult(std::move(error)));
}

void DaoPageTools::CleanupOperation(Operation* operation) {
  ReleaseLock(operation);
  ClearTemporaryHighlight(operation);
  if (operation && operation->name == "highlight_element" &&
      !operation->persistent_highlight_committed &&
      !operation->highlight_generation.empty()) {
    std::erase_if(
        highlighted_targets_, [operation](const HighlightEntry& entry) {
          return !entry.target ||
                 (entry.target.get() == operation->target.get() &&
                  entry.generation == operation->highlight_generation);
        });
    QueueHighlightCleanup(operation->target, operation->highlight_generation);
  }
  const std::set<int> command_ids = std::move(operation->command_ids);
  for (int command_id : command_ids) {
    devtools_client_->CancelCommand(
        command_id, MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                     "Page tool command is no longer active."));
  }
}

void DaoPageTools::CancelCursorState(Operation* operation) {
  if (!operation || !operation->target || !ui_delegate_ ||
      (operation->name != "move_cursor" && operation->name != "agent_click" &&
       operation->name != "click_by_ref")) {
    return;
  }
  ui_delegate_->CancelCursor(operation->target.get());
  std::erase_if(cursor_targets_,
                [target = operation->target](
                    const base::WeakPtr<content::WebContents>& entry) {
                  return !entry || entry.get() == target.get();
                });
}

void DaoPageTools::TrackCursor(content::WebContents* target) {
  if (!target) {
    return;
  }
  const auto tracked =
      std::find_if(cursor_targets_.begin(), cursor_targets_.end(),
                   [target](const base::WeakPtr<content::WebContents>& entry) {
                     return entry && entry.get() == target;
                   });
  if (tracked == cursor_targets_.end()) {
    cursor_targets_.push_back(target->GetWeakPtr());
  }
}

bool DaoPageTools::ValidateOperationTarget(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return false;
  }
  if (!operation->target) {
    FinishError(request_id,
                MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                                 "Page target is no longer available."));
    return false;
  }
  if (operation->target_resolver) {
    TargetResolver target_resolver = operation->target_resolver;
    base::WeakPtr<content::WebContents> expected_target = operation->target;
    base::WeakPtr<DaoPageTools> weak_this = weak_factory_.GetWeakPtr();
    operation = nullptr;
    auto eligible_target = target_resolver.Run();
    if (!weak_this) {
      return false;
    }
    operation = FindOperation(request_id);
    if (!operation) {
      return false;
    }
    if (!eligible_target.has_value()) {
      FinishError(request_id, std::move(eligible_target).error());
      return false;
    }
    if (!expected_target || operation->target.get() != expected_target.get() ||
        eligible_target.value() != operation->target.get()) {
      FinishError(
          request_id,
          MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                           "The authorized browser target changed during the "
                           "operation."));
      return false;
    }
  }
  content::RenderFrameHost* primary_main_frame =
      operation->target->GetPrimaryMainFrame();
  const content::NavigationEntry* entry =
      operation->target->GetController().GetLastCommittedEntry();
  if (!primary_main_frame || !entry) {
    FinishError(
        request_id,
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "The authorized page document is unavailable."));
    return false;
  }
  if (primary_main_frame->GetLastCommittedOrigin() !=
      operation->committed_origin) {
    FinishError(request_id,
                MakeDaoToolError(DaoToolErrorCode::kTargetForbidden,
                                 "The authorized page origin changed during "
                                 "the operation."));
    return false;
  }
  if (entry->GetMainFrameDocumentSequenceNumber() !=
      operation->document_sequence_number) {
    FinishError(request_id,
                MakeDaoToolError(DaoToolErrorCode::kTargetForbidden,
                                 "The authorized page document changed during "
                                 "the operation."));
    return false;
  }
  return true;
}

bool DaoPageTools::AcquireLock(Operation* operation) {
  if (!operation || !operation->target || !ui_delegate_) {
    return false;
  }
  for (auto it = lock_entries_.begin(); it != lock_entries_.end();) {
    if (!it->target) {
      it = lock_entries_.erase(it);
      continue;
    }
    if (it->target.get() == operation->target.get()) {
      ++it->owners;
      operation->owns_lock = true;
      return true;
    }
    ++it;
  }
  if (ui_delegate_->IsTargetLocked(operation->target.get())) {
    return false;
  }
  ui_delegate_->LockTarget(operation->target.get());
  lock_entries_.push_back({operation->target, 1});
  operation->owns_lock = true;
  return true;
}

void DaoPageTools::ReleaseLock(Operation* operation) {
  if (!operation || !operation->owns_lock) {
    return;
  }
  operation->owns_lock = false;
  for (auto it = lock_entries_.begin(); it != lock_entries_.end(); ++it) {
    if (it->target.get() != operation->target.get()) {
      continue;
    }
    if (--it->owners == 0) {
      if (it->target) {
        ui_delegate_->UnlockTarget(it->target.get());
      }
      lock_entries_.erase(it);
    }
    return;
  }
}

void DaoPageTools::ClearTemporaryHighlight(Operation* operation) {
  if (!operation || !operation->temporary_highlight) {
    return;
  }
  operation->temporary_highlight = false;
  if (operation->target && !operation->highlight_generation.empty()) {
    QueueHighlightCleanup(operation->target,
                          std::move(operation->highlight_generation));
  }
}

void DaoPageTools::ExecuteGetPageInfo(std::string_view request_id) {
  base::DictValue params;
  params.Set("expression",
             "(() => document.querySelector('meta[name=\"description\" i]')"
             "?.getAttribute('content') || '')()");
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            Operation* operation = self->FindOperation(request_id);
            if (!operation || !operation->target) {
              self->FinishError(
                  request_id,
                  MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                                   "Page target is no longer available."));
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            base::DictValue data;
            data.Set("url", operation->target->GetURL().spec());
            data.Set("title", base::UTF16ToUTF8(operation->target->GetTitle()));
            data.Set("description",
                     RemoteString(result).value_or(std::string()));
            data.Set("tab_id",
                     GetOrCreateSidebarTabId(operation->target.get()));
            self->FinishSuccess(request_id, base::Value(std::move(data)));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteGetPageHtml(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  if (!operation || !operation->target) {
    return;
  }
  const std::string url = operation->target->GetURL().spec();
  const std::string title = base::UTF16ToUTF8(operation->target->GetTitle());
  base::DictValue params;
  params.Set("expression", "document.documentElement.outerHTML");
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             std::string url, std::string title,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            std::optional<std::string> html = RemoteString(result);
            if (!html) {
              self->FinishError(request_id,
                                InternalError("Failed to read page HTML."));
              return;
            }
            const bool truncated = TruncatePageText(&*html);
            base::DictValue data;
            data.Set("url", url);
            data.Set("title", title);
            data.Set("html", std::move(*html));
            if (truncated) {
              data.Set("truncated", true);
            }
            self->FinishSuccess(request_id, base::Value(std::move(data)));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id), url, title));
}

void DaoPageTools::ExecuteAccessibilityTree(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string filter = operation->arguments.FindString("filter")
                                 ? *operation->arguments.FindString("filter")
                                 : "interactive";
  base::DictValue params;
  params.Set("expression", std::string(kAccessibilityTreeScript) + "(" +
                               QuoteForJavaScript(filter) + ")");
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            std::optional<std::string> value = RemoteString(result);
            if (!value) {
              self->FinishError(
                  request_id,
                  InternalError("Accessibility tree evaluation failed."));
              return;
            }
            std::optional<base::Value> parsed =
                base::JSONReader::Read(*value, base::JSON_PARSE_RFC);
            if (parsed && parsed->is_dict()) {
              self->FinishSuccess(request_id, std::move(*parsed));
              return;
            }
            self->FinishSuccess(request_id, base::Value(base::DictValue().Set(
                                                "tree", std::move(*value))));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteCaptureScreenshot(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  base::DictValue params;
  params.Set("format", "jpeg");
  params.Set("quality", 60);
  if (const base::DictValue* clip = operation->arguments.FindDict("clip")) {
    const std::optional<double> x = clip->FindDouble("x");
    const std::optional<double> y = clip->FindDouble("y");
    const std::optional<double> width = clip->FindDouble("width");
    const std::optional<double> height = clip->FindDouble("height");
    const double scale = clip->FindDouble("scale").value_or(1.0);
    if (!x || !y || !width || !height || *width <= 0 || *height <= 0 ||
        scale <= 0) {
      FinishError(request_id, InvalidArgument("Invalid screenshot clip."));
      return;
    }
    params.Set("clip", base::DictValue()
                           .Set("x", *x)
                           .Set("y", *y)
                           .Set("width", *width)
                           .Set("height", *height)
                           .Set("scale", scale));
  }
  SendCommand(
      request_id, "Page.captureScreenshot", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult response) {
            if (!self) {
              return;
            }
            if (!response.has_value()) {
              self->FinishError(request_id, std::move(response).error());
              return;
            }
            const std::string* data =
                response->is_dict() ? response->GetDict().FindString("data")
                                    : nullptr;
            if (!data) {
              self->FinishError(
                  request_id,
                  InternalError("No screenshot data was returned."));
              return;
            }
            DaoBrowserToolResult result;
            result.ok = true;
            result.data = base::Value(
                base::DictValue().Set("data", *data).Set("format", "jpeg"));
            result.media =
                DaoToolMedia{.mime_type = "image/jpeg", .data = *data};
            self->Finish(request_id, std::move(result));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteClickElement(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string* selector = operation->arguments.FindString("selector");
  if (!selector || selector->empty()) {
    FinishError(request_id, InvalidArgument("Selector must not be empty."));
    return;
  }
  const std::string selector_copy = *selector;
  if (!ValidateOperationTarget(request_id)) {
    return;
  }
  const std::string script =
      "(() => { const el = document.querySelector(" +
      QuoteForJavaScript(selector_copy) +
      "); if (!el) return 'element not found'; el.click(); return 'clicked'; "
      "})()";
  base::DictValue params;
  params.Set("expression", script);
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult response) {
            if (!self) {
              return;
            }
            if (!response.has_value()) {
              self->FinishError(request_id, std::move(response).error());
              return;
            }
            self->FinishSuccess(
                request_id,
                base::Value(base::DictValue().Set(
                    "result", RemoteString(response).value_or(std::string()))));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteAnimatedClick(std::string_view request_id,
                                        std::string selector) {
  if (!FindOperation(request_id) || !ValidateOperationTarget(request_id)) {
    return;
  }
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  operation->temporary_highlight = true;
  operation->highlight_generation = NewHighlightGeneration();
  const std::string script =
      std::string(kHighlightInjectScript) +
      "; (() => { const selector = " + QuoteForJavaScript(selector) +
      "; const el = document.querySelector(selector);"
      "if (!el) return JSON.stringify({error:'element not found'});"
      "window.__dao_agent__.showHighlight(selector," +
      QuoteForJavaScript(operation->highlight_generation) +
      ");"
      "const r = el.getBoundingClientRect();"
      "return JSON.stringify({x:r.left+r.width/2,y:r.top+r.height/2}); })()";
  base::DictValue params;
  params.Set("expression", script);
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(&DaoPageTools::OnAnimatedClickBounds,
                     weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::OnAnimatedClickBounds(
    std::string request_id,
    DaoDevToolsClient::CommandResult result) {
  if (!ValidateOperationTarget(request_id)) {
    return;
  }
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  std::optional<std::string> json = RemoteString(result);
  std::optional<base::Value> parsed =
      json ? base::JSONReader::Read(*json, base::JSON_PARSE_RFC) : std::nullopt;
  if (!parsed || !parsed->is_dict() || parsed->GetDict().FindString("error")) {
    FinishError(request_id, InvalidArgument("Page element was not found."));
    return;
  }
  const double x = parsed->GetDict().FindDouble("x").value_or(0);
  const double y = parsed->GetDict().FindDouble("y").value_or(0);
  if (!ui_delegate_) {
    OnAnimatedCursorMoved(request_id, x, y, true);
    return;
  }
  ui_delegate_->MoveCursor(
      operation->target.get(), x, y,
      base::BindOnce(&DaoPageTools::OnAnimatedCursorMoved,
                     weak_factory_.GetWeakPtr(), request_id, x, y));
}

void DaoPageTools::OnAnimatedCursorMoved(std::string request_id,
                                         double x,
                                         double y,
                                         bool moved) {
  if (!ValidateOperationTarget(request_id)) {
    return;
  }
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  if (moved && ui_delegate_) {
    TrackCursor(operation->target.get());
    ui_delegate_->PlayClickRipple(operation->target.get());
  }
  DispatchMouseMove(request_id, x, y);
}

void DaoPageTools::DispatchMouseMove(std::string_view request_id,
                                     double x,
                                     double y) {
  base::DictValue params;
  params.Set("type", "mouseMoved");
  params.Set("x", static_cast<int>(x));
  params.Set("y", static_cast<int>(y));
  params.Set("button", "none");
  params.Set("buttons", 0);
  SendCommand(request_id, "Input.dispatchMouseEvent", std::move(params),
              base::BindOnce(&DaoPageTools::DispatchMousePress,
                             weak_factory_.GetWeakPtr(),
                             std::string(request_id), x, y));
}

void DaoPageTools::DispatchMousePress(std::string request_id,
                                      double x,
                                      double y,
                                      DaoDevToolsClient::CommandResult result) {
  if (!ValidateOperationTarget(request_id)) {
    return;
  }
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  base::DictValue params;
  params.Set("type", "mousePressed");
  params.Set("x", static_cast<int>(x));
  params.Set("y", static_cast<int>(y));
  params.Set("button", "left");
  params.Set("buttons", 1);
  params.Set("clickCount", 1);
  SendCommand(request_id, "Input.dispatchMouseEvent", std::move(params),
              base::BindOnce(&DaoPageTools::DispatchMouseRelease,
                             weak_factory_.GetWeakPtr(), request_id, x, y));
}

void DaoPageTools::DispatchMouseRelease(
    std::string request_id,
    double x,
    double y,
    DaoDevToolsClient::CommandResult result) {
  if (!ValidateOperationTarget(request_id)) {
    return;
  }
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  base::DictValue params;
  params.Set("type", "mouseReleased");
  params.Set("x", static_cast<int>(x));
  params.Set("y", static_cast<int>(y));
  params.Set("button", "left");
  params.Set("buttons", 0);
  params.Set("clickCount", 1);
  SendCommand(request_id, "Input.dispatchMouseEvent", std::move(params),
              base::BindOnce(&DaoPageTools::FinishAnimatedClick,
                             weak_factory_.GetWeakPtr(), request_id));
}

void DaoPageTools::FinishAnimatedClick(
    std::string request_id,
    DaoDevToolsClient::CommandResult result) {
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  base::DictValue params;
  params.Set("expression",
             ClearHighlightScript(operation->highlight_generation));
  params.Set("returnByValue", true);
  SendCommand(request_id, "Runtime.evaluate", std::move(params),
              base::BindOnce(
                  [](base::WeakPtr<DaoPageTools> self, std::string request_id,
                     DaoDevToolsClient::CommandResult result) {
                    if (!self) {
                      return;
                    }
                    Operation* operation = self->FindOperation(request_id);
                    if (!operation) {
                      return;
                    }
                    if (!HighlightCleanupSucceeded(result)) {
                      self->QueueHighlightCleanup(
                          operation->target,
                          std::move(operation->highlight_generation));
                    } else {
                      operation->highlight_generation.clear();
                    }
                    operation->temporary_highlight = false;
                    self->FinishSuccess(
                        request_id,
                        base::Value(base::DictValue().Set("success", true)));
                  },
                  weak_factory_.GetWeakPtr(), request_id));
}

void DaoPageTools::ExecuteMoveCursor(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::optional<double> x = operation->arguments.FindDouble("x");
  const std::optional<double> y = operation->arguments.FindDouble("y");
  if (!x || !y || !ui_delegate_ || !operation->target) {
    FinishError(request_id,
                InternalError("Agent cursor target is unavailable."));
    return;
  }
  ui_delegate_->MoveCursor(
      operation->target.get(), *x, *y,
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             bool moved) {
            if (!self) {
              return;
            }
            if (!self->ValidateOperationTarget(request_id)) {
              return;
            }
            Operation* operation = self->FindOperation(request_id);
            if (!operation) {
              return;
            }
            if (!moved) {
              self->FinishError(request_id,
                                InternalError("Agent cursor is unavailable."));
              return;
            }
            self->TrackCursor(operation->target.get());
            self->FinishSuccess(request_id, base::Value(true));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteHighlightElement(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string* selector = operation->arguments.FindString("selector");
  if (!selector || selector->empty()) {
    FinishError(request_id, InvalidArgument("Selector must not be empty."));
    return;
  }
  operation->highlight_generation = NewHighlightGeneration();
  for (auto it = highlighted_targets_.begin();
       it != highlighted_targets_.end();) {
    if (!it->target) {
      it = highlighted_targets_.erase(it);
    } else if (it->target.get() == operation->target.get()) {
      QueueHighlightCleanup(it->target, std::move(it->generation));
      it = highlighted_targets_.erase(it);
    } else {
      ++it;
    }
  }
  highlighted_targets_.push_back(
      {operation->target, operation->highlight_generation});
  base::DictValue params;
  params.Set("expression",
             std::string(kHighlightInjectScript) +
                 "; window.__dao_agent__.showHighlight(" +
                 QuoteForJavaScript(*selector) + "," +
                 QuoteForJavaScript(operation->highlight_generation) + ")");
  params.Set("returnByValue", true);
  SendCommand(request_id, "Runtime.evaluate", std::move(params),
              base::BindOnce(
                  [](base::WeakPtr<DaoPageTools> self, std::string request_id,
                     DaoDevToolsClient::CommandResult result) {
                    if (!self) {
                      return;
                    }
                    if (!result.has_value()) {
                      self->FinishError(request_id, std::move(result).error());
                      return;
                    }
                    if (!self->ValidateOperationTarget(request_id)) {
                      return;
                    }
                    Operation* operation = self->FindOperation(request_id);
                    if (!operation) {
                      return;
                    }
                    operation->persistent_highlight_committed = true;
                    self->FinishSuccess(
                        request_id,
                        base::Value(base::DictValue().Set("success", true)));
                  },
                  weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteScroll(std::string_view request_id, bool up) {
  Operation* operation = FindOperation(request_id);
  const double amount = operation->arguments.FindDouble("amount").value_or(0.0);
  const std::string amount_expression =
      amount > 0 && std::isfinite(amount)
          ? std::string(up ? "-" : "") + base::NumberToString(amount)
          : std::string(up ? "-" : "") + "Math.round(window.innerHeight * 0.8)";
  const std::string script =
      "(() => { const amount = " + amount_expression +
      "; window.scrollBy({top:amount,behavior:'smooth'});"
      "return JSON.stringify({scrollY:Math.round(window.scrollY+amount),"
      "scrollHeight:document.documentElement.scrollHeight,"
      "viewportHeight:window.innerHeight}); })()";
  base::DictValue params;
  params.Set("expression", script);
  params.Set("returnByValue", true);
  SendCommand(request_id, "Runtime.evaluate", std::move(params),
              base::BindOnce(
                  [](base::WeakPtr<DaoPageTools> self, std::string request_id,
                     DaoDevToolsClient::CommandResult result) {
                    if (!self) {
                      return;
                    }
                    if (!result.has_value()) {
                      self->FinishError(request_id, std::move(result).error());
                      return;
                    }
                    std::optional<std::string> json = RemoteString(result);
                    std::optional<base::Value> parsed =
                        json ? base::JSONReader::Read(*json,
                                                      base::JSON_PARSE_RFC)
                             : std::nullopt;
                    if (!parsed || !parsed->is_dict()) {
                      self->FinishError(request_id,
                                        InternalError("Page scroll failed."));
                      return;
                    }
                    self->FinishSuccess(request_id, std::move(*parsed));
                  },
                  weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteScrollToElement(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string* ref_id = operation->arguments.FindString("ref_id");
  const std::string* selector = operation->arguments.FindString("selector");
  std::string query;
  if (ref_id && !ref_id->empty()) {
    query = "[data-dao-ref=" + QuoteForJavaScript(*ref_id) + "]";
  } else if (selector && !selector->empty()) {
    query = *selector;
  } else {
    FinishError(request_id, InvalidArgument("selector or ref_id is required."));
    return;
  }
  const std::string script =
      "(() => { const el=document.querySelector(" + QuoteForJavaScript(query) +
      "); if(!el) return JSON.stringify({error:'element not found'});"
      "el.scrollIntoView({behavior:'smooth',block:'center'});"
      "return JSON.stringify({scrolled:true}); })()";
  base::DictValue params;
  params.Set("expression", script);
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            std::optional<std::string> json = RemoteString(result);
            std::optional<base::Value> parsed =
                json ? base::JSONReader::Read(*json, base::JSON_PARSE_RFC)
                     : std::nullopt;
            if (!parsed || !parsed->is_dict()) {
              self->FinishError(
                  request_id,
                  InternalError("Scroll-to-element evaluation failed."));
              return;
            }
            self->FinishSuccess(request_id, std::move(*parsed));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecutePressKeyChord(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string* keys = operation->arguments.FindString("keys");
  if (!keys || keys->empty()) {
    FinishError(request_id, InvalidArgument("Key chord must not be empty."));
    return;
  }
  const std::string script =
      "(() => { const combo=" + QuoteForJavaScript(*keys) +
      ".toLowerCase(); const parts=combo.split('+');"
      "const key=parts[parts.length-1].trim();"
      "const opts={bubbles:true,cancelable:true,"
      "ctrlKey:combo.includes('ctrl'),"
      "metaKey:combo.includes('cmd')||combo.includes('meta'),"
      "shiftKey:combo.includes('shift'),altKey:combo.includes('alt')};"
      "const map={enter:'Enter',tab:'Tab',escape:'Escape',esc:'Escape',"
      "backspace:'Backspace',delete:'Delete',space:' ',up:'ArrowUp',"
      "down:'ArrowDown',left:'ArrowLeft',right:'ArrowRight'};"
      "opts.key=map[key]||key; const el=document.activeElement||document.body;"
      "el.dispatchEvent(new KeyboardEvent('keydown',opts));"
      "el.dispatchEvent(new KeyboardEvent('keyup',opts));"
      "if(opts.key.length===1&&!opts.ctrlKey&&!opts.metaKey)"
      "el.dispatchEvent(new InputEvent('input',{data:opts.key,"
      "inputType:'insertText',bubbles:true}));"
      "return 'pressed: '+combo; })()";
  base::DictValue params;
  params.Set("expression", script);
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            base::DictValue data;
            data.Set("success", true);
            data.Set("result", RemoteString(result).value_or(std::string()));
            self->FinishSuccess(request_id, base::Value(std::move(data)));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::ExecuteTypeText(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string* text = operation->arguments.FindString("text");
  if (!text || text->empty()) {
    FinishError(request_id, InvalidArgument("Text must not be empty."));
    return;
  }
  if (!operation->arguments.FindBool("clear").value_or(false)) {
    InsertText(request_id);
    return;
  }
  base::DictValue params;
  params.Set(
      "expression",
      "(async () => { const el=document.activeElement; let selection;"
      "if(el&&(el.tagName==='INPUT'||el.tagName==='TEXTAREA')) "
      "selection=el.select();"
      "else selection=document.execCommand('selectAll');"
      "if(selection&&typeof selection.then==='function') await selection;"
      "return 'selected'; })()");
  params.Set("returnByValue", true);
  params.Set("awaitPromise", true);
  SendCommand(request_id, "Runtime.evaluate", std::move(params),
              base::BindOnce(
                  [](base::WeakPtr<DaoPageTools> self, std::string request_id,
                     DaoDevToolsClient::CommandResult result) {
                    if (!self) {
                      return;
                    }
                    if (!result.has_value()) {
                      self->FinishError(request_id, std::move(result).error());
                      return;
                    }
                    self->InsertText(request_id);
                  },
                  weak_factory_.GetWeakPtr(), std::string(request_id)));
}

void DaoPageTools::InsertText(std::string_view request_id) {
  if (!FindOperation(request_id) || !ValidateOperationTarget(request_id)) {
    return;
  }
  Operation* operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const std::string text = *operation->arguments.FindString("text");
  base::DictValue params;
  params.Set("text", text);
  SendCommand(
      request_id, "Input.insertText", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             std::string text, DaoDevToolsClient::CommandResult result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            self->FinishSuccess(
                request_id, base::Value(base::DictValue()
                                            .Set("success", true)
                                            .Set("typed", std::move(text))));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id), text));
}

void DaoPageTools::ExecuteScript(std::string_view request_id) {
  Operation* operation = FindOperation(request_id);
  const std::string* code = operation->arguments.FindString("code");
  if (!code || code->empty()) {
    FinishError(request_id, InvalidArgument("Script must not be empty."));
    return;
  }
  const bool lock_tab =
      operation->arguments.FindBool("lock_tab").value_or(false);
  const std::string code_copy = *code;
  if (lock_tab && !ValidateOperationTarget(request_id)) {
    return;
  }
  if (lock_tab) {
    operation = FindOperation(request_id);
    if (!operation) {
      return;
    }
    AcquireLock(operation);
  }
  base::DictValue params;
  params.Set("expression", code_copy);
  params.Set("returnByValue", true);
  SendCommand(
      request_id, "Runtime.evaluate", std::move(params),
      base::BindOnce(
          [](base::WeakPtr<DaoPageTools> self, std::string request_id,
             DaoDevToolsClient::CommandResult response) {
            if (!self) {
              return;
            }
            if (!response.has_value()) {
              self->FinishError(request_id, std::move(response).error());
              return;
            }
            base::DictValue data;
            const base::Value* value = RemoteValue(response.value());
            if (value) {
              if (value->is_string()) {
                data.Set("result", value->GetString());
              } else {
                std::string json;
                base::JSONWriter::Write(*value, &json);
                data.Set("result", std::move(json));
              }
            }
            if (response->is_dict()) {
              const base::Value* exception =
                  response->GetDict().Find("exceptionDetails");
              if (exception) {
                std::string json;
                base::JSONWriter::Write(*exception, &json);
                data.Set("error", std::move(json));
              }
            }
            self->FinishSuccess(request_id, base::Value(std::move(data)));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id)));
}

}  // namespace dao
