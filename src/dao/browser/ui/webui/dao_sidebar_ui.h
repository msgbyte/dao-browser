// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_SIDEBAR_UI_H_
#define DAO_BROWSER_UI_WEBUI_DAO_SIDEBAR_UI_H_

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/download/content/public/all_download_item_notifier.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "dao/browser/ui/webui/dao_pinned_tab_model.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "services/media_session/public/mojom/media_session.mojom.h"
#include "ui/menus/simple_menu_model.h"

class Browser;

namespace content {
class DownloadManager;
class WebContents;
}  // namespace content

namespace download {
class DownloadItem;
}  // namespace download

namespace views {
class MenuRunner;
}  // namespace views

namespace tabs {
class TabInterface;
}  // namespace tabs

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
class DaoSidebarUIHandler : public content::WebUIMessageHandler,
                            public TabStripModelObserver,
                            public download::AllDownloadItemNotifier::Observer,
                            public ui::SimpleMenuModel::Delegate,
                            public media_session::mojom::MediaSessionObserver {
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
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;

  // download::AllDownloadItemNotifier::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadUpdated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void OnDownloadRemoved(content::DownloadManager* manager,
                         download::DownloadItem* item) override;

  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;
  void MenuClosed(ui::SimpleMenuModel* source) override;

  // media_session::mojom::MediaSessionObserver:
  void MediaSessionInfoChanged(
      media_session::mojom::MediaSessionInfoPtr session_info) override {}
  void MediaSessionMetadataChanged(
      const std::optional<media_session::MediaMetadata>& metadata) override {}
  void MediaSessionActionsChanged(
      const std::vector<media_session::mojom::MediaSessionAction>& actions)
      override;
  void MediaSessionImagesChanged(
      const base::flat_map<media_session::mojom::MediaSessionImageType,
                           std::vector<media_session::MediaImage>>& images)
      override {}
  void MediaSessionPositionChanged(
      const std::optional<media_session::MediaPosition>& position) override {}

  base::ListValue GetPinnedItemsForTesting();
  base::DictValue GetSidebarStateForTesting();
  void PinTabForTesting(int index);
  void UnpinPinnedItemForTesting(const std::string& id,
                                 int target_index = -1);
  void ActivateOrOpenPinnedItemForTesting(const std::string& id);
  void ClosePinnedItemTabForTesting(const std::string& id);

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
  base::DictValue BuildSidebarState();
  base::ListValue BuildPinnedItems();
  int FindOpenPinnedTabIndexForItem(const DaoPinnedTabItem& item) const;
  void LoadPinnedItemsThenPushFullState();
  void SavePinnedItems();
  void PinTabAtIndex(int index);
  void UnpinPinnedItemById(const std::string& id, int target_index = -1);
  void ActivateOrOpenPinnedItem(const std::string& id);
  void ClosePinnedItemTab(const std::string& id);
  void MovePinnedItem(const std::string& id, int to_index);
  void PushTabUpdate(int index);
  void OnSplitStateChanged();
  void ConsolidateSplitGroupTabs();
  void PushDownloadState();
  void PushActiveDownloads();

  // Media playback widget.
  int FindAudibleTabIndex() const;
  void PushMediaPlaybackState();
  // Bind/reset the Mojo observer on the given tab's MediaSession.
  void BindMediaSessionObserver(int tab_index);
  void ResetMediaSessionObserver();
  base::ListValue BuildActiveDownloadList();

  // JS message handlers
  void HandleGetInitialState(const base::ListValue& args);
  void HandleActivateTab(const base::ListValue& args);
  void HandleCloseTab(const base::ListValue& args);
  void HandleToggleMute(const base::ListValue& args);
  void HandleMoveTab(const base::ListValue& args);
  void HandleShowCommandBarForNewTab(const base::ListValue& args);
  void HandleFileDrop(const base::ListValue& args);
  void HandleSetDropInsertIndex(const base::ListValue& args);
  void HandleRequestDownloadState(const base::ListValue& args);
  void HandleOpenDownloadsFolder(const base::ListValue& args);
  void HandleOpenRecentFile(const base::ListValue& args);
  void HandleCancelDownload(const base::ListValue& args);
  void HandleStartFileDrag(const base::ListValue& args);
  void HandleTabDragActive(const base::ListValue& args);
  void HandleMoveTabCrossWindow(const base::ListValue& args);
  void HandleDetachTabToNewWindow(const base::ListValue& args);
  void HandleLoadFolders(const base::ListValue& args);
  void HandleSaveFolders(const base::ListValue& args);
  void HandleShowTabContextMenu(const base::ListValue& args);
  void HandlePinTab(const base::ListValue& args);
  void HandleUnpinPinnedItem(const base::ListValue& args);
  void HandleActivateOrOpenPinnedItem(const base::ListValue& args);
  void HandleClosePinnedItemTab(const base::ListValue& args);
  void HandleMovePinnedItem(const base::ListValue& args);
  void HandleShowPinnedItemContextMenu(const base::ListValue& args);
  void HandleShowTabTooltip(const base::ListValue& args);
  void HandleHideTabTooltip(const base::ListValue& args);

  // Media playback control handlers.
  void HandleMediaPlayPause(const base::ListValue& args);
  void HandleMediaPrevious(const base::ListValue& args);
  void HandleMediaNext(const base::ListValue& args);
  void HandleMediaDismiss(const base::ListValue& args);
  void HandleMediaActivateTab(const base::ListValue& args);

  // Sidebar context menu handler.
  void HandleShowSidebarContextMenu(const base::ListValue& args);

  // Context menu helpers.
  int FindVisualPosition(int tab_index) const;
  void CloseTabsInVisualRange(int from, int to);
  void ClearContextMenuState();

  void OnScanResultReady(base::ListValue file_entries,
                         std::vector<base::FilePath> paths);
  static std::string FormatSpeed(int64_t bytes_per_sec);

  // Tab context menu command IDs.
  enum TabContextMenuCommand {
    kDuplicateTab = 0,
    kCopyLink,
    kToggleMute,
    kCloseTab,
    kCloseOtherTabs,
    kCloseTabsAbove,
    kCloseTabsBelow,
    kInspectSidebar,
    kPinTab,
    kPinnedOpen,
    kPinnedUnpin,
    kPinnedCloseTab,
    kPinnedCopyLink,
  };

  raw_ptr<Browser> browser_ = nullptr;

  // Media widget state.
  int media_widget_tab_index_ = -1;
  bool media_widget_dismissed_ = false;
  bool media_widget_user_paused_ = false;
  // Tab whose MediaSession is currently being observed via Mojo.
  int observed_media_tab_index_ = -1;
  // Actions registered by the currently observed MediaSession.
  bool has_prev_action_ = false;
  bool has_next_action_ = false;
  mojo::Receiver<media_session::mojom::MediaSessionObserver>
      media_session_observer_receiver_{this};
  std::unique_ptr<download::AllDownloadItemNotifier> download_notifier_;
  std::vector<base::FilePath> recent_file_paths_;
  std::string folder_json_;  // Per-window folder data (in-memory)
  DaoPinnedTabModel pinned_tab_model_;
  bool pinned_items_loaded_ = false;
  bool pinned_items_load_pending_ = false;
  bool pinned_items_auto_save_enabled_ = true;

  // Context menu state.
  int context_menu_tab_index_ = -1;
  std::string context_menu_pinned_item_id_;
  std::set<int> folder_tab_indices_;
  std::vector<int>
      visual_tab_order_;  // Tab model indices in visual order (top to bottom)
  std::unique_ptr<ui::SimpleMenuModel> tab_context_menu_model_;
  std::unique_ptr<views::MenuRunner> tab_context_menu_runner_;

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
