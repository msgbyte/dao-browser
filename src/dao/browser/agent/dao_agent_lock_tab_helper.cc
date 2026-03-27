// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_lock_tab_helper.h"

#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"

namespace dao {

DaoAgentLockTabHelper::DaoAgentLockTabHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<DaoAgentLockTabHelper>(*web_contents) {}

DaoAgentLockTabHelper::~DaoAgentLockTabHelper() {
  if (locked_) {
    scoped_ignore_input_.reset();
  }
}

void DaoAgentLockTabHelper::Lock() {
  if (locked_) {
    return;
  }
  locked_ = true;
  scoped_ignore_input_.emplace(
      web_contents()->IgnoreInputEvents(std::nullopt));
  // Trigger tab UI update through the existing TabChangedAt pipeline.
  web_contents()->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
}

void DaoAgentLockTabHelper::Unlock() {
  if (!locked_) {
    return;
  }
  locked_ = false;
  scoped_ignore_input_.reset();
  web_contents()->NotifyNavigationStateChanged(content::INVALIDATE_TYPE_TAB);
}

// static
void DaoAgentLockTabHelper::LockContents(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  DaoAgentLockTabHelper::CreateForWebContents(contents);
  DaoAgentLockTabHelper::FromWebContents(contents)->Lock();
}

// static
void DaoAgentLockTabHelper::UnlockContents(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  auto* helper = DaoAgentLockTabHelper::FromWebContents(contents);
  if (helper) {
    helper->Unlock();
  }
}

// static
bool DaoAgentLockTabHelper::IsLocked(content::WebContents* contents) {
  if (!contents) {
    return false;
  }
  auto* helper = DaoAgentLockTabHelper::FromWebContents(contents);
  return helper && helper->is_locked();
}

void DaoAgentLockTabHelper::WebContentsDestroyed() {
  if (locked_) {
    locked_ = false;
    scoped_ignore_input_.reset();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoAgentLockTabHelper);

}  // namespace dao
