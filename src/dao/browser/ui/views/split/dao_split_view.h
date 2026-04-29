// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_VIEW_H_

#include <memory>
#include <vector>

#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "dao/browser/ui/views/split/dao_split_node.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace content {
class WebContents;
}

namespace dao {

class DaoAddressBarView;
class DaoSplitDividerView;
class DaoSplitNode;
class DaoSplitBranchNode;
class DaoSplitLeafNode;
class DaoSplitPaneView;

// Container view that manages the binary split tree of web content panes.
// Lives as a child of contents_container_ inside BrowserView.
//
// When only one pane exists (the root leaf), DaoSplitView is hidden and
// Chromium's normal ContentsWebView is used.  When split mode is active
// (>1 leaf), DaoSplitView becomes visible and the original ContentsWebView
// is zero-sized.
class DaoSplitView : public views::View,
                      public TabStripModelObserver {
  METADATA_HEADER(DaoSplitView, views::View)

 public:
  struct SplitGroupSummary {
    SplitGroupSummary();
    ~SplitGroupSummary();
    SplitGroupSummary(const SplitGroupSummary&);
    SplitGroupSummary& operator=(const SplitGroupSummary&);
    SplitGroupSummary(SplitGroupSummary&&);
    SplitGroupSummary& operator=(SplitGroupSummary&&);

    std::vector<content::WebContents*> contents;
    raw_ptr<content::WebContents> representative = nullptr;
    bool is_active = false;
  };

  explicit DaoSplitView(Browser* browser);
  DaoSplitView(const DaoSplitView&) = delete;
  DaoSplitView& operator=(const DaoSplitView&) = delete;
  ~DaoSplitView() override;

  // Whether split mode is active (>1 leaf in tree).
  bool IsSplitActive() const;

  // Number of visible panes.
  int PaneCount() const;

  // Whether |web_contents| participates in the stored split layout.
  bool ContainsWebContents(content::WebContents* web_contents) const;

  // The tabs currently participating in the stored split layout.
  std::vector<content::WebContents*> GetSplitContents() const;

  // Summaries for all split groups in sidebar display order.
  std::vector<SplitGroupSummary> GetSplitGroupSummaries() const;

  // Whether |web_contents| belongs to the currently displayed split group.
  bool IsActiveSplitTab(content::WebContents* web_contents) const;

  // Split the pane displaying |existing_contents| in |direction|, adding
  // |new_contents| as the new sibling.  Returns false if max depth reached.
  bool SplitPane(content::WebContents* existing_contents,
                 SplitDirection direction,
                 bool new_contents_first,
                 content::WebContents* new_contents);

  // Close the pane displaying |web_contents|.  If it's the last pane,
  // returns false (no-op).
  bool ClosePane(content::WebContents* web_contents);

  // Exit split mode keeping |keep_contents| as the surviving tab.
  // Activates that tab and closes all other panes.
  void UnsplitKeepingPane(content::WebContents* keep_contents);

  // Set the active (focused) pane.  Updates TabStripModel active tab
  // and re-calls WasShown() on all visible panes.
  void SetActivePane(DaoSplitPaneView* pane);

  // Start/update/finish pane rearranging from the pane header drag handle.
  void BeginPaneRearrange(content::WebContents* source_contents);
  void UpdatePaneRearrange(const gfx::Point& point_in_view);
  void EndPaneRearrange(const gfx::Point& point_in_view);

  // Pop the current pane into a standalone browser window.
  bool PopOutPane(content::WebContents* web_contents);

  // Called by divider during drag to relayout.
  void OnDividerDragged();
  void OnDividerDragFinished();

  // The address bar of the currently focused pane (for bubble anchoring).
  DaoAddressBarView* focused_pane_address_bar() const;

  // Swap content in a pane (drop on center, not edge).
  void SwapPaneContent(DaoSplitLeafNode* leaf,
                       content::WebContents* new_contents);

  // Ensure all visible panes have WasShown() called.
  void EnsureAllPanesShown();

  // Persistence.
  void SaveLayout();
  void RestoreLayout();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // Called by the tab list when a tab drag starts/ends.  While active,
  // this view becomes hit-testable so it can receive drop events.
  void SetTabDragActive(bool active);

  // Native bypass entry points for the macOS DaoEventInterceptor. In v147
  // Chromium's BridgedContentView rejects renderer-initiated drags before
  // they reach Views-level CanDrop/OnDrop, so the interceptor has to drive
  // our drag pipeline itself. |location_in_view| is in this view's coords.
  // UpdateNativeDropIndicator also auto-enables tab_drag_active_ so the
  // view is hit-testable even if SetTabDragActive hasn't run yet.
  void UpdateNativeDropIndicator(const gfx::Point& location_in_view);
  void HideNativeDropIndicator();
  // Accepts a "dao-tab-drag:<sid>:<idx>" payload at |location_in_view|.
  // Handles split creation, cross-window move, and center swap/activate.
  // Returns true if the drop was applied.
  bool ProcessNativeTabDrop(const gfx::Point& location_in_view,
                            const std::string& payload);

  // Callback invoked when split state changes (split created/destroyed).
  void set_split_state_changed_callback(base::RepeatingClosure callback) {
    split_state_changed_callback_ = std::move(callback);
  }

  // views::View:
  void Layout(PassKey) override;
  bool GetCanProcessEventsWithinSubtree() const override;
  bool GetDropFormats(int* formats,
                      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  views::View::DropCallback GetDropCallback(
      const ui::DropTargetEvent& event) override;

 private:
  struct SplitGroup {
    SplitGroup();
    ~SplitGroup();

    std::unique_ptr<DaoSplitNode> root;
  };

  // Keep the single root leaf bound to the currently active tab before the
  // first split happens.
  void SyncSinglePaneRootWithActiveTab();
  void SyncActiveGroupWithActiveTab();
  void SyncActivePaneWithActiveTab();
  bool HasSplitLayout() const;
  void UpdatePaneVisibility();
  bool MovePane(content::WebContents* source_contents,
                content::WebContents* target_contents,
                SplitDirection direction,
                bool source_first);
  SplitGroup* FindGroupForContents(content::WebContents* web_contents);
  const SplitGroup* FindGroupForContents(
      content::WebContents* web_contents) const;
  std::vector<content::WebContents*> GetGroupContents(
      const SplitGroup* group) const;
  content::WebContents* GetRepresentativeContents(
      const SplitGroup* group) const;
  bool RemoveContentsFromGroup(SplitGroup* group,
                               content::WebContents* web_contents);
  void RemoveContentsFromOtherGroups(content::WebContents* web_contents,
                                     SplitGroup* excluded_group);
  void RemoveEmptyOrCollapsedGroups();
  DaoSplitNode* GetLayoutRoot() const;
  void DetachPrimaryContentsHost(content::WebContents* first_contents,
                                 content::WebContents* second_contents);
  void PerformDeferredSplit(content::WebContents* existing_contents,
                            SplitDirection direction,
                            bool new_contents_first,
                            content::WebContents* new_contents);
  void PerformDeferredSwap(content::WebContents* target_contents,
                           content::WebContents* new_contents);

  // Rebuild the entire view hierarchy from the tree structure.
  void RebuildViews();

  // Recursively create views for a subtree.
  void BuildViewsForNode(DaoSplitNode* node);

  // Find the leaf node whose bounds contain |point| (in DaoSplitView coords).
  DaoSplitLeafNode* FindLeafAtPoint(const gfx::Point& point);

  // Determine which edge zone the point is in within a leaf's bounds.
  // Returns the direction for splitting, or nullopt for center.
  std::optional<SplitDirection> DetectDropZone(
      DaoSplitLeafNode* leaf,
      const gfx::Point& point);

  // Update divider view positions from branch node bounds.
  void UpdateDividerPositions();

  // Animate pane bounds from |start| to |end| over the given duration.
  void AnimatePaneBounds(DaoSplitPaneView* pane,
                         const gfx::Rect& start,
                         const gfx::Rect& end,
                         base::TimeDelta duration);

  // Animate divider bounds similarly.
  void AnimateDividerBounds(DaoSplitDividerView* divider,
                            const gfx::Rect& start,
                            const gfx::Rect& end,
                            base::TimeDelta duration);

  // Block/unblock native events on all visible pane web contents (drag support).
  void BlockAllNativeEvents(bool block);

  // Periodically check cursor position to show/hide pane headers on hover.
  void UpdateHoverTracking();
  void CheckCursorOverPanes();

  // Re-attach a WebContents to the primary ContentsWebView after split
  // deactivation.  Called via PostTask to give the renderer time to process
  // WasHidden and discard stale paint caches.
  void ReattachWebContentsToPrimary(content::WebContents* web_contents);

  // Handle the actual drop.
  void OnDrop(const ui::DropTargetEvent& event,
              ui::mojom::DragOperation& output_drag_op,
              std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner);

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
  std::unique_ptr<DaoSplitNode> single_root_;
  std::vector<std::unique_ptr<SplitGroup>> split_groups_;
  raw_ptr<SplitGroup> active_group_ = nullptr;

  // All pane views currently in the tree (not owned, children of this view).
  std::vector<raw_ptr<DaoSplitPaneView>> pane_views_;
  // All divider views currently in the tree.
  std::vector<raw_ptr<DaoSplitDividerView>> divider_views_;

  // The active (focused) pane.
  raw_ptr<DaoSplitPaneView> active_pane_ = nullptr;

  // Drop zone tracking.
  raw_ptr<DaoSplitLeafNode> drop_target_leaf_ = nullptr;
  std::optional<SplitDirection> drop_zone_direction_;
  raw_ptr<content::WebContents> rearrange_source_contents_ = nullptr;

  // Drop zone overlay view (shown during drag).
  raw_ptr<views::View> drop_overlay_ = nullptr;

  // True while a tab is being dragged from the sidebar.
  bool tab_drag_active_ = false;

  // Cursor hover tracking for pane headers.
  base::RepeatingTimer hover_timer_;

  base::RepeatingClosure split_state_changed_callback_;

  base::WeakPtrFactory<DaoSplitView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_VIEW_H_
