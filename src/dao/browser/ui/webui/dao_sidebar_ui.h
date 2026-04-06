// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_SIDEBAR_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_SIDEBAR_UI_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"

class Browser;

namespace content {
class DownloadManager;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

namespace dao {

class DaoSidebarUI;
class DaoSplitView;

// WebUI config for dao://sidebar
class DaoSidebarUIConfig : public content::WebUIConfig {
 public:
  DaoSidebarUIConfig();

  // content::WebUIConfig:
  std::unique_ptr<content::WebUIController> CreateWebUIController(
      content::WebUI* web_ui,
      const GURL& url) override;
};

// Message handler for the sidebar WebUI.
class DaoSidebarUIHandler
    : public content::WebUIMessageHandler,
      public TabStripModelObserver,
      public download::AllDownloadItemNotifier::Observer {
 public:
  DaoSidebarUIHandler();
  ~DaoSidebarUIHandler() override;

  // Set the browser this handler operates on.
  void SetBrowser(Browser* browser);

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void TabChangedAt(content::WebContents* contents,
                    int index,
                    TabChangeType change_type) override;

  // download::AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

 private:
  // Returns the split view for this browser, or nullptr.
  DaoSplitView* GetSplitView() const;

  // Whether |contents| belongs to any split group with 2+ members.
  bool IsInAnySplitGroup(content::WebContents* contents) const;

  // Move group members around |anchor|, preserving their relative order
  // as given by |group_ordered|.
  void PlaceGroupAroundAnchor(
      const std::vector<content::WebContents*>& group_ordered,
      content::WebContents* anchor);

  void PushFullState();
  void PushTabUpdate(int index);
  void OnSplitStateChanged();
  void ConsolidateSplitGroupTabs();
  void PushDownloadState();
  void PushActiveDownloads();
  base::Value::List BuildActiveDownloadList();

  // JS message handlers
  void HandleGetInitialState(const base::Value::List& args);
  void HandleActivateTab(const base::Value::List& args);
  void HandleCloseTab(const base::Value::List& args);
  void HandleToggleMute(const base::Value::List& args);
  void HandleMoveTab(const base::Value::List& args);
  void HandleShowCommandBarForNewTab(const base::Value::List& args);
  void HandleFileDrop(const base::Value::List& args);
  void HandleSetDropInsertIndex(const base::Value::List& args);
  void HandleRequestDownloadState(const base::Value::List& args);
  void HandleOpenDownloadsFolder(const base::Value::List& args);
  void HandleOpenRecentFile(const base::Value::List& args);
  void HandleCancelDownload(const base::Value::List& args);
  void HandleStartFileDrag(const base::Value::List& args);
  void HandleTabDragActive(const base::Value::List& args);
  void HandleMoveTabCrossWindow(const base::Value::List& args);
  void HandleDetachTabToNewWindow(const base::Value::List& args);
  void HandleLoadFolders(const base::Value::List& args);
  void HandleSaveFolders(const base::Value::List& args);

  void OnScanResultReady(base::Value::List file_entries,
                         std::vector<base::FilePath> paths);
  static std::string FormatSpeed(int64_t bytes_per_sec);

  raw_ptr<Browser> browser_ = nullptr;
  std::unique_ptr<download::AllDownloadItemNotifier> download_notifier_;
  std::vector<base::FilePath> recent_file_paths_;
  std::string folder_json_;  // Per-window folder data (in-memory)
  base::WeakPtrFactory<DaoSidebarUIHandler> weak_factory_{this};
};

// WebUI controller for dao://sidebar
class DaoSidebarUI : public content::WebUIController {
 public:
  explicit DaoSidebarUI(content::WebUI* web_ui);
  ~DaoSidebarUI() override;

  void SetBrowser(Browser* browser);

 private:
  raw_ptr<DaoSidebarUIHandler> handler_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_SIDEBAR_UI_H_
