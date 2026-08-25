// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_browser_automation_session.h"

#include <utility>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/tab_list/tab_list_interface.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"

namespace dao {
namespace {

tabs::TabInterface* FindTabForContents(BrowserWindowInterface* browser_window,
                                       content::WebContents* target) {
  if (!browser_window || !target) {
    return nullptr;
  }
  TabListInterface* tabs = TabListInterface::From(browser_window);
  if (!tabs) {
    return nullptr;
  }
  for (tabs::TabInterface* tab : tabs->GetAllTabs()) {
    if (tab->GetContents() == target) {
      return tab;
    }
  }
  return nullptr;
}

bool WindowContainsTab(BrowserWindowInterface* browser_window,
                       tabs::TabInterface* target) {
  if (!browser_window || !target) {
    return false;
  }
  TabListInterface* tabs = TabListInterface::From(browser_window);
  if (!tabs) {
    return false;
  }
  for (tabs::TabInterface* tab : tabs->GetAllTabs()) {
    if (tab == target) {
      return true;
    }
  }
  return false;
}

}  // namespace

DaoBrowserAutomationSession::DevToolsState::DevToolsState() = default;

DaoBrowserAutomationSession::DevToolsState::~DevToolsState() = default;

bool IsAutomationUrlEligible(const GURL& url) {
  return url.SchemeIsHTTPOrHTTPS() || url.spec() == "about:blank";
}

bool IsDaoHomeUrl(const GURL& url) {
  return url.SchemeIs(content::kChromeUIScheme) && url.host() == "home";
}

DaoBrowserAutomationSession::DaoBrowserAutomationSession(
    BrowserWindowInterface* browser_window,
    content::WebContents* target,
    TargetPolicy target_policy)
    : browser_window_(browser_window ? browser_window->GetWeakPtr() : nullptr),
      profile_(browser_window ? browser_window->GetProfile()->GetWeakPtr()
                              : nullptr),
      target_policy_(target_policy) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  SetTarget(target);
}

DaoBrowserAutomationSession::~DaoBrowserAutomationSession() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

BrowserWindowInterface* DaoBrowserAutomationSession::browser_window() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return browser_window_.get();
}

Profile* DaoBrowserAutomationSession::profile() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return profile_.get();
}

void DaoBrowserAutomationSession::SetTarget(content::WebContents* target) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  content::WebContents* previous_target = resolved_contents_.get();
  const tabs::TabHandle previous_handle = target_handle_;
  tabs::TabInterface* target_tab =
      FindTabForContents(browser_window_.get(), target);
  target_handle_ =
      target_tab ? target_tab->GetHandle() : tabs::TabHandle::Null();
  resolved_contents_ =
      target_tab ? target_tab->GetContents()->GetWeakPtr() : nullptr;
  CaptureAuthorizationSnapshot(resolved_contents_.get());
  if (previous_handle != target_handle_ ||
      previous_target != resolved_contents_.get()) {
    NotifyTargetChanged();
  }
}

bool DaoBrowserAutomationSession::RefreshTargetDocumentSnapshot(
    content::WebContents* target) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto resolved_target = ResolveTarget();
  if (!resolved_target.has_value() || *resolved_target != target) {
    return false;
  }
  CaptureAuthorizationSnapshot(target);
  return true;
}

base::expected<content::WebContents*, DaoToolError>
DaoBrowserAutomationSession::ResolveTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  tabs::TabInterface* target_tab = target_handle_.Get();
  content::WebContents* target =
      target_tab ? target_tab->GetContents() : nullptr;
  if (!browser_window_ || !profile_ || !target_tab || !target ||
      browser_window_->GetProfile() != profile_.get() ||
      target->GetBrowserContext() != profile_.get() ||
      !WindowContainsTab(browser_window_.get(), target_tab)) {
    ClearResolvedTarget();
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The authorized browser target is no longer available."));
  }
  if (resolved_contents_.get() != target) {
    base::WeakPtr<DaoBrowserAutomationSession> weak_this =
        weak_factory_.GetWeakPtr();
    base::WeakPtr<content::WebContents> target_weak = target->GetWeakPtr();
    resolved_contents_ = target->GetWeakPtr();
    CaptureAuthorizationSnapshot(target);
    NotifyTargetChanged();
    if (!weak_this) {
      return base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "Browser automation session was destroyed while resolving its "
          "target."));
    }
    if (!target_weak ||
        weak_this->resolved_contents_.get() != target_weak.get()) {
      return base::unexpected(MakeDaoToolError(
          DaoToolErrorCode::kToolCancelled,
          "The authorized browser target changed again while resolving."));
    }
    target = target_weak.get();
  }
  return target;
}

base::expected<content::WebContents*, DaoToolError>
DaoBrowserAutomationSession::ResolveEligibleTarget() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  base::WeakPtr<DaoBrowserAutomationSession> weak_this =
      weak_factory_.GetWeakPtr();
  auto target = ResolveTarget();
  if (!weak_this) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kToolCancelled,
        "Browser automation session was destroyed while resolving its "
        "target."));
  }
  if (!target.has_value()) {
    return base::unexpected(std::move(target.error()));
  }
  if (!weak_this->browser_window_ || !weak_this->profile_) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kTargetGone,
        "The authorized browser target is no longer available."));
  }

  const auto& url = target.value()->GetLastCommittedURL();
  const bool eligible_url =
      IsAutomationUrlEligible(url) ||
      (weak_this->target_policy_ == TargetPolicy::kLegacyUiWithDaoHome &&
       IsDaoHomeUrl(url));
  if (weak_this->browser_window_->GetType() !=
          BrowserWindowInterface::TYPE_NORMAL ||
      weak_this->profile_->IsOffTheRecord() ||
      weak_this->profile_->IsGuestSession() || !eligible_url) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kTargetForbidden,
        "The authorized browser target is not eligible for automation."));
  }
  return target.value();
}

base::WeakPtr<DaoBrowserAutomationSession>
DaoBrowserAutomationSession::GetWeakPtr() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return weak_factory_.GetWeakPtr();
}

void DaoBrowserAutomationSession::SetTargetChangedCallback(
    TargetChangedCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  target_changed_callback_ = std::move(callback);
}

base::CallbackListSubscription
DaoBrowserAutomationSession::AddTargetChangedObserver(
    TargetChangedObserver observer) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return target_changed_observers_.Add(std::move(observer));
}

DaoBrowserAutomationSession::DevToolsState&
DaoBrowserAutomationSession::devtools_state() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return devtools_state_;
}

const DaoBrowserAutomationSession::DevToolsState&
DaoBrowserAutomationSession::devtools_state() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return devtools_state_;
}

const std::string& DaoBrowserAutomationSession::expected_domain() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return expected_domain_;
}

void DaoBrowserAutomationSession::set_expected_domain(
    std::string expected_domain) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  expected_domain_ = std::move(expected_domain);
}

const url::Origin& DaoBrowserAutomationSession::committed_origin() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return committed_origin_;
}

int64_t DaoBrowserAutomationSession::document_sequence_number() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return document_sequence_number_;
}

void DaoBrowserAutomationSession::CaptureAuthorizationSnapshot(
    content::WebContents* target) const {
  committed_origin_ = url::Origin();
  document_sequence_number_ = -1;
  if (!target || !target->GetPrimaryMainFrame()) {
    return;
  }
  committed_origin_ = target->GetPrimaryMainFrame()->GetLastCommittedOrigin();
  const content::NavigationEntry* entry =
      target->GetController().GetLastCommittedEntry();
  if (entry) {
    document_sequence_number_ = entry->GetMainFrameDocumentSequenceNumber();
  }
}

void DaoBrowserAutomationSession::NotifyTargetChanged() const {
  TargetChangedCallback callback = target_changed_callback_;
  base::WeakPtr<DaoBrowserAutomationSession> weak_this =
      weak_factory_.GetWeakPtr();
  if (callback) {
    callback.Run(const_cast<DaoBrowserAutomationSession*>(this));
  }
  if (!weak_this) {
    return;
  }
  target_changed_observers_.Notify(
      const_cast<DaoBrowserAutomationSession*>(this));
}

void DaoBrowserAutomationSession::ClearResolvedTarget() const {
  if (!resolved_contents_) {
    return;
  }
  resolved_contents_.reset();
  CaptureAuthorizationSnapshot(nullptr);
  NotifyTargetChanged();
}

}  // namespace dao
