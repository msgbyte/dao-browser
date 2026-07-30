// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_session_lifecycle_monitor.h"

#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/automation/dao_browser_automation_session.h"
#include "dao/browser/automation/dao_browser_target_policy.h"

namespace dao {
namespace {

DaoToolError TargetGone(std::string message) {
  return MakeDaoToolError(DaoToolErrorCode::kTargetGone, std::move(message));
}

}  // namespace

DaoMcpSessionLifecycleMonitor::DaoMcpSessionLifecycleMonitor(
    DaoBrowserAutomationSession* session,
    InvalidatedCallback callback)
    : session_(session ? session->GetWeakPtr() : nullptr),
      callback_(std::move(callback)) {}

DaoMcpSessionLifecycleMonitor::~DaoMcpSessionLifecycleMonitor() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Detach();
}

void DaoMcpSessionLifecycleMonitor::Start() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (started_ || invalidated_) {
    return;
  }
  started_ = true;
  if (!session_) {
    Invalidate(TargetGone("The MCP browser session is no longer available."));
    return;
  }

  browser_window_ = session_->browser_window();
  profile_ = session_->profile();
  tab_list_ =
      browser_window_ ? TabListInterface::From(browser_window_.get()) : nullptr;
  if (!browser_window_ || !profile_ || !tab_list_) {
    Invalidate(TargetGone(
        "The authorized MCP browser owners are no longer available."));
    return;
  }

  browser_close_subscription_ =
      browser_window_->RegisterBrowserDidClose(base::BindRepeating(
          [](base::WeakPtr<DaoMcpSessionLifecycleMonitor> monitor,
             BrowserWindowInterface*) {
            if (monitor) {
              monitor->InvalidateAfterObserverNotification(
                  TargetGone("The authorized MCP browser window was closed."));
            }
          },
          weak_factory_.GetWeakPtr()));
  profile_observation_.Observe(profile_);
  tab_list_observation_.Observe(tab_list_);
  target_changed_subscription_ = session_->AddTargetChangedObserver(
      base::BindRepeating(&DaoMcpSessionLifecycleMonitor::OnTargetChanged,
                          weak_factory_.GetWeakPtr()));
  Revalidate();
}

void DaoMcpSessionLifecycleMonitor::OnTargetChanged(
    DaoBrowserAutomationSession* session) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (session_.get() != session) {
    ScheduleRevalidate();
    return;
  }
  // Move the WebContents observation synchronously while the old target is
  // still alive. Policy validation and terminal callbacks remain deferred so
  // SetTarget() can finish notifying its CallbackList without deleting the
  // session or this monitor from inside the notification.
  auto target = session->ResolveTarget();
  if (target.has_value() && web_contents() != *target) {
    Observe(*target);
  }
  ScheduleRevalidate();
}

void DaoMcpSessionLifecycleMonitor::ScheduleRevalidate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (invalidated_) {
    return;
  }
  const uint64_t generation = ++revalidation_generation_;
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoMcpSessionLifecycleMonitor::RevalidateIfCurrent,
                     weak_factory_.GetWeakPtr(), generation));
}

void DaoMcpSessionLifecycleMonitor::RevalidateIfCurrent(uint64_t generation) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (generation != revalidation_generation_) {
    return;
  }
  Revalidate();
}

void DaoMcpSessionLifecycleMonitor::Revalidate() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (invalidated_ || revalidating_) {
    return;
  }
  if (!session_ || !browser_window_ || !profile_) {
    Invalidate(TargetGone("The MCP browser session is no longer available."));
    return;
  }

  revalidating_ = true;
  base::WeakPtr<DaoMcpSessionLifecycleMonitor> weak_this =
      weak_factory_.GetWeakPtr();
  auto target = session_->ResolveTarget();
  if (!weak_this) {
    return;
  }
  revalidating_ = false;
  if (!session_) {
    Invalidate(TargetGone("The MCP browser session is no longer available."));
    return;
  }
  if (!target.has_value()) {
    Invalidate(std::move(target).error());
    return;
  }
  if (web_contents() != *target) {
    Observe(*target);
  }

  // A discarded tab retains its stable TabHandle while Chromium installs a
  // replacement WebContents. Its committed URL is empty until the replacement
  // commits, so defer URL policy until PrimaryPageChanged supplies a stable
  // document.
  if ((*target)->GetLastCommittedURL().is_empty()) {
    return;
  }

  Browser* browser = browser_window_->GetBrowserForMigrationOnly();
  auto policy = ValidateExternalTarget(browser, profile_, *target);
  if (!policy.has_value()) {
    Invalidate(std::move(policy).error());
    return;
  }
  if (!session_->RefreshTargetDocumentSnapshot(*target)) {
    Invalidate(
        TargetGone("The authorized MCP browser page changed during review."));
  }
}

void DaoMcpSessionLifecycleMonitor::Invalidate(DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (invalidated_) {
    return;
  }
  invalidated_ = true;
  InvalidatedCallback callback = std::move(callback_);
  Detach();
  if (callback) {
    std::move(callback).Run(std::move(error));
  }
}

void DaoMcpSessionLifecycleMonitor::InvalidateAfterObserverNotification(
    DaoToolError error) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (invalidated_) {
    return;
  }
  invalidated_ = true;
  InvalidatedCallback callback = std::move(callback_);
  Detach();
  if (callback) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(std::move(callback), std::move(error)));
  }
}

void DaoMcpSessionLifecycleMonitor::Detach() {
  Observe(nullptr);
  target_changed_subscription_ = {};
  tab_list_observation_.Reset();
  profile_observation_.Reset();
  browser_close_subscription_ = {};
  tab_list_ = nullptr;
  profile_ = nullptr;
  browser_window_ = nullptr;
}

void DaoMcpSessionLifecycleMonitor::OnProfileWillBeDestroyed(Profile*) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InvalidateAfterObserverNotification(
      TargetGone("The authorized MCP browser profile was destroyed."));
}

void DaoMcpSessionLifecycleMonitor::OnTabRemoved(TabListInterface&,
                                                 tabs::TabInterface*,
                                                 TabRemovedReason) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  // A Dao close_tab operation may retarget the session later in the same tab
  // removal notification. Revalidate after every observer has seen the event.
  ScheduleRevalidate();
}

void DaoMcpSessionLifecycleMonitor::OnWebContentsReplaced(
    TabListInterface&,
    tabs::TabInterface*,
    content::WebContents* old_contents,
    content::WebContents* new_contents) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (web_contents() == old_contents) {
    Observe(new_contents);
  }
  ScheduleRevalidate();
}

void DaoMcpSessionLifecycleMonitor::OnTabListDestroyed(TabListInterface&) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InvalidateAfterObserverNotification(
      TargetGone("The authorized MCP browser tab list was destroyed."));
}

void DaoMcpSessionLifecycleMonitor::PrimaryPageChanged(content::Page&) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ScheduleRevalidate();
}

void DaoMcpSessionLifecycleMonitor::WebContentsDestroyed() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  InvalidateAfterObserverNotification(
      TargetGone("The authorized MCP browser page was destroyed."));
}

}  // namespace dao
