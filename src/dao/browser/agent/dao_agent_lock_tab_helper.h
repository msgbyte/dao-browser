// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_LOCK_TAB_HELPER_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_LOCK_TAB_HELPER_H_

#include <optional>

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace dao {

// Per-WebContents helper that locks a tab for AI agent control.
// When locked, user input events are blocked via
// WebContents::IgnoreInputEvents().  Programmatic input (CDP, ExecuteScript)
// remains unaffected.
class DaoAgentLockTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<DaoAgentLockTabHelper> {
 public:
  ~DaoAgentLockTabHelper() override;

  DaoAgentLockTabHelper(const DaoAgentLockTabHelper&) = delete;
  DaoAgentLockTabHelper& operator=(const DaoAgentLockTabHelper&) = delete;

  // Lock/unlock this tab for AI agent control.
  void Lock();
  void Unlock();
  bool is_locked() const { return locked_; }

  // Static convenience — creates the helper if needed.
  static void LockContents(content::WebContents* contents);
  static void UnlockContents(content::WebContents* contents);
  static bool IsLocked(content::WebContents* contents);

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  friend class content::WebContentsUserData<DaoAgentLockTabHelper>;
  explicit DaoAgentLockTabHelper(content::WebContents* contents);

  bool locked_ = false;
  std::optional<content::WebContents::ScopedIgnoreInputEvents>
      scoped_ignore_input_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_LOCK_TAB_HELPER_H_
