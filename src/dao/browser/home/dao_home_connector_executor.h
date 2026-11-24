// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_CONNECTOR_EXECUTOR_H_
#define DAO_BROWSER_HOME_DAO_HOME_CONNECTOR_EXECUTOR_H_

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/common/chrome_render_frame.mojom.h"
#include "content/public/browser/frame_tree_node_id.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/home/dao_home_types.h"
#include "dao/common/home_storage_reads.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "url/gurl.h"

class Profile;
namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace dao {

class DaoHomeProjectService;

// Runs one generated Home connector against a detached regular-Profile page.
// Generated connector JavaScript remains in the no-bindings connector frame;
// this class executes only fixed Dao-authored DOM adapter operations.
class DaoHomeConnectorExecutor : public content::WebContentsObserver {
 public:
  using Callback = base::OnceCallback<void(base::Value)>;

  explicit DaoHomeConnectorExecutor(DaoHomeProjectService* service = nullptr);
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
  // Rechecks the retained document before delivery. Inspect TerminalErrorFor
  // on reply and treat unknown storage reads as depending on every key.
  void ValidateDelivery(base::OnceCallback<void(HomeStorageReads)> callback);
  void ResolveMedia(const std::string& handle, Callback callback);
  void ReleaseSource();
  void Cancel();
  void FailPendingCallForSessionChange();

  const base::Value* TerminalErrorFor(const std::string& execution_id) const;
  bool running() const;
  bool collection_finished() const { return collection_finished_; }
  bool OwnsExecution(const std::string& execution_id) const {
    return !execution_id.empty() && (execution_id == execution_id_ ||
                                     execution_id == failed_execution_id_);
  }
  bool OwnsMediaHandle(const std::string& handle) const;
  size_t retained_media_bytes() const { return retained_media_bytes_; }
  bool has_media() const { return !media_handles_.empty(); }
  size_t retained_media_blob_count_for_testing() const {
    return resolved_media_.size();
  }
  const std::string& revision() const { return revision_; }
  const std::string& connector_id() const { return connector_.id; }

  // content::WebContentsObserver:
  void RenderFrameCreated(content::RenderFrameHost* frame) override;
  void FrameDeleted(content::FrameTreeNodeId id) override;
  void ReadyToCommitNavigation(content::NavigationHandle* handle) override;
  void DidFinishNavigation(content::NavigationHandle* handle) override;
  void OnCookiesAccessed(content::RenderFrameHost* frame,
                         const content::CookieAccessDetails& details) override;
  void OnCookiesAccessed(content::NavigationHandle* handle,
                         const content::CookieAccessDetails& details) override;
  void DOMContentLoaded(content::RenderFrameHost* frame) override;
  void DidStopLoading() override;
  void WebContentsDestroyed() override;

 private:
  class OwnerObserver;

  void ReportCookieWrites(const content::CookieAccessDetails& details);
  void ObserveStorageSource(content::NavigationHandle* handle);
  bool StorageOriginIsGranted(content::RenderFrameHost* frame) const;
  bool OwnerIsActive() const;
  bool UrlIsAllowed(const GURL& url) const;
  bool OperationIsAllowed(const std::string& operation) const;
  std::string BuildAdapterScript(const std::string& operation,
                                 const base::ListValue& arguments) const;
  void MaybeCheckDocumentReady();
  void ReplyStarted();
  void OnFrameStorageReads(content::WeakDocumentPtr document,
                           const HomeStorageReads& reads);
  void CheckAuthState();
  void OnAuthState(content::WeakDocumentPtr document, base::Value result);
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

  raw_ptr<DaoHomeProjectService> service_;
  base::WeakPtr<content::WebContents> owner_;
  raw_ptr<Profile> profile_ = nullptr;
  std::unique_ptr<OwnerObserver> owner_observer_;
  std::unique_ptr<content::WebContents> source_;
  std::string execution_id_;
  std::string failed_execution_id_;
  base::Value terminal_error_;
  std::string revision_;
  HomeConnector connector_;
  HomeLimits limits_;
  std::string module_source_;
  base::Value schema_;
  base::Value input_;
  Callback start_callback_;
  Callback page_callback_;
  Callback finish_callback_;
  base::OnceCallback<void(HomeStorageReads)> delivery_callback_;
  content::WeakDocumentPtr finished_document_;
  std::vector<mojo::AssociatedRemote<chrome::mojom::DaoHomeStorageSource>>
      delivery_storage_sources_;
  HomeStorageReads delivery_storage_reads_;
  size_t pending_storage_reads_ = 0;
  std::set<content::FrameTreeNodeId> committed_storage_frames_;
  bool storage_reads_incomplete_ = false;
  base::Value finish_result_;
  std::map<std::string, GURL, std::less<>> media_handles_;
  std::map<std::string, base::Value> resolved_media_;
  std::vector<GURL> pending_media_urls_;
  base::TimeTicks media_ready_deadline_;
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
