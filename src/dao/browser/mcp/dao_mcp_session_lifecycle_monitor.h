// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_MCP_SESSION_LIFECYCLE_MONITOR_H_
#define DAO_BROWSER_MCP_DAO_MCP_SESSION_LIFECYCLE_MONITOR_H_

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "chrome/browser/tab_list/tab_list_interface_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

class BrowserWindowInterface;
class Profile;
class TabListInterface;

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace dao {

class DaoBrowserAutomationSession;

// Observes every owner that keeps an approved MCP target valid. The monitor
// reports the first terminal lifecycle or policy error and then detaches.
class DaoMcpSessionLifecycleMonitor : public ProfileObserver,
                                      public TabListInterfaceObserver,
                                      public content::WebContentsObserver {
 public:
  using InvalidatedCallback = base::OnceCallback<void(DaoToolError)>;

  DaoMcpSessionLifecycleMonitor(DaoBrowserAutomationSession* session,
                                InvalidatedCallback callback);
  ~DaoMcpSessionLifecycleMonitor() override;

  DaoMcpSessionLifecycleMonitor(const DaoMcpSessionLifecycleMonitor&) = delete;
  DaoMcpSessionLifecycleMonitor& operator=(
      const DaoMcpSessionLifecycleMonitor&) = delete;

  void Start();

 private:
  void OnTargetChanged(DaoBrowserAutomationSession* session);
  void ScheduleRevalidate();
  void RevalidateIfCurrent(uint64_t generation);
  void Revalidate();
  void Invalidate(DaoToolError error);
  void InvalidateAfterObserverNotification(DaoToolError error);
  void Detach();

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

  // TabListInterfaceObserver:
  void OnTabRemoved(TabListInterface& tab_list,
                    tabs::TabInterface* tab,
                    TabRemovedReason removed_reason) override;
  void OnWebContentsReplaced(TabListInterface& tab_list,
                             tabs::TabInterface* tab,
                             content::WebContents* old_contents,
                             content::WebContents* new_contents) override;
  void OnTabListDestroyed(TabListInterface& tab_list) override;

  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;

  base::WeakPtr<DaoBrowserAutomationSession> session_;
  InvalidatedCallback callback_;
  raw_ptr<BrowserWindowInterface> browser_window_ = nullptr;
  raw_ptr<Profile> profile_ = nullptr;
  raw_ptr<TabListInterface> tab_list_ = nullptr;
  base::CallbackListSubscription browser_close_subscription_;
  base::CallbackListSubscription target_changed_subscription_;
  base::ScopedObservation<Profile, ProfileObserver> profile_observation_{this};
  base::ScopedObservation<TabListInterface, TabListInterfaceObserver>
      tab_list_observation_{this};
  bool started_ = false;
  bool invalidated_ = false;
  bool revalidating_ = false;
  uint64_t revalidation_generation_ = 0;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoMcpSessionLifecycleMonitor> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_MCP_SESSION_LIFECYCLE_MONITOR_H_
