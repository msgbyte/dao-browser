// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_browser_tool_executor.h"

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_target_policy.h"
#include "dao/browser/automation/dao_browser_tool_catalog.h"
#include "dao/browser/automation/dao_devtools_tools.h"
#include "dao/browser/automation/dao_tab_tools.h"
#include "dao/browser/automation/dao_tool_schema_validator.h"
#include "dao/browser/ui/views/dao_tab_identity.h"

namespace dao {
namespace {

base::expected<content::WebContents*, DaoToolError> ResolveTargetForClient(
    DaoBrowserAutomationSession* session,
    DaoToolClient client) {
  if (!session) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                         "Browser automation session is no longer available."));
  }
  if (client == DaoToolClient::kDaoAgent) {
    return session->ResolveEligibleTarget();
  }

  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  auto target = session->ResolveTarget();
  if (!session_weak) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "Browser automation session was destroyed while resolving its "
        "target."));
  }
  if (!target.has_value()) {
    return base::unexpected(std::move(target).error());
  }
  BrowserWindowInterface* browser_window = session_weak->browser_window();
  Browser* browser =
      browser_window ? browser_window->GetBrowserForMigrationOnly() : nullptr;
  auto policy = ValidateExternalTarget(browser, session_weak->profile(), *target);
  if (!policy.has_value()) {
    return base::unexpected(std::move(policy).error());
  }
  return *target;
}

}  // namespace

struct DaoBrowserToolExecutor::PendingRequest {
  PendingRequest(ResultCallback callback,
                 content::WebContents* target,
                 base::TimeDelta timeout,
                 base::OnceClosure deadline)
      : callback(std::move(callback)),
        target(target ? target->GetWeakPtr() : nullptr) {
    timer.Start(FROM_HERE, timeout, std::move(deadline));
  }

  ~PendingRequest() = default;

  base::OneShotTimer timer;
  ResultCallback callback;
  base::WeakPtr<content::WebContents> target;
};

DaoBrowserToolExecutor::DaoBrowserToolExecutor(
    DaoDevToolsClient* devtools_client,
    DaoPageTools::UiDelegate* ui_delegate)
    : DaoBrowserToolExecutor(devtools_client,
                             ui_delegate,
                             DaoBrowserToolCatalog::Get()) {}

DaoBrowserToolExecutor::DaoBrowserToolExecutor(
    DaoDevToolsClient* devtools_client,
    DaoPageTools::UiDelegate* ui_delegate,
    const DaoBrowserToolCatalog* catalog)
    : catalog_(catalog),
      page_tools_(std::make_unique<DaoPageTools>(devtools_client, ui_delegate)),
      tab_tools_(std::make_unique<DaoTabTools>()),
      devtools_tools_(std::make_unique<DaoDevToolsTools>(devtools_client)) {
  CHECK(catalog_);
}

DaoBrowserToolExecutor::~DaoBrowserToolExecutor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  is_shutting_down_ = true;
  is_cancelling_ = false;
  CancelAll(MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                             "Browser tool executor was destroyed."));
  ClearSessionState(nullptr);
  weak_factory_.InvalidateWeakPtrs();
}

bool DaoBrowserToolExecutor::Execute(DaoBrowserAutomationSession* session,
                                     DaoToolClient client,
                                     DaoBrowserToolCall call,
                                     ResultCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_shutting_down_ || is_cancelling_) {
    ReplyError(std::move(callback),
               MakeDaoToolError(DaoToolErrorCode::kToolCancelled,
                                "Browser tool executor is shutting down."));
    return false;
  }
  const DaoBrowserToolDefinition* definition =
      catalog_->Find(call.name, client);
  if (!definition) {
    ReplyError(std::move(callback),
               MakeDaoToolError(DaoToolErrorCode::kUnknownTool,
                                "Unknown or unavailable browser tool."));
    return false;
  }

  auto validation = ValidateToolArguments(*definition, call.arguments);
  if (!validation.has_value()) {
    ReplyError(std::move(callback), std::move(validation).error());
    return false;
  }
  if (call.name == "switch_tab") {
    const std::string* tab_id = call.arguments.FindString("tab_id");
    if ((!tab_id || tab_id->empty()) && !call.arguments.contains("index")) {
      ReplyError(std::move(callback),
                 MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                                  "switch_tab requires tab_id or index."));
      return false;
    }
  }

  if (!session) {
    ReplyError(std::move(callback),
               MakeDaoToolError(DaoToolErrorCode::kTargetGone,
                                "Browser automation session is unavailable."));
    return false;
  }
  base::WeakPtr<DaoBrowserToolExecutor> weak_this = weak_factory_.GetWeakPtr();
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session->GetWeakPtr();
  auto target = ResolveTargetForClient(session, client);
  if (!weak_this || !session_weak) {
    DaoBrowserToolResult result;
    result.error = MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "Browser tool ownership changed while resolving the target.");
    std::move(callback).Run(std::move(result));
    return false;
  }
  if (!target.has_value()) {
    ReplyError(std::move(callback), std::move(target).error());
    return false;
  }

  if (call.request_id.empty() ||
      weak_this->pending_.contains(call.request_id)) {
    ReplyError(std::move(callback),
               MakeDaoToolError(DaoToolErrorCode::kInvalidArgument,
                                "Browser tool request id is empty or active."));
    return false;
  }

  const base::TimeDelta timeout =
      call.timeout.is_positive() ? call.timeout : definition->timeout;
  const std::string request_id = call.request_id;
  weak_this->pending_.emplace(
      request_id, std::make_unique<PendingRequest>(
                      std::move(callback), *target, timeout,
                      base::BindOnce(&DaoBrowserToolExecutor::OnDeadline,
                                     weak_this, request_id)));

  if (DaoTabTools::Handles(call.name)) {
    weak_this->tab_tools_->Execute(
        request_id, session_weak.get(), client, call.name, call.arguments,
        base::BindOnce(&DaoBrowserToolExecutor::Complete, weak_this,
                       request_id));
    return true;
  }

  weak_this->Dispatch(
      request_id, call.name, session_weak.get(), *target,
      session_weak->committed_origin(),
      session_weak->document_sequence_number(),
      base::BindRepeating(
          [](base::WeakPtr<DaoBrowserAutomationSession> session,
             DaoToolClient client)
              -> base::expected<content::WebContents*, DaoToolError> {
            if (!session) {
              return base::unexpected(MakeDaoToolError(
                  DaoToolErrorCode::kTargetGone,
                  "Browser automation session is no longer available."));
            }
            return ResolveTargetForClient(session.get(), client);
          },
          session_weak, client),
      std::move(call.arguments));
  return true;
}

void DaoBrowserToolExecutor::Cancel(std::string_view request_id,
                                    DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;
  }
  if (tab_tools_->Cancel(request_id, error)) {
    return;
  }
  if (devtools_tools_->Cancel(request_id, error)) {
    return;
  }
  if (!page_tools_->Cancel(request_id, error)) {
    DaoBrowserToolResult result;
    result.error = std::move(error);
    Complete(std::string(request_id), std::move(result));
  }
}

void DaoBrowserToolExecutor::CancelAll(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (is_cancelling_) {
    return;
  }
  is_cancelling_ = true;
  base::WeakPtr<DaoBrowserToolExecutor> weak_this = weak_factory_.GetWeakPtr();
  std::vector<std::string> request_ids;
  request_ids.reserve(pending_.size());
  for (const auto& [request_id, _] : pending_) {
    request_ids.push_back(request_id);
  }
  for (const std::string& request_id : request_ids) {
    Cancel(request_id, error);
    if (!weak_this) {
      return;
    }
  }
  if (weak_this) {
    weak_this->is_cancelling_ = false;
  }
}

void DaoBrowserToolExecutor::ClearPageState(
    DaoBrowserAutomationSession* session) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::WeakPtr<DaoBrowserToolExecutor> weak_this = weak_factory_.GetWeakPtr();
  if (session) {
    base::WeakPtr<DaoBrowserAutomationSession> session_weak =
        session->GetWeakPtr();
    auto target = session->ResolveTarget();
    if (!weak_this || !session_weak) {
      return;
    }
    if (target.has_value()) {
      weak_this->page_tools_->ClearHighlights(*target);
      if (!weak_this) {
        return;
      }
    }
  }
  weak_this->page_tools_->ClearAllHighlights();
  if (!weak_this) {
    return;
  }
  weak_this->page_tools_->ClearCursors();
}

void DaoBrowserToolExecutor::ClearSessionState(
    DaoBrowserAutomationSession* session) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::WeakPtr<DaoBrowserToolExecutor> weak_this = weak_factory_.GetWeakPtr();
  base::WeakPtr<DaoBrowserAutomationSession> session_weak =
      session ? session->GetWeakPtr() : nullptr;
  ClearPageState(session);
  if (weak_this) {
    weak_this->devtools_tools_->ClearSessionState(session_weak.get());
  }
}

void DaoBrowserToolExecutor::Dispatch(
    std::string request_id,
    std::string name,
    DaoBrowserAutomationSession* session,
    content::WebContents* target,
    url::Origin committed_origin,
    int64_t document_sequence_number,
    DaoPageTools::TargetResolver target_resolver,
    base::DictValue arguments) {
  if (!DaoPageTools::Handles(name)) {
    if (DaoDevToolsTools::Handles(name)) {
      devtools_tools_->Execute(
          request_id, session, std::move(name), std::move(arguments),
          base::BindOnce(&DaoBrowserToolExecutor::Complete,
                         weak_factory_.GetWeakPtr(), request_id));
      return;
    }
    DaoBrowserToolResult result;
    result.error = MakeDaoToolError(
        DaoToolErrorCode::kInternalError,
        "Browser tool does not have a registered native handler.");
    Complete(std::move(request_id), std::move(result));
    return;
  }
  page_tools_->Execute(request_id, std::move(name), target,
                       std::move(committed_origin), document_sequence_number,
                       std::move(target_resolver), std::move(arguments),
                       base::BindOnce(&DaoBrowserToolExecutor::Complete,
                                      weak_factory_.GetWeakPtr(), request_id));
}

void DaoBrowserToolExecutor::Complete(std::string request_id,
                                      DaoBrowserToolResult result) {
  auto it = pending_.find(request_id);
  if (it == pending_.end()) {
    return;
  }
  std::unique_ptr<PendingRequest> pending = std::move(it->second);
  pending_.erase(it);
  pending->timer.Stop();

  if (!result.target && pending->target) {
    result.target = DaoToolTarget{
        .tab_id = GetOrCreateSidebarTabId(pending->target.get()),
        .url = pending->target->GetVisibleURL().spec(),
    };
  }
  std::move(pending->callback).Run(std::move(result));
}

void DaoBrowserToolExecutor::ReplyError(ResultCallback callback,
                                        DaoToolError error) {
  DaoBrowserToolResult result;
  result.error = std::move(error);
  std::move(callback).Run(std::move(result));
}

void DaoBrowserToolExecutor::OnDeadline(std::string request_id) {
  Cancel(request_id,
         MakeDaoToolError(DaoToolErrorCode::kToolTimeout,
                          "Browser tool call exceeded its deadline.", true));
}

}  // namespace dao
