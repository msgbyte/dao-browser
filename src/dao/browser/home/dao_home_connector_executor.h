// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_CONNECTOR_EXECUTOR_H_
#define DAO_BROWSER_HOME_DAO_HOME_CONNECTOR_EXECUTOR_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/home/dao_home_types.h"
#include "url/gurl.h"

class Profile;
namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace dao {

// Runs one generated Home connector against a detached regular-Profile page.
// Generated connector JavaScript remains in the no-bindings connector frame;
// this class executes only fixed Dao-authored DOM adapter operations.
class DaoHomeConnectorExecutor : public content::WebContentsObserver {
 public:
  using Callback = base::OnceCallback<void(base::Value)>;

  DaoHomeConnectorExecutor();
  ~DaoHomeConnectorExecutor() override;

  DaoHomeConnectorExecutor(const DaoHomeConnectorExecutor&) = delete;
  DaoHomeConnectorExecutor& operator=(const DaoHomeConnectorExecutor&) = delete;

  void Start(content::WebContents* owner,
             Profile* profile,
             std::string revision,
             HomeConnector connector,
             HomeLimits limits,
             std::string module_source,
             std::string schema_source,
             base::Value input,
             Callback callback);
  void CallPage(const std::string& execution_id,
                const std::string& operation,
                base::ListValue arguments,
                Callback callback);
  void Finish(const std::string& execution_id,
              base::Value result,
              Callback callback);
  void ResolveMedia(const std::string& handle, Callback callback);
  void Cancel();

  bool running() const;
  bool collection_finished() const { return collection_finished_; }
  bool OwnsExecution(const std::string& execution_id) const {
    return !execution_id.empty() && execution_id == execution_id_;
  }
  bool OwnsMediaHandle(const std::string& handle) const;
  size_t retained_media_blob_count_for_testing() const {
    return resolved_media_.size();
  }
  const std::string& revision() const { return revision_; }
  const std::string& connector_id() const { return connector_.id; }

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void DidStopLoading() override;
  void WebContentsDestroyed() override;

 private:
  class OwnerObserver;

  bool OwnerIsActive() const;
  bool UrlIsAllowed(const GURL& url) const;
  bool OperationIsAllowed(const std::string& operation) const;
  std::string BuildAdapterScript(const std::string& operation,
                                 const base::ListValue& arguments) const;
  void ReplyStarted();
  void CheckAuthState();
  void OnAuthState(base::Value result);
  void OnPageResult(base::Value result);
  void PollWaitFor();
  void OnWaitForResult(base::Value result);
  void ReplaceMediaReferences(base::Value& value, int depth = 0);
  void SnapshotNextMedia();
  std::string BuildMediaSnapshotScript(const GURL& url,
                                       size_t max_pixels) const;
  void OnMediaSnapshotResult(std::string url, base::Value result);
  void CompleteFinish();
  void Fail(std::string code, std::string message, Callback callback);
  void FailStart(std::string code, std::string message);
  void Reset();

  base::WeakPtr<content::WebContents> owner_;
  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<OwnerObserver> owner_observer_;
  std::unique_ptr<content::WebContents> source_;
  std::string execution_id_;
  std::string revision_;
  HomeConnector connector_;
  HomeLimits limits_;
  std::string module_source_;
  base::Value schema_;
  base::Value input_;
  Callback start_callback_;
  Callback page_callback_;
  Callback finish_callback_;
  base::Value finish_result_;
  std::map<std::string, GURL> media_handles_;
  std::map<std::string, base::Value> resolved_media_;
  std::vector<GURL> pending_media_urls_;
  size_t next_media_snapshot_ = 0;
  size_t retained_media_bytes_ = 0;
  int operation_count_ = 0;
  int scroll_count_ = 0;
  bool committed_allowed_document_ = false;
  bool navigation_pending_ = false;
  bool auth_check_pending_ = false;
  bool collection_finished_ = false;
  std::string wait_for_selector_;
  base::TimeTicks wait_for_deadline_;
  base::OneShotTimer wait_for_timer_;
  base::OneShotTimer timeout_;
  base::WeakPtrFactory<DaoHomeConnectorExecutor> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_CONNECTOR_EXECUTOR_H_
