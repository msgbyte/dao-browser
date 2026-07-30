// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_BROWSER_AUTOMATION_SESSION_H_
#define DAO_BROWSER_AUTOMATION_DAO_BROWSER_AUTOMATION_SESSION_H_

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "components/tabs/public/tab_interface.h"
#include "dao/browser/automation/dao_browser_tool_types.h"
#include "url/origin.h"

class BrowserWindowInterface;
class GURL;
class Profile;

namespace content {
class DevToolsAgentHost;
class WebContents;
}  // namespace content

namespace dao {

bool IsAutomationUrlEligible(const GURL& url);

class DaoBrowserAutomationSession {
 public:
  struct DevToolsState {
    DevToolsState();
    ~DevToolsState();

    DevToolsState(const DevToolsState&) = delete;
    DevToolsState& operator=(const DevToolsState&) = delete;

    bool network_tracking_enabled = false;
    bool network_tracking_requested = false;
    uint64_t network_enable_attempt_epoch = 0;
    std::set<uint64_t> network_pending_enable_attempts;
    std::vector<base::DictValue> network_requests;
    size_t network_request_bytes = 0;
    size_t network_requests_dropped = 0;
    size_t network_fields_truncated = 0;
    std::vector<base::DictValue> staged_network_requests;
    size_t staged_network_request_bytes = 0;
    size_t staged_network_requests_dropped = 0;
    size_t staged_network_fields_truncated = 0;
    bool console_tracking_enabled = false;
    bool console_tracking_requested = false;
    uint64_t console_enable_attempt_epoch = 0;
    std::set<uint64_t> console_pending_enable_attempts;
    std::vector<base::DictValue> console_messages;
    size_t console_message_bytes = 0;
    size_t console_messages_dropped = 0;
    size_t console_fields_truncated = 0;
    std::vector<base::DictValue> staged_console_messages;
    size_t staged_console_message_bytes = 0;
    size_t staged_console_messages_dropped = 0;
    size_t staged_console_fields_truncated = 0;

    base::WeakPtr<content::WebContents> bound_target;
    scoped_refptr<content::DevToolsAgentHost> bound_host;
    url::Origin bound_origin;
    int64_t bound_document_sequence_number = -1;
    uint64_t binding_generation = 0;
  };

  using TargetChangedCallback =
      base::RepeatingCallback<void(DaoBrowserAutomationSession*)>;
  using TargetChangedObserver =
      base::RepeatingCallback<void(DaoBrowserAutomationSession*)>;

  // Sessions must be created, resolved, and destroyed on the browser UI
  // sequence.
  DaoBrowserAutomationSession(BrowserWindowInterface* browser_window,
                              content::WebContents* target);
  ~DaoBrowserAutomationSession();

  DaoBrowserAutomationSession(const DaoBrowserAutomationSession&) = delete;
  DaoBrowserAutomationSession& operator=(const DaoBrowserAutomationSession&) =
      delete;

  BrowserWindowInterface* browser_window() const;
  Profile* profile() const;
  void SetTarget(content::WebContents* target);
  bool RefreshTargetDocumentSnapshot(content::WebContents* target);
  base::expected<content::WebContents*, DaoToolError> ResolveTarget() const;
  base::expected<content::WebContents*, DaoToolError> ResolveEligibleTarget()
      const;
  base::WeakPtr<DaoBrowserAutomationSession> GetWeakPtr();
  void SetTargetChangedCallback(TargetChangedCallback callback);
  base::CallbackListSubscription AddTargetChangedObserver(
      TargetChangedObserver observer);
  DevToolsState& devtools_state();
  const DevToolsState& devtools_state() const;

  const std::string& expected_domain() const;
  void set_expected_domain(std::string expected_domain);
  const url::Origin& committed_origin() const;
  int64_t document_sequence_number() const;

 private:
  void CaptureAuthorizationSnapshot(content::WebContents* target) const;
  void NotifyTargetChanged() const;
  void ClearResolvedTarget() const;

  base::WeakPtr<BrowserWindowInterface> browser_window_;
  base::WeakPtr<Profile> profile_;
  tabs::TabHandle target_handle_;
  mutable base::WeakPtr<content::WebContents> resolved_contents_;
  std::string expected_domain_;
  mutable url::Origin committed_origin_;
  mutable int64_t document_sequence_number_ = -1;
  mutable TargetChangedCallback target_changed_callback_;
  mutable base::RepeatingCallbackList<void(DaoBrowserAutomationSession*)>
      target_changed_observers_;
  DevToolsState devtools_state_;
  SEQUENCE_CHECKER(sequence_checker_);
  mutable base::WeakPtrFactory<DaoBrowserAutomationSession> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_BROWSER_AUTOMATION_SESSION_H_
