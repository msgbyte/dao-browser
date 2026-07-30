// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_devtools_tools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "third_party/re2/src/re2/re2.h"

namespace dao {
namespace {

constexpr size_t kMaxNetworkRequests = 200;
constexpr size_t kMaxConsoleMessages = 500;
constexpr size_t kMaxSourceBytes = 512 * 1024;
constexpr size_t kMaxTrackedFieldBytes = 16 * 1024;
constexpr size_t kMaxNetworkEntryBytes = 32 * 1024;
constexpr size_t kMaxConsoleEntryBytes = 32 * 1024;
constexpr size_t kMaxNetworkTrackingBytes = 512 * 1024;
constexpr size_t kMaxConsoleTrackingBytes = 1024 * 1024;
constexpr size_t kNetworkResourceBufferBytes = 2 * 1024 * 1024;
constexpr size_t kNetworkTotalBufferBytes = 20 * 1024 * 1024;
constexpr size_t kMaxSearchMatches = 1000;
constexpr size_t kMaxSearchResources = 10000;
constexpr size_t kMaxResourceUrlBytes = 2 * 1024 * 1024;
constexpr size_t kMaxResourceFrameDepth = 64;
constexpr size_t kMaxSearchScannedBytes = 4 * 1024 * 1024;
constexpr size_t kMaxResourceDecodedBytes = 1024 * 1024;
constexpr size_t kMaxResourceEncodedBytes =
    ((kMaxResourceDecodedBytes + 2) / 3) * 4;
constexpr size_t kMaxResourceProtocolResponseBytes = 2 * 1024 * 1024;
constexpr size_t kMaxSearchProtocolResponseBytes = 2 * 1024 * 1024;
constexpr size_t kMaxExcerptCodeUnits = 240;

struct ResourceTarget {
  std::string url;
  std::string frame_id;
  std::string type;
  std::string mime_type;
  std::optional<double> content_size;
};

struct ResourceTraversal {
  std::vector<ResourceTarget> resources;
  size_t processed = 0;
  size_t eligible = 0;
  size_t skipped = 0;
  size_t url_bytes = 0;
  bool resource_limit_hit = false;
};

DaoBrowserToolResult ErrorResult(DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  return result;
}

DaoToolError InvalidArgument(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                          std::move(message));
}

DaoToolError InternalError(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kInternalError, std::move(message));
}

base::DictValue NetworkEnableParams() {
  return base::DictValue()
      .Set("maxResourceBufferSize",
           static_cast<int>(kNetworkResourceBufferBytes))
      .Set("maxTotalBufferSize", static_cast<int>(kNetworkTotalBufferBytes));
}

bool GetCurrentDocument(content::WebContents *target, url::Origin *origin,
                        int64_t *document_sequence_number) {
  if (!target || !target->GetPrimaryMainFrame()) {
    return false;
  }
  const content::NavigationEntry *entry =
      target->GetController().GetLastCommittedEntry();
  if (!entry) {
    return false;
  }
  *origin = target->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  *document_sequence_number = entry->GetMainFrameDocumentSequenceNumber();
  return true;
}

bool TruncateText(std::string *text) {
  if (text->size() <= kMaxSourceBytes) {
    return false;
  }
  *text = base::TruncateUTF8ToByteSize(*text, kMaxSourceBytes);
  text->append("\n...[truncated]");
  return true;
}

bool SetBoundedString(base::DictValue *entry, std::string_view key,
                      std::string_view value, size_t limit) {
  const bool truncated = value.size() > limit;
  entry->Set(key, truncated ? base::TruncateUTF8ToByteSize(value, limit)
                            : std::string(value));
  return truncated;
}

size_t FlatEntryStringBytes(const base::DictValue &entry) {
  size_t bytes = 0;
  for (const auto [key, value] : entry) {
    bytes += key.size();
    if (value.is_string()) {
      bytes += value.GetString().size();
    }
  }
  return bytes;
}

void StoreBoundedEntry(std::vector<base::DictValue> *entries,
                       size_t *stored_bytes, size_t *dropped,
                       size_t max_entries, size_t max_bytes,
                       size_t max_entry_bytes, base::DictValue entry) {
  size_t entry_bytes = FlatEntryStringBytes(entry);
  if (entry_bytes > max_entry_bytes) {
    ++*dropped;
    return;
  }
  while (!entries->empty() && (entries->size() >= max_entries ||
                               *stored_bytes + entry_bytes > max_bytes)) {
    *stored_bytes -=
        std::min(*stored_bytes, FlatEntryStringBytes(entries->front()));
    entries->erase(entries->begin());
    ++*dropped;
  }
  if (entry_bytes > max_bytes) {
    ++*dropped;
    return;
  }
  *stored_bytes += entry_bytes;
  entries->push_back(std::move(entry));
}

void ClearTrackingBuffers(DaoBrowserAutomationSession::DevToolsState *state) {
  state->network_requests.clear();
  state->network_request_bytes = 0;
  state->network_requests_dropped = 0;
  state->network_fields_truncated = 0;
  state->network_pending_enable_attempts.clear();
  state->staged_network_requests.clear();
  state->staged_network_request_bytes = 0;
  state->staged_network_requests_dropped = 0;
  state->staged_network_fields_truncated = 0;
  state->console_messages.clear();
  state->console_message_bytes = 0;
  state->console_messages_dropped = 0;
  state->console_fields_truncated = 0;
  state->console_pending_enable_attempts.clear();
  state->staged_console_messages.clear();
  state->staged_console_message_bytes = 0;
  state->staged_console_messages_dropped = 0;
  state->staged_console_fields_truncated = 0;
}

void ClearDomainStaging(DaoBrowserAutomationSession::DevToolsState *state,
                        bool network_domain) {
  if (network_domain) {
    state->staged_network_requests.clear();
    state->staged_network_request_bytes = 0;
    state->staged_network_requests_dropped = 0;
    state->staged_network_fields_truncated = 0;
    return;
  }
  state->staged_console_messages.clear();
  state->staged_console_message_bytes = 0;
  state->staged_console_messages_dropped = 0;
  state->staged_console_fields_truncated = 0;
}

void CommitDomainStaging(DaoBrowserAutomationSession::DevToolsState *state,
                         bool network_domain) {
  if (network_domain) {
    state->network_requests_dropped += state->staged_network_requests_dropped;
    state->network_fields_truncated += state->staged_network_fields_truncated;
    for (base::DictValue &entry : state->staged_network_requests) {
      StoreBoundedEntry(&state->network_requests, &state->network_request_bytes,
                        &state->network_requests_dropped, kMaxNetworkRequests,
                        kMaxNetworkTrackingBytes, kMaxNetworkEntryBytes,
                        std::move(entry));
    }
  } else {
    state->console_messages_dropped += state->staged_console_messages_dropped;
    state->console_fields_truncated += state->staged_console_fields_truncated;
    for (base::DictValue &entry : state->staged_console_messages) {
      StoreBoundedEntry(&state->console_messages, &state->console_message_bytes,
                        &state->console_messages_dropped, kMaxConsoleMessages,
                        kMaxConsoleTrackingBytes, kMaxConsoleEntryBytes,
                        std::move(entry));
    }
  }
  ClearDomainStaging(state, network_domain);
}

std::string MakeSearchExcerpt(std::string_view line) {
  std::u16string utf16 = base::UTF8ToUTF16(line);
  if (utf16.size() <= kMaxExcerptCodeUnits) {
    return std::string(line);
  }
  size_t excerpt_length = kMaxExcerptCodeUnits;
  if (excerpt_length > 0 && excerpt_length < utf16.size() &&
      utf16[excerpt_length - 1] >= 0xD800 &&
      utf16[excerpt_length - 1] <= 0xDBFF && utf16[excerpt_length] >= 0xDC00 &&
      utf16[excerpt_length] <= 0xDFFF) {
    --excerpt_length;
  }
  utf16.resize(excerpt_length);
  return base::UTF16ToUTF8(utf16) + "…";
}

ResourceTraversal
TraverseResourceTree(const base::DictValue &frame_tree,
                     const std::set<std::string> *allowed_types = nullptr,
                     std::string_view type_filter = std::string_view()) {
  struct PendingFrame {
    std::reference_wrapper<const base::DictValue> tree;
    size_t depth;
  };
  ResourceTraversal traversal;
  std::vector<PendingFrame> pending = {{std::cref(frame_tree), 0}};
  std::set<std::pair<std::string, std::string>> seen;
  auto add_candidate = [&](const std::string *url, const std::string *frame_id,
                           std::string_view type, const std::string *mime,
                           std::optional<double> content_size) {
    ++traversal.processed;
    if (!url || !frame_id || url->empty() || frame_id->empty() ||
        (!type_filter.empty() && type_filter != "all" && type != type_filter) ||
        (allowed_types && !allowed_types->contains(std::string(type)))) {
      ++traversal.skipped;
      return true;
    }
    if (!seen.insert({*frame_id, *url}).second) {
      ++traversal.skipped;
      return true;
    }
    if (traversal.resources.size() >= kMaxSearchResources ||
        traversal.url_bytes + url->size() > kMaxResourceUrlBytes) {
      ++traversal.skipped;
      traversal.resource_limit_hit = true;
      return false;
    }
    traversal.url_bytes += url->size();
    ++traversal.eligible;
    traversal.resources.push_back({.url = *url,
                                   .frame_id = *frame_id,
                                   .type = std::string(type),
                                   .mime_type = mime ? *mime : std::string(),
                                   .content_size = content_size});
    return true;
  };

  while (!pending.empty() && !traversal.resource_limit_hit) {
    PendingFrame current = pending.back();
    pending.pop_back();
    if (current.depth > kMaxResourceFrameDepth) {
      ++traversal.skipped;
      traversal.resource_limit_hit = true;
      break;
    }
    const base::DictValue &current_tree = current.tree.get();
    const base::DictValue *frame = current_tree.FindDict("frame");
    const std::string *frame_id = frame ? frame->FindString("id") : nullptr;
    const std::string *frame_url = frame ? frame->FindString("url") : nullptr;
    const std::string *frame_mime =
        frame ? frame->FindString("mimeType") : nullptr;
    if (!add_candidate(frame_url, frame_id, "Document", frame_mime,
                       std::nullopt)) {
      break;
    }
    if (const base::ListValue *resources = current_tree.FindList("resources")) {
      for (const base::Value &value : *resources) {
        const base::DictValue *resource = value.GetIfDict();
        const std::string *url =
            resource ? resource->FindString("url") : nullptr;
        const std::string *type =
            resource ? resource->FindString("type") : nullptr;
        const std::string *mime =
            resource ? resource->FindString("mimeType") : nullptr;
        std::optional<double> content_size =
            resource ? resource->FindDouble("contentSize") : std::nullopt;
        if (!content_size && resource) {
          if (std::optional<int> integer_size =
                  resource->FindInt("contentSize")) {
            content_size = *integer_size;
          }
        }
        if (!type) {
          ++traversal.processed;
          ++traversal.skipped;
          continue;
        }
        if (!add_candidate(url, frame_id, *type, mime, content_size)) {
          break;
        }
      }
    }
    if (traversal.resource_limit_hit) {
      break;
    }
    if (const base::ListValue *children =
            current_tree.FindList("childFrames")) {
      for (auto it = children->rbegin(); it != children->rend(); ++it) {
        if (const base::DictValue *child = it->GetIfDict()) {
          pending.push_back({std::cref(*child), current.depth + 1});
        } else {
          ++traversal.skipped;
        }
      }
    }
  }
  return traversal;
}

bool ApplyDomainEnableResult(DaoBrowserAutomationSession::DevToolsState *state,
                             uint64_t generation,
                             content::DevToolsAgentHost *host,
                             uint64_t attempt_epoch, bool network_domain,
                             bool success) {
  if (state->binding_generation != generation ||
      state->bound_host.get() != host) {
    return false;
  }
  std::set<uint64_t> &pending_attempts =
      network_domain ? state->network_pending_enable_attempts
                     : state->console_pending_enable_attempts;
  if (pending_attempts.erase(attempt_epoch) == 0) {
    return false;
  }
  if (success) {
    if (network_domain) {
      state->network_tracking_enabled = true;
    } else {
      state->console_tracking_enabled = true;
    }
    CommitDomainStaging(state, network_domain);
  } else {
    const bool enabled = network_domain ? state->network_tracking_enabled
                                        : state->console_tracking_enabled;
    if (!enabled && pending_attempts.empty()) {
      ClearDomainStaging(state, network_domain);
    }
  }
  return true;
}

std::string FindFrameIdForUrl(const base::DictValue &frame_tree,
                              std::string_view url,
                              std::string_view required_frame_id,
                              std::optional<double> *content_size = nullptr) {
  ResourceTraversal traversal = TraverseResourceTree(frame_tree);
  for (const ResourceTarget &resource : traversal.resources) {
    if (resource.url == url &&
        (required_frame_id.empty() || resource.frame_id == required_frame_id)) {
      if (content_size) {
        *content_size = resource.content_size;
      }
      return resource.frame_id;
    }
  }
  if (!required_frame_id.empty()) {
    return std::string();
  }
  const base::DictValue *frame = frame_tree.FindDict("frame");
  const std::string *frame_id = frame ? frame->FindString("id") : nullptr;
  return frame_id ? *frame_id : std::string();
}

std::optional<size_t> ParseMaxMatches(const base::Value *value) {
  if (!value) {
    return 20;
  }
  double number = 0;
  if (value->is_int()) {
    number = value->GetInt();
  } else if (value->is_double()) {
    number = value->GetDouble();
  } else {
    return std::nullopt;
  }
  if (!std::isfinite(number) || number <= 0 ||
      number > static_cast<double>(kMaxSearchMatches)) {
    return std::nullopt;
  }
  return static_cast<size_t>(std::ceil(number));
}

bool ConfigureRegex(std::string_view pattern, std::string_view flags,
                    std::unique_ptr<re2::RE2> *regex, std::string *error) {
  std::set<char> seen_flags;
  for (char flag : flags) {
    if (flag != 'i' || !seen_flags.insert(flag).second) {
      *error = "unsupported or duplicate flag";
      return false;
    }
  }

  re2::RE2::Options options;
  options.set_log_errors(false);
  options.set_case_sensitive(!seen_flags.contains('i'));
  *regex = std::make_unique<re2::RE2>(pattern, options);
  if (!(*regex)->ok()) {
    *error = (*regex)->error();
    return false;
  }
  return true;
}

} // namespace

struct DaoDevToolsTools::Operation {
  struct DomainEnableAttempt {
    uint64_t generation = 0;
    scoped_refptr<content::DevToolsAgentHost> host;
    uint64_t epoch = 0;
    bool network_domain = false;
  };

  Operation(std::string request_id, std::string name,
            DaoBrowserAutomationSession *session, content::WebContents *target,
            url::Origin origin, int64_t document_sequence_number,
            uint64_t binding_generation, base::DictValue arguments,
            ResultCallback callback)
      : request_id(std::move(request_id)), name(std::move(name)),
        session(session ? session->GetWeakPtr() : nullptr),
        target(target ? target->GetWeakPtr() : nullptr),
        origin(std::move(origin)),
        document_sequence_number(document_sequence_number),
        binding_generation(binding_generation), arguments(std::move(arguments)),
        callback(std::move(callback)) {}

  ~Operation() = default;

  std::string request_id;
  std::string name;
  base::WeakPtr<DaoBrowserAutomationSession> session;
  base::WeakPtr<content::WebContents> target;
  url::Origin origin;
  int64_t document_sequence_number = -1;
  uint64_t binding_generation = 0;
  base::DictValue arguments;
  ResultCallback callback;
  std::set<int> command_ids;
  std::optional<DomainEnableAttempt> domain_enable_attempt;

  std::string search_pattern;
  std::string search_flags;
  std::unique_ptr<re2::RE2> search_regex;
  std::set<std::string> search_types;
  size_t max_matches = 20;
  std::vector<ResourceTarget> search_targets;
  size_t next_search_target = 0;
  base::ListValue search_matches;
  size_t search_processed = 0;
  size_t search_eligible = 0;
  size_t search_skipped = 0;
  size_t search_scanned_bytes = 0;
  size_t search_sources_searched = 0;
  size_t search_fetch_failed = 0;
  size_t search_content_limit_failures = 0;
  size_t search_sources_truncated = 0;
  bool search_resource_limit_hit = false;
  bool search_scan_limit_hit = false;
  std::optional<double> resource_content_size;
};

DaoDevToolsTools::DaoDevToolsTools(DaoDevToolsClient *devtools_client)
    : devtools_client_(devtools_client) {
  CHECK(devtools_client_);
  devtools_client_->SetEventCallback(base::BindRepeating(
      &DaoDevToolsTools::OnCDPEvent, weak_factory_.GetWeakPtr()));
}

DaoDevToolsTools::~DaoDevToolsTools() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_shutting_down_ = true;
  is_cancelling_ = false;
  CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                             "DevTools tool dispatcher was destroyed."));
  ClearSessionState(nullptr);
  devtools_client_->SetEventCallback(DaoDevToolsClient::EventCallback());
  weak_factory_.InvalidateWeakPtrs();
}

bool DaoDevToolsTools::Handles(std::string_view name) {
  constexpr std::array<std::string_view, 10> kNames = {
      "enable_network_tracking", "get_network_requests",
      "clear_network_requests",  "get_network_body",
      "enable_console_tracking", "get_console_messages",
      "clear_console_messages",  "list_page_resources",
      "get_resource_content",    "search_in_resources",
  };
  return std::ranges::find(kNames, name) != kNames.end();
}

base::DictValue DaoDevToolsTools::TraverseResourceTreeForTesting(
    const base::DictValue &frame_tree, std::string_view type_filter) {
  ResourceTraversal traversal =
      TraverseResourceTree(frame_tree, nullptr, type_filter);
  base::ListValue resources;
  for (ResourceTarget &resource : traversal.resources) {
    resources.Append(base::DictValue()
                         .Set("url", std::move(resource.url))
                         .Set("frame_id", std::move(resource.frame_id))
                         .Set("type", std::move(resource.type)));
  }
  return base::DictValue()
      .Set("resources", std::move(resources))
      .Set("processed", static_cast<double>(traversal.processed))
      .Set("eligible", static_cast<double>(traversal.eligible))
      .Set("skipped", static_cast<double>(traversal.skipped))
      .Set("url_bytes", static_cast<double>(traversal.url_bytes))
      .Set("resource_limit_hit", traversal.resource_limit_hit);
}

bool DaoDevToolsTools::ApplyDomainEnableResultForTesting(
    DaoBrowserAutomationSession *session, uint64_t generation,
    content::DevToolsAgentHost *host, uint64_t attempt_epoch,
    bool network_domain, bool success) {
  if (!session) {
    return false;
  }
  DaoBrowserAutomationSession::DevToolsState &state = session->devtools_state();
  if (state.binding_generation == generation &&
      state.bound_host.get() == host) {
    (network_domain ? state.network_pending_enable_attempts
                    : state.console_pending_enable_attempts)
        .insert(attempt_epoch);
  }
  return ApplyDomainEnableResult(&state, generation, host, attempt_epoch,
                                 network_domain, success);
}

void DaoDevToolsTools::Execute(std::string request_id,
                               DaoBrowserAutomationSession *session,
                               std::string name, base::DictValue arguments,
                               ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "DevTools tool dispatcher is cancelling pending operations.")));
    return;
  }
  if (!session || !Handles(name) || request_id.empty() ||
      operations_.contains(request_id)) {
    std::move(callback).Run(
        ErrorResult(InternalError("DevTools tool dispatch is unavailable.")));
    return;
  }

  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  auto target = SyncSessionBinding(session, false);
  if (!weak_this || !session_weak) {
    std::move(callback).Run(ErrorResult(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "DevTools tool ownership changed while binding the target.")));
    return;
  }
  if (!target.has_value()) {
    std::move(callback).Run(ErrorResult(std::move(target).error()));
    return;
  }

  url::Origin origin;
  int64_t document_sequence_number = -1;
  if (!GetCurrentDocument(*target, &origin, &document_sequence_number)) {
    std::move(callback).Run(ErrorResult(
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "The authorized page document is unavailable.")));
    return;
  }
  session_weak->SetTargetChangedCallback(base::BindRepeating(
      &DaoDevToolsTools::OnSessionTargetChanged, weak_factory_.GetWeakPtr()));
  const uint64_t binding_generation =
      session_weak->devtools_state().binding_generation;
  operations_.emplace(
      request_id,
      std::make_unique<Operation>(request_id, name, session_weak.get(), *target,
                                  std::move(origin), document_sequence_number,
                                  binding_generation, std::move(arguments),
                                  std::move(callback)));

  if (name == "enable_network_tracking") {
    ExecuteEnableNetwork(request_id);
  } else if (name == "get_network_requests") {
    ExecuteGetNetwork(request_id);
  } else if (name == "clear_network_requests") {
    ExecuteClearNetwork(request_id);
  } else if (name == "get_network_body") {
    ExecuteGetNetworkBody(request_id);
  } else if (name == "enable_console_tracking") {
    ExecuteEnableConsole(request_id);
  } else if (name == "get_console_messages") {
    ExecuteGetConsole(request_id);
  } else if (name == "clear_console_messages") {
    ExecuteClearConsole(request_id);
  } else if (name == "list_page_resources") {
    ExecuteListResources(request_id);
  } else if (name == "get_resource_content") {
    ExecuteGetResourceContent(request_id);
  } else {
    ExecuteSearch(request_id);
  }
}

bool DaoDevToolsTools::Cancel(std::string_view request_id, DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_) {
    return false;
  }
  return CancelInternal(request_id, std::move(error));
}

bool DaoDevToolsTools::CancelInternal(std::string_view request_id,
                                      DaoToolError error) {
  auto it = operations_.find(request_id);
  if (it == operations_.end()) {
    return false;
  }
  std::unique_ptr<Operation> operation = std::move(it->second);
  operations_.erase(it);
  FailPendingDomainEnableAttempt(operation.get());
  CleanupOperation(operation.get());
  std::move(operation->callback).Run(ErrorResult(std::move(error)));
  return true;
}

void DaoDevToolsTools::CancelAll(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_cancelling_) {
    return;
  }
  is_cancelling_ = true;
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  CancelAllOperations(std::move(error));
  if (!weak_this) {
    return;
  }
  weak_this->is_cancelling_ = false;
  base::WeakPtr<DaoBrowserAutomationSession> pending_session =
      weak_this->pending_target_change_session_;
  weak_this->pending_target_change_session_.reset();
  if (!weak_this->is_shutting_down_ && pending_session) {
    weak_this->OnSessionTargetChanged(pending_session.get());
  }
}

void DaoDevToolsTools::CancelAllOperations(DaoToolError error) {
  std::vector<std::string> request_ids;
  request_ids.reserve(operations_.size());
  for (const auto &[request_id, _] : operations_) {
    request_ids.push_back(request_id);
  }
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  for (const std::string &request_id : request_ids) {
    CancelInternal(request_id, error);
    if (!weak_this) {
      return;
    }
  }
}

void DaoDevToolsTools::CancelSessionOperations(
    DaoBrowserAutomationSession *session, DaoToolError error) {
  std::vector<std::string> request_ids;
  for (const auto &[request_id, operation] : operations_) {
    if (operation->session.get() == session) {
      request_ids.push_back(request_id);
    }
  }
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  for (const std::string &request_id : request_ids) {
    CancelInternal(request_id, error);
    if (!weak_this) {
      return;
    }
  }
}

void DaoDevToolsTools::ClearSessionState(DaoBrowserAutomationSession *session) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const bool owns_transition_gate = !is_cancelling_;
  if (owns_transition_gate) {
    is_cancelling_ = true;
  }
  ++clear_generation_;
  if (!session || pending_target_change_session_.get() == session) {
    pending_target_change_session_.reset();
  }
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  auto finish_clear = [&weak_this, owns_transition_gate]() {
    if (owns_transition_gate && weak_this) {
      weak_this->is_cancelling_ = false;
    }
  };
  if (!session) {
    CancelAllOperations(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools session state was cleared."));
    if (!weak_this) {
      return;
    }
    if (weak_this->bound_session_) {
      base::WeakPtr<DaoBrowserAutomationSession> bound_session =
          weak_this->bound_session_;
      weak_this->ResetBinding(bound_session.get(), true, true);
      if (!weak_this) {
        return;
      }
      if (bound_session) {
        bound_session->SetTargetChangedCallback(
            DaoBrowserAutomationSession::TargetChangedCallback());
      }
    } else {
      weak_this->devtools_client_->Detach();
      if (!weak_this) {
        return;
      }
    }
    weak_this->pending_target_change_session_.reset();
    weak_this->bound_session_.reset();
    finish_clear();
    return;
  }
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  CancelSessionOperations(
      session, MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                "DevTools session state was cleared."));
  if (!weak_this) {
    return;
  }
  if (!session_weak) {
    finish_clear();
    return;
  }
  weak_this->ResetBinding(session_weak.get(), true, true);
  if (!weak_this) {
    return;
  }
  if (!session_weak) {
    finish_clear();
    return;
  }
  session_weak->SetTargetChangedCallback(
      DaoBrowserAutomationSession::TargetChangedCallback());
  if (weak_this->pending_target_change_session_.get() == session_weak.get()) {
    weak_this->pending_target_change_session_.reset();
  }
  if (weak_this->bound_session_.get() == session_weak.get()) {
    weak_this->bound_session_.reset();
  }
  finish_clear();
}

DaoDevToolsTools::Operation *
DaoDevToolsTools::FindOperation(std::string_view request_id) {
  auto it = operations_.find(request_id);
  return it == operations_.end() ? nullptr : it->second.get();
}

int DaoDevToolsTools::SendCommand(
    std::string_view request_id, std::string method, base::DictValue params,
    base::OnceCallback<void(base::expected<base::Value, DaoToolError>)>
        callback,
    std::optional<size_t> max_response_bytes) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_ ||
      !ValidateOperationTarget(request_id)) {
    return 0;
  }
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return 0;
  }
  auto guarded_callback = base::BindOnce(
      [](base::WeakPtr<DaoDevToolsTools> self, std::string request_id,
         base::OnceCallback<void(base::expected<base::Value, DaoToolError>)>
             callback,
         base::expected<base::Value, DaoToolError> result) {
        if (!self || !self->ValidateOperationTarget(request_id)) {
          return;
        }
        std::move(callback).Run(std::move(result));
      },
      weak_factory_.GetWeakPtr(), std::string(request_id), std::move(callback));
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  const int command_id = devtools_client_->SendCommand(
      method, std::move(params), std::move(guarded_callback),
      max_response_bytes);
  if (!weak_this) {
    return 0;
  }
  operation = weak_this->FindOperation(request_id);
  if (operation && command_id > 0) {
    operation->command_ids.insert(command_id);
  } else if (command_id > 0) {
    weak_this->devtools_client_->CancelCommand(
        command_id,
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools tool command was cancelled during "
                         "dispatch."));
  }
  return command_id;
}

void DaoDevToolsTools::Finish(std::string_view request_id,
                              DaoBrowserToolResult result) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = operations_.find(request_id);
  if (it == operations_.end()) {
    return;
  }
  std::unique_ptr<Operation> operation = std::move(it->second);
  operations_.erase(it);
  CleanupOperation(operation.get());
  result.ok = !result.error.has_value();
  std::move(operation->callback).Run(std::move(result));
}

void DaoDevToolsTools::FinishSuccess(std::string_view request_id,
                                     base::Value data) {
  DaoBrowserToolResult result;
  result.ok = true;
  result.data = std::move(data);
  Finish(request_id, std::move(result));
}

void DaoDevToolsTools::FinishError(std::string_view request_id,
                                   DaoToolError error) {
  Finish(request_id, ErrorResult(std::move(error)));
}

void DaoDevToolsTools::FailPendingDomainEnableAttempt(Operation *operation) {
  if (!operation || !operation->domain_enable_attempt || !operation->session) {
    return;
  }
  Operation::DomainEnableAttempt attempt =
      std::move(*operation->domain_enable_attempt);
  operation->domain_enable_attempt.reset();
  ApplyDomainEnableResult(&operation->session->devtools_state(),
                          attempt.generation, attempt.host.get(), attempt.epoch,
                          attempt.network_domain, false);
}

void DaoDevToolsTools::CleanupOperation(Operation *operation) {
  if (!operation) {
    return;
  }
  std::set<int> command_ids = std::move(operation->command_ids);
  for (int command_id : command_ids) {
    devtools_client_->CancelCommand(
        command_id,
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools tool command is no longer active."));
  }
}

bool DaoDevToolsTools::ValidateOperationTarget(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return false;
  }
  if (!operation->session || !operation->target) {
    FinishError(request_id,
                MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                                 "DevTools target is no longer available."));
    return false;
  }
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  auto target = operation->session->ResolveEligibleTarget();
  if (!weak_this) {
    return false;
  }
  operation = weak_this->FindOperation(request_id);
  if (!operation) {
    return false;
  }
  if (!target.has_value()) {
    FinishError(request_id, std::move(target).error());
    return false;
  }
  if (*target != operation->target.get()) {
    FinishError(
        request_id,
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "The authorized DevTools target changed during the "
                         "operation."));
    return false;
  }
  url::Origin origin;
  int64_t document_sequence_number = -1;
  if (!GetCurrentDocument(*target, &origin, &document_sequence_number)) {
    FinishError(
        request_id,
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "The authorized page document is unavailable."));
    return false;
  }
  if (origin != operation->origin ||
      document_sequence_number != operation->document_sequence_number) {
    FinishError(
        request_id,
        MakeDaoToolError(DaoToolErrorCode::kTargetForbidden,
                         "The authorized page document changed during the "
                         "DevTools operation."));
    return false;
  }
  const DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  if (bound_session_.get() != operation->session.get() ||
      state.bound_target.get() != operation->target.get() ||
      state.bound_host.get() != devtools_client_->agent_host() ||
      state.binding_generation != operation->binding_generation) {
    FinishError(
        request_id,
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "The DevTools target binding changed during the "
                         "operation."));
    return false;
  }
  return true;
}

base::expected<content::WebContents *, DaoToolError>
DaoDevToolsTools::SyncSessionBinding(DaoBrowserAutomationSession *session,
                                     bool clear_buffers, int rebind_attempt) {
  if (!session) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "Browser automation session is no longer available."));
  }
  const uint64_t initial_clear_generation = clear_generation_;
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  auto target = session->ResolveEligibleTarget();
  if (!weak_this || !session_weak) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "DevTools tool dispatcher was destroyed while binding."));
  }
  if (!target.has_value()) {
    return base::unexpected(std::move(target).error());
  }
  if (clear_generation_ != initial_clear_generation) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools session state was cleared while binding."));
  }
  const bool owns_transition_gate = !is_cancelling_;
  if (owns_transition_gate) {
    is_cancelling_ = true;
  }
  auto finish_binding =
      [&weak_this, owns_transition_gate](
          base::expected<content::WebContents *, DaoToolError> result) {
        if (!owns_transition_gate || !weak_this) {
          return result;
        }
        weak_this->is_cancelling_ = false;
        base::WeakPtr<DaoBrowserAutomationSession> pending_session =
            weak_this->pending_target_change_session_;
        weak_this->pending_target_change_session_.reset();
        if (!weak_this->is_shutting_down_ && pending_session) {
          weak_this->OnSessionTargetChanged(pending_session.get());
          if (!weak_this) {
            return base::expected<content::WebContents *, DaoToolError>(
                base::unexpected(MakeDaoToolError(
                    DaoToolErrorCode::kToolCancelled,
                    "DevTools tool dispatcher was destroyed while processing "
                    "a target change.")));
          }
          return base::expected<content::WebContents *, DaoToolError>(
              base::unexpected(MakeDaoToolError(
                  DaoToolErrorCode::kToolCancelled,
                  "The authorized DevTools target changed while binding.")));
        }
        return result;
      };
  session = session_weak.get();
  base::WeakPtr<content::WebContents> target_weak = (*target)->GetWeakPtr();

  if (bound_session_ && bound_session_.get() != session) {
    base::WeakPtr<DaoBrowserAutomationSession> previous = bound_session_;
    if (previous) {
      previous->SetTargetChangedCallback(
          DaoBrowserAutomationSession::TargetChangedCallback());
    }
    CancelSessionOperations(
        previous.get(),
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "The shared DevTools target changed."));
    if (!weak_this || !session_weak) {
      return finish_binding(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "DevTools tool dispatcher was destroyed while binding.")));
    }
    if (weak_this->clear_generation_ != initial_clear_generation) {
      return finish_binding(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "DevTools session state was cleared while binding.")));
    }
    if (previous) {
      weak_this->ResetBinding(previous.get(), false, false);
    }
    if (!weak_this || !session_weak) {
      return finish_binding(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "DevTools tool dispatcher was destroyed while binding.")));
    }
    if (weak_this->clear_generation_ != initial_clear_generation) {
      return finish_binding(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "DevTools session state was cleared while binding.")));
    }
    if (weak_this->bound_session_.get() == previous.get()) {
      weak_this->bound_session_.reset();
    }
    session = session_weak.get();
  }

  auto refreshed_target = session_weak->ResolveEligibleTarget();
  if (!weak_this || !session_weak) {
    return finish_binding(base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "DevTools tool dispatcher was destroyed while binding.")));
  }
  if (weak_this->clear_generation_ != initial_clear_generation) {
    return finish_binding(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools session state was cleared while binding.")));
  }
  if (!refreshed_target.has_value()) {
    return finish_binding(
        base::unexpected(std::move(refreshed_target).error()));
  }
  const bool target_changed_before_attach =
      !target_weak || *refreshed_target != target_weak.get() ||
      weak_this->pending_target_change_session_.get() == session_weak.get();
  if (target_changed_before_attach) {
    if (rebind_attempt >= 3) {
      return finish_binding(base::unexpected(
          MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                           "The DevTools target changed repeatedly while "
                           "binding.")));
    }
    if (weak_this->pending_target_change_session_.get() == session_weak.get()) {
      weak_this->pending_target_change_session_.reset();
    }
    auto rebound = weak_this->SyncSessionBinding(session_weak.get(), true,
                                                 rebind_attempt + 1);
    return finish_binding(std::move(rebound));
  }
  target = std::move(refreshed_target);
  target_weak = (*target)->GetWeakPtr();
  url::Origin origin;
  int64_t document_sequence_number = -1;
  if (!GetCurrentDocument(target_weak.get(), &origin,
                          &document_sequence_number)) {
    return finish_binding(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "The authorized page document is unavailable.")));
  }

  DaoBrowserAutomationSession::DevToolsState &state = session->devtools_state();
  const bool target_changed =
      state.bound_target &&
      (state.bound_target.get() != target_weak.get() ||
       state.bound_origin != origin ||
       state.bound_document_sequence_number != document_sequence_number);
  const bool binding_matches =
      bound_session_.get() == session &&
      state.bound_target.get() == target_weak.get() && state.bound_host &&
      devtools_client_->agent_host() &&
      state.bound_host.get() == devtools_client_->agent_host() &&
      state.bound_origin == origin &&
      state.bound_document_sequence_number == document_sequence_number;
  if (binding_matches) {
    return finish_binding(target_weak.get());
  }

  ++state.binding_generation;
  state.network_tracking_enabled = false;
  state.console_tracking_enabled = false;
  state.network_pending_enable_attempts.clear();
  state.console_pending_enable_attempts.clear();
  ClearDomainStaging(&state, true);
  ClearDomainStaging(&state, false);
  state.bound_host = nullptr;
  state.bound_target = target_weak;
  state.bound_origin = origin;
  state.bound_document_sequence_number = document_sequence_number;
  if (clear_buffers || target_changed) {
    ClearTrackingBuffers(&state);
  }
  const uint64_t binding_generation = state.binding_generation;

  bound_session_ = session_weak;
  const bool attached = devtools_client_->AttachTo(target_weak.get());
  if (!weak_this || !session_weak) {
    return finish_binding(base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "DevTools tool dispatcher was destroyed while binding.")));
  }
  if (weak_this->clear_generation_ != initial_clear_generation) {
    return finish_binding(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools session state was cleared while binding.")));
  }
  auto attached_target = session_weak->ResolveEligibleTarget();
  if (!weak_this || !session_weak) {
    return finish_binding(base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "DevTools tool dispatcher was destroyed while binding.")));
  }
  if (weak_this->clear_generation_ != initial_clear_generation) {
    return finish_binding(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "DevTools session state was cleared while binding.")));
  }
  DaoBrowserAutomationSession::DevToolsState &rebound_state =
      session_weak->devtools_state();
  if (rebound_state.binding_generation != binding_generation ||
      rebound_state.bound_target.get() != target_weak.get()) {
    return finish_binding(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                         "The DevTools target changed while binding.")));
  }
  if (!attached) {
    rebound_state.bound_host = nullptr;
    if (weak_this->bound_session_.get() == session_weak.get()) {
      weak_this->bound_session_.reset();
    }
    return finish_binding(base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kDevToolsAttachFailed,
                         "Could not attach to the DevTools target.")));
  }
  url::Origin attached_origin;
  int64_t attached_document_sequence_number = -1;
  const bool attached_target_changed =
      !attached_target.has_value() || !target_weak ||
      *attached_target != target_weak.get() ||
      !GetCurrentDocument(target_weak.get(), &attached_origin,
                          &attached_document_sequence_number) ||
      attached_origin != origin ||
      attached_document_sequence_number != document_sequence_number ||
      weak_this->pending_target_change_session_.get() == session_weak.get();
  if (attached_target_changed) {
    weak_this->ResetBinding(session_weak.get(), false, true);
    if (!weak_this || !session_weak) {
      return finish_binding(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "DevTools tool dispatcher was destroyed while rebinding.")));
    }
    if (weak_this->clear_generation_ != initial_clear_generation) {
      return finish_binding(base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "DevTools session state was cleared while binding.")));
    }
    if (rebind_attempt >= 3) {
      return finish_binding(base::unexpected(
          MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                           "The DevTools target changed repeatedly while "
                           "binding.")));
    }
    if (weak_this->pending_target_change_session_.get() == session_weak.get()) {
      weak_this->pending_target_change_session_.reset();
    }
    auto rebound = weak_this->SyncSessionBinding(session_weak.get(), true,
                                                 rebind_attempt + 1);
    return finish_binding(std::move(rebound));
  }
  rebound_state.bound_host = weak_this->devtools_client_->agent_host();
  weak_this->ReenableDomains(session_weak.get());
  return finish_binding(target_weak.get());
}

void DaoDevToolsTools::OnSessionTargetChanged(
    DaoBrowserAutomationSession *session) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!session || is_shutting_down_) {
    return;
  }
  if (is_cancelling_) {
    pending_target_change_session_ = session->GetWeakPtr();
    return;
  }
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  const uint64_t initial_clear_generation = clear_generation_;
  is_cancelling_ = true;
  auto finish_target_change = [&weak_this]() {
    if (!weak_this) {
      return;
    }
    weak_this->is_cancelling_ = false;
    base::WeakPtr<DaoBrowserAutomationSession> pending_session =
        weak_this->pending_target_change_session_;
    weak_this->pending_target_change_session_.reset();
    if (!weak_this->is_shutting_down_ && pending_session) {
      weak_this->OnSessionTargetChanged(pending_session.get());
    }
  };
  CancelSessionOperations(
      session, MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                "The authorized DevTools target changed."));
  if (!weak_this) {
    return;
  }
  if (!session_weak) {
    finish_target_change();
    return;
  }
  if (weak_this->clear_generation_ != initial_clear_generation) {
    finish_target_change();
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state =
      session_weak->devtools_state();
  ++state.binding_generation;
  state.bound_host = nullptr;
  state.bound_target.reset();
  state.bound_origin = url::Origin();
  state.bound_document_sequence_number = -1;
  ClearTrackingBuffers(&state);
  if (weak_this->bound_session_.get() == session_weak.get()) {
    weak_this->devtools_client_->Detach();
    if (!weak_this) {
      return;
    }
    if (!session_weak) {
      finish_target_change();
      return;
    }
    if (weak_this->clear_generation_ != initial_clear_generation) {
      finish_target_change();
      return;
    }
    weak_this->bound_session_.reset();
  }

  auto rebound = weak_this->SyncSessionBinding(session_weak.get(), true);
  if (!weak_this) {
    return;
  }
  const bool rebound_succeeded = rebound.has_value();
  finish_target_change();
  if (!weak_this || !rebound_succeeded) {
    return;
  }
}

void DaoDevToolsTools::ReenableDomains(DaoBrowserAutomationSession *session) {
  DaoBrowserAutomationSession::DevToolsState &state = session->devtools_state();
  const uint64_t generation = state.binding_generation;
  scoped_refptr<content::DevToolsAgentHost> host = state.bound_host;
  if (state.network_tracking_requested) {
    const uint64_t attempt_epoch = ++state.network_enable_attempt_epoch;
    state.network_pending_enable_attempts.insert(attempt_epoch);
    devtools_client_->SendCommand(
        "Network.enable", NetworkEnableParams(),
        base::BindOnce(&DaoDevToolsTools::OnDomainReenabled,
                       weak_factory_.GetWeakPtr(), session->GetWeakPtr(),
                       generation, host, attempt_epoch, true));
  }
  if (state.console_tracking_requested) {
    const uint64_t attempt_epoch = ++state.console_enable_attempt_epoch;
    state.console_pending_enable_attempts.insert(attempt_epoch);
    devtools_client_->SendCommand(
        "Runtime.enable", base::DictValue(),
        base::BindOnce(&DaoDevToolsTools::OnDomainReenabled,
                       weak_factory_.GetWeakPtr(), session->GetWeakPtr(),
                       generation, host, attempt_epoch, false));
  }
}

void DaoDevToolsTools::OnDomainReenabled(
    base::WeakPtr<DaoBrowserAutomationSession> session, uint64_t generation,
    scoped_refptr<content::DevToolsAgentHost> host, uint64_t attempt_epoch,
    bool network_domain, base::expected<base::Value, DaoToolError> result) {
  if (!session) {
    return;
  }
  ApplyDomainEnableResult(&session->devtools_state(), generation, host.get(),
                          attempt_epoch, network_domain, result.has_value());
}

void DaoDevToolsTools::ResetBinding(DaoBrowserAutomationSession *session,
                                    bool clear_enabled_flags,
                                    bool clear_buffers) {
  if (!session) {
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state = session->devtools_state();
  ++state.binding_generation;
  state.network_pending_enable_attempts.clear();
  state.console_pending_enable_attempts.clear();
  ClearDomainStaging(&state, true);
  ClearDomainStaging(&state, false);
  state.bound_target.reset();
  state.bound_host = nullptr;
  state.bound_origin = url::Origin();
  state.bound_document_sequence_number = -1;
  if (clear_enabled_flags) {
    state.network_tracking_enabled = false;
    state.network_tracking_requested = false;
    ++state.network_enable_attempt_epoch;
    state.console_tracking_enabled = false;
    state.console_tracking_requested = false;
    ++state.console_enable_attempt_epoch;
  }
  if (clear_buffers) {
    ClearTrackingBuffers(&state);
  }
  if (bound_session_.get() == session) {
    devtools_client_->Detach();
  }
}

void DaoDevToolsTools::OnCDPEvent(content::DevToolsAgentHost *agent_host,
                                  const std::string &method,
                                  const base::DictValue &params) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!bound_session_) {
    return;
  }
  base::WeakPtr<DaoBrowserAutomationSession> session = bound_session_;
  DaoBrowserAutomationSession::DevToolsState &initial_state =
      session->devtools_state();
  if (!agent_host || initial_state.bound_host.get() != agent_host ||
      devtools_client_->agent_host() != agent_host) {
    return;
  }

  const uint64_t initial_generation = initial_state.binding_generation;
  base::WeakPtr<DaoDevToolsTools> weak_this = weak_factory_.GetWeakPtr();
  auto target = session->ResolveEligibleTarget();
  if (!weak_this || !session ||
      weak_this->bound_session_.get() != session.get()) {
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state = session->devtools_state();
  if (state.binding_generation != initial_generation ||
      state.bound_host.get() != agent_host ||
      weak_this->devtools_client_->agent_host() != agent_host) {
    return;
  }
  if (!target.has_value() || state.bound_target.get() != *target) {
    weak_this->OnSessionTargetChanged(session.get());
    return;
  }
  url::Origin origin;
  int64_t document_sequence_number = -1;
  if (!GetCurrentDocument(*target, &origin, &document_sequence_number) ||
      state.bound_origin != origin ||
      state.bound_document_sequence_number != document_sequence_number) {
    weak_this->OnSessionTargetChanged(session.get());
    return;
  }

  const bool capture_network = state.network_tracking_enabled ||
                               !state.network_pending_enable_attempts.empty();
  if (capture_network) {
    std::vector<base::DictValue> *network_requests =
        state.network_tracking_enabled ? &state.network_requests
                                       : &state.staged_network_requests;
    size_t *network_request_bytes = state.network_tracking_enabled
                                        ? &state.network_request_bytes
                                        : &state.staged_network_request_bytes;
    size_t *network_requests_dropped =
        state.network_tracking_enabled ? &state.network_requests_dropped
                                       : &state.staged_network_requests_dropped;
    size_t *network_fields_truncated =
        state.network_tracking_enabled ? &state.network_fields_truncated
                                       : &state.staged_network_fields_truncated;
    if (method == "Network.requestWillBeSent") {
      base::DictValue entry;
      if (const base::DictValue *request = params.FindDict("request")) {
        if (const std::string *url = request->FindString("url")) {
          *network_fields_truncated +=
              SetBoundedString(&entry, "url", *url, kMaxTrackedFieldBytes);
        }
        if (const std::string *request_method = request->FindString("method")) {
          *network_fields_truncated += SetBoundedString(
              &entry, "method", *request_method, kMaxTrackedFieldBytes);
        }
      }
      if (const std::string *request_id = params.FindString("requestId")) {
        *network_fields_truncated += SetBoundedString(
            &entry, "request_id", *request_id, kMaxTrackedFieldBytes);
      }
      if (const std::string *type = params.FindString("type")) {
        *network_fields_truncated +=
            SetBoundedString(&entry, "type", *type, kMaxTrackedFieldBytes);
      }
      entry.Set("timestamp", params.FindDouble("timestamp").value_or(0));
      entry.Set("phase", "request");
      StoreBoundedEntry(network_requests, network_request_bytes,
                        network_requests_dropped, kMaxNetworkRequests,
                        kMaxNetworkTrackingBytes, kMaxNetworkEntryBytes,
                        std::move(entry));
    } else if (method == "Network.responseReceived") {
      base::DictValue entry;
      if (const base::DictValue *response = params.FindDict("response")) {
        if (const std::string *url = response->FindString("url")) {
          *network_fields_truncated +=
              SetBoundedString(&entry, "url", *url, kMaxTrackedFieldBytes);
        }
        if (std::optional<int> status = response->FindInt("status")) {
          entry.Set("status", *status);
        }
        if (const std::string *mime = response->FindString("mimeType")) {
          *network_fields_truncated += SetBoundedString(
              &entry, "mimeType", *mime, kMaxTrackedFieldBytes);
        }
      }
      if (const std::string *request_id = params.FindString("requestId")) {
        *network_fields_truncated += SetBoundedString(
            &entry, "request_id", *request_id, kMaxTrackedFieldBytes);
      }
      if (const std::string *type = params.FindString("type")) {
        *network_fields_truncated +=
            SetBoundedString(&entry, "type", *type, kMaxTrackedFieldBytes);
      }
      entry.Set("phase", "response");
      StoreBoundedEntry(network_requests, network_request_bytes,
                        network_requests_dropped, kMaxNetworkRequests,
                        kMaxNetworkTrackingBytes, kMaxNetworkEntryBytes,
                        std::move(entry));
    }
  }

  const bool capture_console = state.console_tracking_enabled ||
                               !state.console_pending_enable_attempts.empty();
  if (!capture_console) {
    return;
  }
  std::vector<base::DictValue> *console_messages =
      state.console_tracking_enabled ? &state.console_messages
                                     : &state.staged_console_messages;
  size_t *console_message_bytes = state.console_tracking_enabled
                                      ? &state.console_message_bytes
                                      : &state.staged_console_message_bytes;
  size_t *console_messages_dropped =
      state.console_tracking_enabled ? &state.console_messages_dropped
                                     : &state.staged_console_messages_dropped;
  size_t *console_fields_truncated =
      state.console_tracking_enabled ? &state.console_fields_truncated
                                     : &state.staged_console_fields_truncated;
  if (method == "Runtime.consoleAPICalled") {
    base::DictValue entry;
    if (const std::string *type = params.FindString("type")) {
      *console_fields_truncated +=
          SetBoundedString(&entry, "type", *type, kMaxTrackedFieldBytes);
    }
    if (const base::ListValue *arguments = params.FindList("args");
        arguments && !arguments->empty()) {
      std::string text;
      for (const base::Value &argument : *arguments) {
        const base::DictValue *remote = argument.GetIfDict();
        const std::string *value =
            remote ? remote->FindString("value") : nullptr;
        const std::string *description =
            remote ? remote->FindString("description") : nullptr;
        const std::string *part = value ? value : description;
        if (!part) {
          continue;
        }
        if (!text.empty()) {
          if (text.size() >= kMaxTrackedFieldBytes) {
            ++*console_fields_truncated;
            break;
          }
          text.push_back(' ');
        }
        const size_t remaining = kMaxTrackedFieldBytes - text.size();
        if (part->size() > remaining) {
          text.append(base::TruncateUTF8ToByteSize(*part, remaining));
          ++*console_fields_truncated;
          break;
        }
        text.append(*part);
      }
      entry.Set("text", std::move(text));
    }
    entry.Set("timestamp", params.FindDouble("timestamp").value_or(0));
    StoreBoundedEntry(console_messages, console_message_bytes,
                      console_messages_dropped, kMaxConsoleMessages,
                      kMaxConsoleTrackingBytes, kMaxConsoleEntryBytes,
                      std::move(entry));
  } else if (method == "Runtime.exceptionThrown") {
    base::DictValue entry;
    entry.Set("type", "error");
    if (const base::DictValue *details = params.FindDict("exceptionDetails")) {
      if (const std::string *text = details->FindString("text")) {
        *console_fields_truncated +=
            SetBoundedString(&entry, "text", *text, kMaxTrackedFieldBytes);
      }
      if (std::optional<int> line = details->FindInt("lineNumber")) {
        entry.Set("line", *line);
      }
      if (const std::string *url = details->FindString("url")) {
        *console_fields_truncated +=
            SetBoundedString(&entry, "url", *url, kMaxTrackedFieldBytes);
      }
    }
    entry.Set("timestamp", params.FindDouble("timestamp").value_or(0));
    StoreBoundedEntry(console_messages, console_message_bytes,
                      console_messages_dropped, kMaxConsoleMessages,
                      kMaxConsoleTrackingBytes, kMaxConsoleEntryBytes,
                      std::move(entry));
  }
}

void DaoDevToolsTools::ExecuteEnableNetwork(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation || !operation->session) {
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  state.network_tracking_requested = true;
  const uint64_t generation = state.binding_generation;
  scoped_refptr<content::DevToolsAgentHost> host = state.bound_host;
  const uint64_t attempt_epoch = ++state.network_enable_attempt_epoch;
  state.network_pending_enable_attempts.insert(attempt_epoch);
  operation->domain_enable_attempt = Operation::DomainEnableAttempt{
      .generation = generation,
      .host = host,
      .epoch = attempt_epoch,
      .network_domain = true,
  };
  SendCommand(
      request_id, "Network.enable", NetworkEnableParams(),
      base::BindOnce(
          [](base::WeakPtr<DaoDevToolsTools> self, std::string request_id,
             uint64_t generation,
             scoped_refptr<content::DevToolsAgentHost> host,
             uint64_t attempt_epoch,
             base::expected<base::Value, DaoToolError> result) {
            if (!self) {
              return;
            }
            Operation *operation = self->FindOperation(request_id);
            if (!operation || !operation->session) {
              return;
            }
            DaoBrowserAutomationSession::DevToolsState &state =
                operation->session->devtools_state();
            if (!result.has_value()) {
              ApplyDomainEnableResult(&state, generation, host.get(),
                                      attempt_epoch, true, false);
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            ApplyDomainEnableResult(&state, generation, host.get(),
                                    attempt_epoch, true, true);
            self->FinishSuccess(
                request_id,
                base::Value(base::DictValue()
                                .Set("success", true)
                                .Set("message", "Network tracking enabled")));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id), generation,
          std::move(host), attempt_epoch));
}

void DaoDevToolsTools::ExecuteGetNetwork(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation || !operation->session) {
    return;
  }
  const DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  base::ListValue requests;
  for (const base::DictValue &entry : state.network_requests) {
    requests.Append(entry.Clone());
  }
  FinishSuccess(
      request_id,
      base::Value(
          base::DictValue()
              .Set("requests", std::move(requests))
              .Set("count", static_cast<int>(state.network_requests.size()))
              .Set("enabled", state.network_tracking_enabled)
              .Set("stored_bytes",
                   static_cast<double>(state.network_request_bytes))
              .Set("dropped_count",
                   static_cast<double>(state.network_requests_dropped))
              .Set("truncated_field_count",
                   static_cast<double>(state.network_fields_truncated))));
}

void DaoDevToolsTools::ExecuteClearNetwork(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation || !operation->session) {
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  state.network_requests.clear();
  state.network_request_bytes = 0;
  state.network_requests_dropped = 0;
  state.network_fields_truncated = 0;
  ClearDomainStaging(&state, true);
  FinishSuccess(request_id,
                base::Value(base::DictValue().Set("success", true)));
}

void DaoDevToolsTools::ExecuteGetNetworkBody(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  const std::string *network_request_id =
      operation ? operation->arguments.FindString("request_id") : nullptr;
  if (!network_request_id || network_request_id->empty()) {
    FinishError(request_id, InvalidArgument("request_id must not be empty."));
    return;
  }
  const std::string captured_request_id = *network_request_id;
  SendCommand(
      request_id, "Network.getResponseBody",
      base::DictValue().Set("requestId", captured_request_id),
      base::BindOnce(
          [](base::WeakPtr<DaoDevToolsTools> self, std::string request_id,
             std::string network_request_id,
             base::expected<base::Value, DaoToolError> result) {
            if (!self) {
              return;
            }
            if (!result.has_value()) {
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            base::DictValue data;
            data.Set("request_id", network_request_id);
            const base::DictValue *response = result->GetIfDict();
            const std::string *body =
                response ? response->FindString("body") : nullptr;
            if (!body) {
              data.Set("error",
                       response
                           ? "No body returned (request may not have completed)"
                           : "CDP call failed");
              self->FinishSuccess(request_id, base::Value(std::move(data)));
              return;
            }
            const bool base64_encoded =
                response->FindBool("base64Encoded").value_or(false);
            if (base64_encoded) {
              data.Set("body", *body);
              data.Set("base64_encoded", true);
            } else {
              std::string text = *body;
              const bool truncated = TruncateText(&text);
              data.Set("body", std::move(text));
              data.Set("base64_encoded", false);
              if (truncated) {
                data.Set("truncated", true);
              }
            }
            self->FinishSuccess(request_id, base::Value(std::move(data)));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id),
          captured_request_id));
}

void DaoDevToolsTools::ExecuteEnableConsole(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation || !operation->session) {
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  state.console_tracking_requested = true;
  const uint64_t generation = state.binding_generation;
  scoped_refptr<content::DevToolsAgentHost> host = state.bound_host;
  const uint64_t attempt_epoch = ++state.console_enable_attempt_epoch;
  state.console_pending_enable_attempts.insert(attempt_epoch);
  operation->domain_enable_attempt = Operation::DomainEnableAttempt{
      .generation = generation,
      .host = host,
      .epoch = attempt_epoch,
      .network_domain = false,
  };
  SendCommand(
      request_id, "Runtime.enable", base::DictValue(),
      base::BindOnce(
          [](base::WeakPtr<DaoDevToolsTools> self, std::string request_id,
             uint64_t generation,
             scoped_refptr<content::DevToolsAgentHost> host,
             uint64_t attempt_epoch,
             base::expected<base::Value, DaoToolError> result) {
            if (!self) {
              return;
            }
            Operation *operation = self->FindOperation(request_id);
            if (!operation || !operation->session) {
              return;
            }
            DaoBrowserAutomationSession::DevToolsState &state =
                operation->session->devtools_state();
            if (!result.has_value()) {
              ApplyDomainEnableResult(&state, generation, host.get(),
                                      attempt_epoch, false, false);
              self->FinishError(request_id, std::move(result).error());
              return;
            }
            ApplyDomainEnableResult(&state, generation, host.get(),
                                    attempt_epoch, false, true);
            self->FinishSuccess(
                request_id,
                base::Value(base::DictValue()
                                .Set("success", true)
                                .Set("message", "Console tracking enabled")));
          },
          weak_factory_.GetWeakPtr(), std::string(request_id), generation,
          std::move(host), attempt_epoch));
}

void DaoDevToolsTools::ExecuteGetConsole(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation || !operation->session) {
    return;
  }
  const std::string *filter = operation->arguments.FindString("filter");
  const DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  base::ListValue messages;
  for (const base::DictValue &entry : state.console_messages) {
    const std::string *type = entry.FindString("type");
    if (filter && !filter->empty() && type && *type != *filter) {
      continue;
    }
    messages.Append(entry.Clone());
  }
  const int count = static_cast<int>(messages.size());
  FinishSuccess(
      request_id,
      base::Value(
          base::DictValue()
              .Set("messages", std::move(messages))
              .Set("count", count)
              .Set("enabled", state.console_tracking_enabled)
              .Set("stored_bytes",
                   static_cast<double>(state.console_message_bytes))
              .Set("dropped_count",
                   static_cast<double>(state.console_messages_dropped))
              .Set("truncated_field_count",
                   static_cast<double>(state.console_fields_truncated))));
}

void DaoDevToolsTools::ExecuteClearConsole(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation || !operation->session) {
    return;
  }
  DaoBrowserAutomationSession::DevToolsState &state =
      operation->session->devtools_state();
  state.console_messages.clear();
  state.console_message_bytes = 0;
  state.console_messages_dropped = 0;
  state.console_fields_truncated = 0;
  ClearDomainStaging(&state, false);
  FinishSuccess(request_id,
                base::Value(base::DictValue().Set("success", true)));
}

void DaoDevToolsTools::ExecuteListResources(std::string_view request_id) {
  SendCommand(request_id, "Page.enable", base::DictValue(),
              base::BindOnce(&DaoDevToolsTools::OnPageEnabledForResourceList,
                             weak_factory_.GetWeakPtr(),
                             std::string(request_id)));
}

void DaoDevToolsTools::OnPageEnabledForResourceList(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  SendCommand(request_id, "Page.getResourceTree", base::DictValue(),
              base::BindOnce(&DaoDevToolsTools::OnResourceTreeForList,
                             weak_factory_.GetWeakPtr(), request_id));
}

void DaoDevToolsTools::OnResourceTreeForList(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const std::string *type_filter =
      operation->arguments.FindString("type_filter");
  base::ListValue resources;
  std::string main_frame_id;
  const base::DictValue *response = result->GetIfDict();
  const base::DictValue *tree =
      response ? response->FindDict("frameTree") : nullptr;
  ResourceTraversal traversal;
  if (tree) {
    traversal = TraverseResourceTree(
        *tree, nullptr, type_filter ? *type_filter : std::string());
    for (ResourceTarget &resource : traversal.resources) {
      base::DictValue entry;
      entry.Set("url", std::move(resource.url));
      entry.Set("type", std::move(resource.type));
      entry.Set("mimeType", std::move(resource.mime_type));
      entry.Set("frameId", std::move(resource.frame_id));
      if (resource.content_size) {
        entry.Set("contentSize", *resource.content_size);
      }
      resources.Append(std::move(entry));
    }
    const base::DictValue *frame = tree->FindDict("frame");
    const std::string *frame_id = frame ? frame->FindString("id") : nullptr;
    if (frame_id) {
      main_frame_id = *frame_id;
    }
  }
  const int count = static_cast<int>(resources.size());
  FinishSuccess(
      request_id,
      base::Value(
          base::DictValue()
              .Set("resources", std::move(resources))
              .Set("count", count)
              .Set("mainFrameId", std::move(main_frame_id))
              .Set("processed", static_cast<double>(traversal.processed))
              .Set("eligible", static_cast<double>(traversal.eligible))
              .Set("skipped", static_cast<double>(traversal.skipped))
              .Set("resource_limit_hit", traversal.resource_limit_hit)));
}

void DaoDevToolsTools::ExecuteGetResourceContent(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  const std::string *url =
      operation ? operation->arguments.FindString("url") : nullptr;
  if (!url || url->empty()) {
    FinishError(request_id, InvalidArgument("url must not be empty."));
    return;
  }
  SendCommand(request_id, "Page.enable", base::DictValue(),
              base::BindOnce(&DaoDevToolsTools::OnPageEnabledForResourceContent,
                             weak_factory_.GetWeakPtr(),
                             std::string(request_id)));
}

void DaoDevToolsTools::OnPageEnabledForResourceContent(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  SendCommand(request_id, "Page.getResourceTree", base::DictValue(),
              base::BindOnce(&DaoDevToolsTools::OnResourceTreeForContent,
                             weak_factory_.GetWeakPtr(), request_id));
}

void DaoDevToolsTools::OnResourceTreeForContent(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const std::string *url = operation->arguments.FindString("url");
  const std::string *required_frame_id =
      operation->arguments.FindString("frame_id");
  const base::DictValue *response = result->GetIfDict();
  const base::DictValue *tree =
      response ? response->FindDict("frameTree") : nullptr;
  std::optional<double> content_size;
  const std::string frame_id =
      tree && url ? FindFrameIdForUrl(*tree, *url,
                                      required_frame_id ? *required_frame_id
                                                        : std::string_view(),
                                      &content_size)
                  : std::string();
  if (frame_id.empty()) {
    if (required_frame_id && !required_frame_id->empty()) {
      FinishError(
          request_id,
          InvalidArgument("frame_id does not own the requested resource URL."));
      return;
    }
    FinishSuccess(
        request_id,
        base::Value(base::DictValue()
                        .Set("url", url ? *url : std::string())
                        .Set("error", "Could not resolve frame id for url")));
    return;
  }
  if (content_size &&
      *content_size > static_cast<double>(kMaxResourceDecodedBytes)) {
    FinishError(
        request_id,
        InvalidArgument("Resource content exceeds the 1 MiB decoded limit."));
    return;
  }
  operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  operation->resource_content_size = content_size;
  FetchResourceContent(request_id, frame_id);
}

void DaoDevToolsTools::FetchResourceContent(std::string_view request_id,
                                            std::string frame_id) {
  Operation *operation = FindOperation(request_id);
  const std::string *url =
      operation ? operation->arguments.FindString("url") : nullptr;
  if (!url) {
    return;
  }
  SendCommand(
      request_id, "Page.getResourceContent",
      base::DictValue().Set("frameId", std::move(frame_id)).Set("url", *url),
      base::BindOnce(&DaoDevToolsTools::OnResourceContent,
                     weak_factory_.GetWeakPtr(), std::string(request_id)),
      kMaxResourceProtocolResponseBytes);
}

void DaoDevToolsTools::OnResourceContent(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const std::string *url = operation->arguments.FindString("url");
  base::DictValue data;
  data.Set("url", url ? *url : std::string());
  const base::DictValue *response = result->GetIfDict();
  const std::string *content =
      response ? response->FindString("content") : nullptr;
  if (!content) {
    data.Set("error", response ? "No content for url" : "CDP call failed");
    FinishSuccess(request_id, base::Value(std::move(data)));
    return;
  }
  const bool base64_encoded =
      response->FindBool("base64Encoded").value_or(false);
  if (base64_encoded) {
    if (content->size() > kMaxResourceEncodedBytes) {
      FinishError(
          request_id,
          InvalidArgument("Resource content exceeds the 1 MiB decoded limit."));
      return;
    }
    data.Set("content", *content);
    data.Set("base64_encoded", true);
  } else {
    if (content->size() > kMaxResourceDecodedBytes) {
      FinishError(
          request_id,
          InvalidArgument("Resource content exceeds the 1 MiB decoded limit."));
      return;
    }
    std::string text = *content;
    const bool truncated = TruncateText(&text);
    data.Set("content", std::move(text));
    data.Set("base64_encoded", false);
    if (truncated) {
      data.Set("truncated", true);
    }
  }
  FinishSuccess(request_id, base::Value(std::move(data)));
}

void DaoDevToolsTools::ExecuteSearch(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const std::string *pattern = operation->arguments.FindString("pattern");
  if (!pattern || pattern->empty()) {
    FinishError(request_id, InvalidArgument("pattern must not be empty."));
    return;
  }
  operation->search_pattern = *pattern;
  const std::string *flags = operation->arguments.FindString("flags");
  operation->search_flags = flags && !flags->empty() ? *flags : "i";
  std::string regex_error;
  if (!ConfigureRegex(operation->search_pattern, operation->search_flags,
                      &operation->search_regex, &regex_error)) {
    FinishError(request_id, InvalidArgument("Invalid regex: " + regex_error));
    return;
  }
  std::optional<size_t> max_matches =
      ParseMaxMatches(operation->arguments.Find("max_matches"));
  if (!max_matches) {
    FinishError(request_id,
                InvalidArgument(
                    "max_matches must be greater than 0 and at most 1000."));
    return;
  }
  operation->max_matches = *max_matches;

  const std::string *raw_types = operation->arguments.FindString("types");
  for (const std::string &type : base::SplitString(
           raw_types && !raw_types->empty() ? *raw_types
                                            : "Script,Stylesheet,Document",
           ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    operation->search_types.insert(type);
  }
  for (std::string_view binary : {"Image", "Font", "Media", "Manifest"}) {
    operation->search_types.erase(std::string(binary));
  }

  SendCommand(request_id, "Page.enable", base::DictValue(),
              base::BindOnce(&DaoDevToolsTools::OnPageEnabledForSearch,
                             weak_factory_.GetWeakPtr(),
                             std::string(request_id)));
}

void DaoDevToolsTools::OnPageEnabledForSearch(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  SendCommand(request_id, "Page.getResourceTree", base::DictValue(),
              base::BindOnce(&DaoDevToolsTools::OnResourceTreeForSearch,
                             weak_factory_.GetWeakPtr(), request_id));
}

void DaoDevToolsTools::OnResourceTreeForSearch(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    FinishError(request_id, std::move(result).error());
    return;
  }
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const base::DictValue *response = result->GetIfDict();
  const base::DictValue *tree =
      response ? response->FindDict("frameTree") : nullptr;
  if (tree) {
    ResourceTraversal traversal =
        TraverseResourceTree(*tree, &operation->search_types);
    operation->search_targets = std::move(traversal.resources);
    operation->search_processed = traversal.processed;
    operation->search_eligible = traversal.eligible;
    operation->search_skipped = traversal.skipped;
    operation->search_resource_limit_hit = traversal.resource_limit_hit;
  }
  FetchNextSearchResource(request_id);
}

void DaoDevToolsTools::FetchNextSearchResource(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  if (operation->search_matches.size() >= operation->max_matches ||
      operation->next_search_target >= operation->search_targets.size() ||
      operation->search_scanned_bytes >= kMaxSearchScannedBytes) {
    operation->search_scan_limit_hit =
        operation->search_scanned_bytes >= kMaxSearchScannedBytes &&
        operation->next_search_target < operation->search_targets.size();
    FinishSearch(request_id);
    return;
  }
  const ResourceTarget &target =
      operation->search_targets[operation->next_search_target++];
  if (target.type == "Document") {
    SendCommand(request_id, "Page.createIsolatedWorld",
                base::DictValue()
                    .Set("frameId", target.frame_id)
                    .Set("worldName", "dao-resource-search")
                    .Set("grantUniveralAccess", false),
                base::BindOnce(&DaoDevToolsTools::OnSearchDocumentWorld,
                               weak_factory_.GetWeakPtr(),
                               std::string(request_id)));
    return;
  }
  SendCommand(
      request_id, "Page.getResourceContent",
      base::DictValue().Set("frameId", target.frame_id).Set("url", target.url),
      base::BindOnce(&DaoDevToolsTools::OnSearchResourceContent,
                     weak_factory_.GetWeakPtr(), std::string(request_id)),
      kMaxResourceProtocolResponseBytes);
}

void DaoDevToolsTools::OnSearchDocumentWorld(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  if (!result.has_value()) {
    OnSearchResourceContent(request_id, std::move(result));
    return;
  }
  const base::DictValue *response = result->GetIfDict();
  const std::optional<int> execution_context_id =
      response ? response->FindInt("executionContextId") : std::nullopt;
  if (!execution_context_id) {
    OnSearchResourceContent(
        request_id,
        base::unexpected(InternalError(
            "No execution context was created for the document resource.")));
    return;
  }
  SendCommand(request_id, "Runtime.evaluate",
              base::DictValue()
                  .Set("contextId", *execution_context_id)
                  .Set("expression", "document.documentElement.outerHTML")
                  .Set("returnByValue", true),
              base::BindOnce(&DaoDevToolsTools::OnSearchResourceContent,
                             weak_factory_.GetWeakPtr(), request_id),
              kMaxSearchProtocolResponseBytes);
}

void DaoDevToolsTools::OnSearchResourceContent(
    std::string request_id, base::expected<base::Value, DaoToolError> result) {
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const size_t completed_index = operation->next_search_target - 1;
  const ResourceTarget &target = operation->search_targets[completed_index];
  if (!result.has_value()) {
    ++operation->search_fetch_failed;
    if (result.error().code == DaoToolErrorCode::kInvalidArgument) {
      ++operation->search_content_limit_failures;
    }
    FetchNextSearchResource(request_id);
    return;
  }
  const base::DictValue *response = result->GetIfDict();
  if (response && response->Find("exceptionDetails")) {
    ++operation->search_fetch_failed;
    FetchNextSearchResource(request_id);
    return;
  }
  const base::DictValue *remote_result =
      response ? response->FindDict("result") : nullptr;
  const std::string *content =
      response ? response->FindString("content") : nullptr;
  if (!content && remote_result) {
    content = remote_result->FindString("value");
  }
  const bool base64_encoded =
      response && response->FindBool("base64Encoded").value_or(false);
  if (content && !base64_encoded) {
    ++operation->search_sources_searched;
    const size_t remaining_scan_bytes =
        kMaxSearchScannedBytes - operation->search_scanned_bytes;
    const size_t source_limit = std::min(kMaxSourceBytes, remaining_scan_bytes);
    std::string text =
        content->size() > source_limit
            ? std::string(base::TruncateUTF8ToByteSize(*content, source_limit))
            : *content;
    if (text.size() < content->size()) {
      ++operation->search_sources_truncated;
      ++operation->search_content_limit_failures;
      if (remaining_scan_bytes <= kMaxSourceBytes) {
        operation->search_scan_limit_hit = true;
      }
    }
    operation->search_scanned_bytes += text.size();
    const std::string_view text_view(text);
    size_t line_start = 0;
    int line_number = 1;
    while (line_start <= text.size() &&
           operation->search_matches.size() < operation->max_matches) {
      size_t line_end = text.find('\n', line_start);
      if (line_end == std::string::npos) {
        line_end = text.size();
      }
      const std::string_view line =
          text_view.substr(line_start, line_end - line_start);
      if (re2::RE2::PartialMatch(re2::StringPiece(line.data(), line.size()),
                                 *operation->search_regex)) {
        std::string excerpt = MakeSearchExcerpt(line);
        operation->search_matches.Append(
            base::DictValue()
                .Set("url", target.url)
                .Set("frame_id", target.frame_id)
                .Set("line", line_number)
                .Set("excerpt", std::move(excerpt)));
      }
      if (line_end == text.size()) {
        break;
      }
      line_start = line_end + 1;
      ++line_number;
    }
  } else {
    ++operation->search_fetch_failed;
  }
  FetchNextSearchResource(request_id);
}

void DaoDevToolsTools::FinishSearch(std::string_view request_id) {
  Operation *operation = FindOperation(request_id);
  if (!operation) {
    return;
  }
  const bool truncated =
      operation->search_matches.size() >= operation->max_matches;
  const bool source_limit_hit = operation->search_sources_truncated > 0 ||
                                operation->search_content_limit_failures > 0;
  const bool incomplete =
      truncated || source_limit_hit || operation->search_fetch_failed > 0 ||
      operation->search_resource_limit_hit || operation->search_scan_limit_hit;
  base::DictValue data;
  data.Set("pattern", operation->search_pattern);
  data.Set("flags", operation->search_flags);
  data.Set("attempted", static_cast<int>(operation->next_search_target));
  data.Set("searched", static_cast<int>(operation->search_sources_searched));
  data.Set("fetch_failed", static_cast<int>(operation->search_fetch_failed));
  data.Set("content_limit_failures",
           static_cast<int>(operation->search_content_limit_failures));
  data.Set("matches", std::move(operation->search_matches));
  data.Set("truncated", truncated);
  data.Set("source_truncated", operation->search_sources_truncated > 0);
  data.Set("source_limit", static_cast<double>(kMaxSourceBytes));
  data.Set("sources_truncated",
           static_cast<double>(operation->search_sources_truncated));
  data.Set("scanned_bytes",
           static_cast<double>(operation->search_scanned_bytes));
  data.Set("processed", static_cast<double>(operation->search_processed));
  data.Set("eligible", static_cast<double>(operation->search_eligible));
  data.Set("skipped", static_cast<double>(operation->search_skipped));
  data.Set("resource_limit_hit", operation->search_resource_limit_hit);
  data.Set("scan_limit_hit", operation->search_scan_limit_hit);
  data.Set("source_limit_hit", source_limit_hit);
  data.Set("incomplete", incomplete);
  FinishSuccess(request_id, base::Value(std::move(data)));
}

} // namespace dao
