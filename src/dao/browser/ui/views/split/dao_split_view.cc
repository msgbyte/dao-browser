// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/split/dao_split_view.h"

#include <algorithm>
#include <map>
#include <string>

#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/sidebar/dao_tab_list_view.h"
#include "dao/browser/ui/views/split/dao_split_divider_view.h"
#include "dao/browser/ui/views/split/dao_split_node.h"
#include "dao/browser/ui/views/split/dao_split_pane_view.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/canvas.h"

namespace dao {

namespace {

constexpr auto kDeferredDropActionDelay = base::Milliseconds(25);

void RefreshSplitLayout(Browser* browser, views::View* view) {
  if (view->parent()) {
    view->parent()->InvalidateLayout();
  }
  view->InvalidateLayout();
  view->SchedulePaint();

  if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser)) {
    if (auto* sidebar = browser_view->dao_sidebar()) {
      if (auto* tab_list_view = sidebar->tab_list_view()) {
        tab_list_view->RefreshForSplitStateChange();
      }
    }
    browser_view->InvalidateLayout();
    browser_view->SchedulePaint();
  }
}

void CollectLeafContents(DaoSplitNode* node,
                         std::vector<content::WebContents*>* contents) {
  if (!node)
    return;
  if (node->IsLeaf()) {
    if (auto* web_contents = node->AsLeaf()->web_contents()) {
      contents->push_back(web_contents);
    }
    return;
  }
  if (node->IsBranch()) {
    DaoSplitBranchNode* branch = node->AsBranch();
    CollectLeafContents(branch->first(), contents);
    CollectLeafContents(branch->second(), contents);
  }
}

// A simple overlay view drawn over the drop zone during drag.
class DropZoneOverlayView : public views::View {
  METADATA_HEADER(DropZoneOverlayView, views::View)
 public:
  DropZoneOverlayView() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetVisible(false);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(GetLocalBounds(), kDropZoneOverlay);
  }
};

BEGIN_METADATA(DropZoneOverlayView)
END_METADATA

}  // namespace

BEGIN_METADATA(DaoSplitView)
END_METADATA

DaoSplitView::SplitGroupSummary::SplitGroupSummary() = default;
DaoSplitView::SplitGroupSummary::~SplitGroupSummary() = default;
DaoSplitView::SplitGroupSummary::SplitGroupSummary(
    const SplitGroupSummary&) = default;
DaoSplitView::SplitGroupSummary& DaoSplitView::SplitGroupSummary::operator=(
    const SplitGroupSummary&) = default;
DaoSplitView::SplitGroupSummary::SplitGroupSummary(SplitGroupSummary&&) = default;
DaoSplitView::SplitGroupSummary& DaoSplitView::SplitGroupSummary::operator=(
    SplitGroupSummary&&) = default;

DaoSplitView::SplitGroup::SplitGroup() = default;
DaoSplitView::SplitGroup::~SplitGroup() = default;

DaoSplitView::DaoSplitView(Browser* browser)
    : browser_(browser),
      tab_strip_model_(browser->tab_strip_model()) {
  tab_strip_model_->AddObserver(this);

  // Create the drop zone overlay (always present, usually hidden).
  drop_overlay_ = AddChildView(std::make_unique<DropZoneOverlayView>());

  // Start with a single leaf for the active tab.
  content::WebContents* active = tab_strip_model_->GetActiveWebContents();
  single_root_ = std::make_unique<DaoSplitLeafNode>(active);
  SyncSinglePaneRootWithActiveTab();

  // Always visible so we can intercept tab drag-drops.  When not in split
  // mode, HitTestRect returns false except during active tab drags, so
  // regular mouse events pass through to the ContentsWebView below.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
}

DaoSplitView::~DaoSplitView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoSplitView::SetTabDragActive(bool active) {
  if (active) {
    SyncSinglePaneRootWithActiveTab();
  }

  if (tab_drag_active_ == active)
    return;
  tab_drag_active_ = active;

  // Block/unblock native NSViews so drag events reach DaoSplitView instead of
  // being swallowed by underlying web contents.
  if (IsSplitActive()) {
    BlockAllNativeEvents(active);
  } else if (tab_strip_model_) {
    auto* wc = tab_strip_model_->GetActiveWebContents();
    if (wc) {
      if (active) {
        BlockWebContentNativeEvents(wc);
      } else {
        UnblockWebContentNativeEvents(wc);
      }
    }
  }

  if (!active) {
    // Clean up drop state when drag ends.
    drop_target_leaf_ = nullptr;
    drop_zone_direction_.reset();
    if (drop_overlay_)
      drop_overlay_->SetVisible(false);
  }
}

bool DaoSplitView::GetCanProcessEventsWithinSubtree() const {
  // When split is active, this view is the primary content view —
  // handle all events.
  if (IsSplitActive())
    return true;

  // In single-pane mode, only intercept events while a tab is being
  // dragged from the sidebar.  Otherwise let events pass through to
  // the ContentsWebView below.
  if (tab_drag_active_)
    return true;

  return false;
}

bool DaoSplitView::IsSplitActive() const {
  return active_group_ && active_group_->root &&
         active_group_->root->CountLeaves() > 1;
}

int DaoSplitView::PaneCount() const {
  return active_group_ && active_group_->root ? active_group_->root->CountLeaves()
                                              : 0;
}

bool DaoSplitView::ContainsWebContents(content::WebContents* web_contents) const {
  return FindGroupForContents(web_contents) != nullptr;
}

std::vector<content::WebContents*> DaoSplitView::GetSplitContents() const {
  return GetGroupContents(active_group_);
}

std::vector<DaoSplitView::SplitGroupSummary>
DaoSplitView::GetSplitGroupSummaries() const {
  std::vector<SplitGroupSummary> summaries;
  summaries.reserve(split_groups_.size());

  for (const auto& group : split_groups_) {
    SplitGroupSummary summary;
    summary.contents = GetGroupContents(group.get());
    if (summary.contents.size() < 2) {
      continue;
    }
    summary.representative = GetRepresentativeContents(group.get());
    summary.is_active = group.get() == active_group_;
    summaries.push_back(std::move(summary));
  }

  return summaries;
}

bool DaoSplitView::IsActiveSplitTab(content::WebContents* web_contents) const {
  return active_group_ && web_contents && active_group_->root &&
         active_group_->root->FindLeaf(web_contents);
}

bool DaoSplitView::SplitPane(content::WebContents* existing_contents,
                             SplitDirection direction,
                             bool new_contents_first,
                             content::WebContents* new_contents) {
  if (!existing_contents || !new_contents || existing_contents == new_contents) {
    return false;
  }

  SplitGroup* target_group = FindGroupForContents(existing_contents);
  SplitGroup* source_group = FindGroupForContents(new_contents);
  if (target_group && source_group == target_group) {
    return false;
  }

  RemoveContentsFromOtherGroups(new_contents, target_group);

  if (!target_group) {
    auto group = std::make_unique<SplitGroup>();
    group->root = std::make_unique<DaoSplitLeafNode>(existing_contents);
    DaoSplitLeafNode* leaf = group->root->AsLeaf();
    auto result = dao::SplitLeaf(group->root, leaf, direction, new_contents_first,
                                 new_contents);
    static_cast<void>(result);
    if (!group->root || group->root->CountLeaves() <= 1) {
      return false;
    }
    split_groups_.push_back(std::move(group));
  } else {
    auto* leaf = target_group->root ? target_group->root->FindLeaf(existing_contents)
                                    : nullptr;
    if (!leaf) {
      return false;
    }
    auto result =
        dao::SplitLeaf(target_group->root, leaf, direction, new_contents_first,
                       new_contents);
    static_cast<void>(result);
    if (!target_group->root || target_group->root->CountLeaves() <= 1) {
      return false;
    }
  }

  DetachPrimaryContentsHost(existing_contents, new_contents);
  SyncActiveGroupWithActiveTab();
  RebuildViews();
  SaveLayout();
  EnsureAllPanesShown();
  SyncActivePaneWithActiveTab();
  RefreshSplitLayout(browser_, this);
  return true;
}

bool DaoSplitView::ClosePane(content::WebContents* web_contents) {
  SplitGroup* group = FindGroupForContents(web_contents);
  if (!group || !group->root)
    return false;

  auto* leaf = group->root->FindLeaf(web_contents);
  if (!leaf)
    return false;

  // Last pane protection.
  if (group->root->CountLeaves() <= 1)
    return false;

  dao::CloseLeaf(group->root, leaf);
  RemoveEmptyOrCollapsedGroups();

  SyncActiveGroupWithActiveTab();
  RebuildViews();
  SaveLayout();
  SyncActivePaneWithActiveTab();
  RefreshSplitLayout(browser_, this);
  return true;
}

void DaoSplitView::SetActivePane(DaoSplitPaneView* pane) {
  if (active_pane_ == pane)
    return;

  if (active_pane_)
    active_pane_->SetActive(false);

  active_pane_ = pane;

  if (active_pane_) {
    active_pane_->SetActive(true);

    // Update TabStripModel active index.
    content::WebContents* wc = active_pane_->web_contents();
    if (wc && tab_strip_model_) {
      int index = tab_strip_model_->GetIndexOfWebContents(wc);
      if (index != TabStripModel::kNoTab &&
          index != tab_strip_model_->active_index()) {
        tab_strip_model_->ActivateTabAt(index);
      }
    }

    // Re-call WasShown() on all visible panes to prevent render throttling.
    EnsureAllPanesShown();
  }
}

void DaoSplitView::OnDividerDragged() {
  // Relayout from current tree ratios.
  if (active_group_ && active_group_->root) {
    active_group_->root->Layout(GetLocalBounds());
    // Update view positions from node bounds.
    for (DaoSplitPaneView* pane : pane_views_) {
      if (pane && pane->web_contents()) {
        DaoSplitLeafNode* leaf =
            active_group_->root->FindLeaf(pane->web_contents());
        if (leaf) {
          pane->SetBoundsRect(leaf->bounds());
        }
      }
    }
    // Update divider positions.
    UpdateDividerPositions();
  }
}

void DaoSplitView::OnDividerDragFinished() {
  SaveLayout();
}

DaoAddressBarView* DaoSplitView::focused_pane_address_bar() const {
  if (active_pane_)
    return active_pane_->address_bar();
  return nullptr;
}

void DaoSplitView::SwapPaneContent(DaoSplitLeafNode* leaf,
                                   content::WebContents* new_contents) {
  if (!leaf || !new_contents)
    return;
  if (leaf->web_contents() == new_contents) {
    return;
  }
  SplitGroup* target_group = active_group_;
  if (!target_group) {
    return;
  }
  SplitGroup* source_group = FindGroupForContents(new_contents);
  if (source_group == target_group) {
    return;
  }

  RemoveContentsFromOtherGroups(new_contents, target_group);
  leaf->set_web_contents(new_contents);
  if (tab_strip_model_) {
    int index = tab_strip_model_->GetIndexOfWebContents(new_contents);
    if (index != TabStripModel::kNoTab &&
        index != tab_strip_model_->active_index()) {
      tab_strip_model_->ActivateTabAt(index);
    }
  }
  SyncActiveGroupWithActiveTab();
  RebuildViews();
  EnsureAllPanesShown();
  SyncActivePaneWithActiveTab();
  RefreshSplitLayout(browser_, this);
}

void DaoSplitView::EnsureAllPanesShown() {
  if (!IsSplitActive()) {
    return;
  }
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane) {
      pane->SetContentsVisible(true);
    }
  }
}

void DaoSplitView::SaveLayout() {
  // TODO: Persist tree to profile prefs under "dao.split_layout".
}

void DaoSplitView::RestoreLayout() {
  // TODO: Read from profile prefs and rebuild tree.
}

// --- TabStripModelObserver ---------------------------------------------------

void DaoSplitView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      if (ContainsWebContents(removed.contents)) {
        ClosePane(removed.contents);
        break;
      }
    }
  }

  SyncSinglePaneRootWithActiveTab();

  if (selection.active_tab_changed()) {
    SplitGroup* previous_group = active_group_;
    SyncActiveGroupWithActiveTab();
    if (previous_group != active_group_) {
      RebuildViews();
    }
    SyncActivePaneWithActiveTab();
    RefreshSplitLayout(browser_, this);
  }
}

// --- Layout ------------------------------------------------------------------

void DaoSplitView::Layout(PassKey) {
  DaoSplitNode* root = GetLayoutRoot();
  if (!root)
    return;

  // Always layout the tree so that leaf bounds are current even in
  // single-pane mode.  This is necessary for drag-drop hit-testing
  // (FindLeafAtPoint) to work before the first split occurs.
  root->Layout(GetLocalBounds());
  UpdatePaneVisibility();

  if (!IsSplitActive())
    return;

  // Position pane views according to leaf bounds.
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      DaoSplitLeafNode* leaf = active_group_->root->FindLeaf(pane->web_contents());
      if (leaf) {
        pane->SetBoundsRect(leaf->bounds());
      }
    }
  }

  // Position dividers.
  UpdateDividerPositions();
}

// --- Drop target -------------------------------------------------------------

bool DaoSplitView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::STRING;
  return true;
}

bool DaoSplitView::CanDrop(const ui::OSExchangeData& data) {
  auto text = data.GetString();
  return text.has_value() &&
         text->starts_with(u"dao-tab-drag:");
}

void DaoSplitView::OnDragEntered(const ui::DropTargetEvent& event) {
  // Prime the target state immediately so the overlay appears as soon as the
  // cursor enters the content area instead of waiting for a follow-up update.
  OnDragUpdated(event);
}

int DaoSplitView::OnDragUpdated(const ui::DropTargetEvent& event) {
  gfx::Point point = event.location();
  DaoSplitLeafNode* leaf = FindLeafAtPoint(point);

  if (!leaf) {
    drop_target_leaf_ = nullptr;
    drop_zone_direction_.reset();
    drop_overlay_->SetVisible(false);
    return ui::DragDropTypes::DRAG_NONE;
  }

  drop_target_leaf_ = leaf;
  drop_zone_direction_ = DetectDropZone(leaf, point);

  // Show overlay on the target half (or hide for center).
  if (drop_zone_direction_.has_value()) {
    gfx::Rect overlay_bounds = leaf->bounds();
    switch (drop_zone_direction_.value()) {
      case SplitDirection::kHorizontal:
        // Right half if point is on right side, left half otherwise.
        if (point.x() > leaf->bounds().CenterPoint().x()) {
          overlay_bounds.set_x(overlay_bounds.CenterPoint().x());
          overlay_bounds.set_width(overlay_bounds.width() / 2);
        } else {
          overlay_bounds.set_width(overlay_bounds.width() / 2);
        }
        break;
      case SplitDirection::kVertical:
        if (point.y() > leaf->bounds().CenterPoint().y()) {
          overlay_bounds.set_y(overlay_bounds.CenterPoint().y());
          overlay_bounds.set_height(overlay_bounds.height() / 2);
        } else {
          overlay_bounds.set_height(overlay_bounds.height() / 2);
        }
        break;
    }
    drop_overlay_->SetBoundsRect(overlay_bounds);
    drop_overlay_->SetVisible(true);
  } else {
    drop_overlay_->SetVisible(false);
  }

  return ui::DragDropTypes::DRAG_MOVE;
}

void DaoSplitView::OnDragExited() {
  drop_target_leaf_ = nullptr;
  drop_zone_direction_.reset();
  drop_overlay_->SetVisible(false);
  BlockAllNativeEvents(false);
  SetTabDragActive(false);
}

views::View::DropCallback DaoSplitView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  return base::BindOnce(&DaoSplitView::OnDrop, base::Unretained(this));
}

void DaoSplitView::OnDrop(
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  drop_overlay_->SetVisible(false);
  BlockAllNativeEvents(false);

  DaoSplitLeafNode* target_leaf = FindLeafAtPoint(event.location());
  std::optional<SplitDirection> target_zone =
      target_leaf ? DetectDropZone(target_leaf, event.location()) : std::nullopt;

  if (!target_leaf || !tab_strip_model_) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  // Parse tab index from drag string.
  auto text = event.data().GetString();
  if (!text.has_value() || !text->starts_with(u"dao-tab-drag:")) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  std::u16string index_str = text->substr(13);  // len("dao-tab-drag:") = 13
  int tab_index = 0;
  if (!base::StringToInt(index_str, &tab_index)) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  content::WebContents* dragged_contents =
      tab_strip_model_->GetWebContentsAt(tab_index);
  if (!dragged_contents) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  if (target_zone.has_value()) {
    // Split the target pane.
    content::WebContents* existing = target_leaf->web_contents();
    if (!existing && !IsSplitActive() && tab_strip_model_) {
      existing = tab_strip_model_->GetActiveWebContents();
      target_leaf->set_web_contents(existing);
    }
    if (existing && existing != dragged_contents) {
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&DaoSplitView::PerformDeferredSplit,
                         weak_factory_.GetWeakPtr(), existing,
                         target_zone.value(),
                         target_zone.value() == SplitDirection::kHorizontal
                             ? event.location().x() <=
                                   target_leaf->bounds().CenterPoint().x()
                             : event.location().y() <=
                                   target_leaf->bounds().CenterPoint().y(),
                         dragged_contents),
          kDeferredDropActionDelay);
    }
  } else {
    // Center drop — just activate the dragged tab in the model.
    // In single-pane mode this simply switches the active tab.
    // In split mode it swaps the pane's content.
    if (IsSplitActive()) {
      if (content::WebContents* target_contents = target_leaf->web_contents();
          target_contents && target_contents != dragged_contents) {
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
            FROM_HERE,
            base::BindOnce(&DaoSplitView::PerformDeferredSwap,
                           weak_factory_.GetWeakPtr(), target_contents,
                           dragged_contents),
            kDeferredDropActionDelay);
      }
    } else {
      // Single-pane: update tree leaf + activate tab, no view rebuild.
      target_leaf->set_web_contents(dragged_contents);
      tab_strip_model_->ActivateTabAt(tab_index);
    }
  }

  output_drag_op = ui::mojom::DragOperation::kMove;
  drop_target_leaf_ = nullptr;
  drop_zone_direction_.reset();
  SetTabDragActive(false);
}

// --- Private -----------------------------------------------------------------

bool DaoSplitView::HasSplitLayout() const {
  return IsSplitActive();
}

void DaoSplitView::SyncSinglePaneRootWithActiveTab() {
  if (!single_root_ || !tab_strip_model_ || !single_root_->IsLeaf()) {
    return;
  }

  content::WebContents* active = tab_strip_model_->GetActiveWebContents();
  DaoSplitLeafNode* leaf = single_root_->AsLeaf();
  if (leaf->web_contents() != active) {
    leaf->set_web_contents(active);
  }
}

void DaoSplitView::SyncActiveGroupWithActiveTab() {
  content::WebContents* active =
      tab_strip_model_ ? tab_strip_model_->GetActiveWebContents() : nullptr;
  active_group_ = FindGroupForContents(active);
}

void DaoSplitView::SyncActivePaneWithActiveTab() {
  content::WebContents* active =
      tab_strip_model_ ? tab_strip_model_->GetActiveWebContents() : nullptr;
  if (!active || !IsSplitActive()) {
    if (active_pane_) {
      active_pane_->SetActive(false);
      active_pane_ = nullptr;
    }
    UpdatePaneVisibility();
    return;
  }

  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents() == active) {
      SetActivePane(pane);
      return;
    }
  }
  UpdatePaneVisibility();
}

void DaoSplitView::UpdatePaneVisibility() {
  const bool split_active = IsSplitActive();

  for (DaoSplitPaneView* pane : pane_views_) {
    if (!pane) {
      continue;
    }
    pane->SetVisible(split_active);
    pane->SetContentsVisible(split_active);
  }

  for (DaoSplitDividerView* divider : divider_views_) {
    if (divider) {
      divider->SetVisible(split_active);
    }
  }

  if (!split_active && drop_overlay_) {
    drop_overlay_->SetVisible(false);
  }
}

DaoSplitView::SplitGroup* DaoSplitView::FindGroupForContents(
    content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  for (const auto& group : split_groups_) {
    if (group->root && group->root->FindLeaf(web_contents)) {
      return group.get();
    }
  }
  return nullptr;
}

const DaoSplitView::SplitGroup* DaoSplitView::FindGroupForContents(
    content::WebContents* web_contents) const {
  if (!web_contents) {
    return nullptr;
  }
  for (const auto& group : split_groups_) {
    if (group->root && group->root->FindLeaf(web_contents)) {
      return group.get();
    }
  }
  return nullptr;
}

std::vector<content::WebContents*> DaoSplitView::GetGroupContents(
    const SplitGroup* group) const {
  std::vector<content::WebContents*> contents;
  if (group && group->root) {
    CollectLeafContents(group->root.get(), &contents);
  }
  return contents;
}

content::WebContents* DaoSplitView::GetRepresentativeContents(
    const SplitGroup* group) const {
  std::vector<content::WebContents*> contents = GetGroupContents(group);
  if (contents.empty()) {
    return nullptr;
  }

  if (group == active_group_ && tab_strip_model_) {
    content::WebContents* active = tab_strip_model_->GetActiveWebContents();
    if (std::find(contents.begin(), contents.end(), active) != contents.end()) {
      return active;
    }
  }

  return contents.front();
}

bool DaoSplitView::RemoveContentsFromGroup(SplitGroup* group,
                                           content::WebContents* web_contents) {
  if (!group || !group->root || !web_contents) {
    return false;
  }

  DaoSplitLeafNode* leaf = group->root->FindLeaf(web_contents);
  if (!leaf) {
    return false;
  }

  if (group->root->CountLeaves() <= 2) {
    group->root.reset();
    return true;
  }

  dao::CloseLeaf(group->root, leaf);
  return true;
}

void DaoSplitView::RemoveContentsFromOtherGroups(
    content::WebContents* web_contents,
    SplitGroup* excluded_group) {
  for (const auto& group : split_groups_) {
    if (group.get() == excluded_group) {
      continue;
    }
    if (RemoveContentsFromGroup(group.get(), web_contents)) {
      break;
    }
  }
  RemoveEmptyOrCollapsedGroups();
}

void DaoSplitView::RemoveEmptyOrCollapsedGroups() {
  SplitGroup* previous_active_group = active_group_;
  active_group_ = nullptr;
  split_groups_.erase(
      std::remove_if(split_groups_.begin(), split_groups_.end(),
                     [](const std::unique_ptr<SplitGroup>& group) {
                       return !group || !group->root ||
                              group->root->CountLeaves() < 2;
                     }),
      split_groups_.end());
  for (const auto& group : split_groups_) {
    if (group.get() == previous_active_group) {
      active_group_ = group.get();
      break;
    }
  }
}

DaoSplitNode* DaoSplitView::GetLayoutRoot() const {
  if (active_group_ && active_group_->root) {
    return active_group_->root.get();
  }
  return single_root_.get();
}

void DaoSplitView::DetachPrimaryContentsHost(
    content::WebContents* first_contents,
    content::WebContents* second_contents) {
  if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {
    if (auto* contents_web_view = browser_view->contents_web_view()) {
      content::WebContents* hosted_contents = contents_web_view->web_contents();
      if (hosted_contents == first_contents || hosted_contents == second_contents) {
        contents_web_view->SetWebContents(nullptr);
      }
    }
  }
}

void DaoSplitView::PerformDeferredSplit(content::WebContents* existing_contents,
                                        SplitDirection direction,
                                        bool new_contents_first,
                                        content::WebContents* new_contents) {
  if (!existing_contents || !new_contents) {
    return;
  }
  SplitPane(existing_contents, direction, new_contents_first, new_contents);
}

void DaoSplitView::PerformDeferredSwap(content::WebContents* target_contents,
                                       content::WebContents* new_contents) {
  if (!target_contents || !new_contents || !active_group_ ||
      !active_group_->root) {
    return;
  }
  if (DaoSplitLeafNode* leaf = active_group_->root->FindLeaf(target_contents)) {
    SwapPaneContent(leaf, new_contents);
  }
}

void DaoSplitView::RebuildViews() {
  // Clear active pane pointer — it will be re-established from the active tab.
  active_pane_ = nullptr;

  std::map<content::WebContents*, raw_ptr<DaoSplitPaneView>> existing_panes;
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      existing_panes[pane->web_contents()] = pane;
    }
  }

  for (DaoSplitDividerView* divider : divider_views_) {
    if (!divider) {
      continue;
    }
    RemoveChildView(divider);
    delete divider;
  }
  divider_views_.clear();

  if (!active_group_ || !active_group_->root)
    return;

  std::vector<raw_ptr<DaoSplitPaneView>> rebuilt_panes;
  std::function<void(DaoSplitNode*)> rebuild_node = [&](DaoSplitNode* node) {
    if (node->IsLeaf()) {
      DaoSplitLeafNode* leaf = node->AsLeaf();
      DaoSplitPaneView* pane = nullptr;
      auto existing_it = existing_panes.find(leaf->web_contents());
      if (existing_it != existing_panes.end()) {
        pane = existing_it->second;
        existing_panes.erase(existing_it);
      } else {
        pane = AddChildView(std::make_unique<DaoSplitPaneView>(
            browser_, this, static_cast<int>(rebuilt_panes.size())));
        pane->SetWebContents(leaf->web_contents());
      }
      rebuilt_panes.push_back(pane);
      return;
    }

    if (node->IsBranch()) {
      DaoSplitBranchNode* branch = node->AsBranch();
      rebuild_node(branch->first());

      DaoSplitDividerView* divider = AddChildView(
          std::make_unique<DaoSplitDividerView>(branch, this));
      divider_views_.push_back(divider);

      rebuild_node(branch->second());
    }
  };

  rebuild_node(active_group_->root.get());
  pane_views_ = std::move(rebuilt_panes);

  for (const auto& [web_contents, pane] : existing_panes) {
    static_cast<void>(web_contents);
    if (!pane) {
      continue;
    }
    pane->SetWebContents(nullptr);
    RemoveChildView(pane);
    delete pane;
  }

  // Mark a default pane as visually active without mutating TabStripModel.
  if (!active_pane_ && !pane_views_.empty()) {
    active_pane_ = pane_views_.front();
    active_pane_->SetActive(true);
  }

  size_t z_order_index = 0;
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane) {
      ReorderChildView(pane, z_order_index++);
    }
  }
  for (DaoSplitDividerView* divider : divider_views_) {
    if (divider) {
      ReorderChildView(divider, z_order_index++);
    }
  }

  // Ensure overlay is on top.
  if (drop_overlay_) {
    ReorderChildView(drop_overlay_, children().size());
  }

  InvalidateLayout();
}

void DaoSplitView::BuildViewsForNode(DaoSplitNode* node) {
  if (node->IsLeaf()) {
    DaoSplitLeafNode* leaf = node->AsLeaf();
    DaoSplitPaneView* pane = AddChildView(
        std::make_unique<DaoSplitPaneView>(
            browser_, this, static_cast<int>(pane_views_.size())));
    pane->SetWebContents(leaf->web_contents());
    pane_views_.push_back(pane);
    return;
  }

  if (node->IsBranch()) {
    DaoSplitBranchNode* branch = node->AsBranch();
    BuildViewsForNode(branch->first());

    // Add divider between the two children.
    DaoSplitDividerView* divider = AddChildView(
        std::make_unique<DaoSplitDividerView>(branch, this));
    divider_views_.push_back(divider);

    BuildViewsForNode(branch->second());
  }
}

void DaoSplitView::UpdateDividerPositions() {
  for (DaoSplitDividerView* divider : divider_views_) {
    if (!divider || !divider->branch_node())
      continue;

    DaoSplitBranchNode* branch = divider->branch_node();
    const gfx::Rect& first_bounds = branch->first()->bounds();
    const gfx::Rect& parent_bounds = branch->bounds();

    gfx::Rect divider_bounds;
    if (branch->direction() == SplitDirection::kHorizontal) {
      divider_bounds = gfx::Rect(first_bounds.right(), parent_bounds.y(),
                                  kDividerWidth, parent_bounds.height());
    } else {
      divider_bounds = gfx::Rect(parent_bounds.x(), first_bounds.bottom(),
                                  parent_bounds.width(), kDividerWidth);
    }
    divider->SetBoundsRect(divider_bounds);
  }
}

DaoSplitLeafNode* DaoSplitView::FindLeafAtPoint(const gfx::Point& point) {
  DaoSplitNode* root = GetLayoutRoot();
  if (!root)
    return nullptr;

  // Walk the tree and find the leaf whose bounds contain the point.
  std::function<DaoSplitLeafNode*(DaoSplitNode*)> find;
  find = [&](DaoSplitNode* node) -> DaoSplitLeafNode* {
    if (node->IsLeaf()) {
      DaoSplitLeafNode* leaf = node->AsLeaf();
      if (leaf->bounds().Contains(point))
        return leaf;
      return nullptr;
    }
    if (node->IsBranch()) {
      DaoSplitBranchNode* branch = node->AsBranch();
      if (DaoSplitLeafNode* found = find(branch->first()))
        return found;
      return find(branch->second());
    }
    return nullptr;
  };

  return find(root);
}

std::optional<SplitDirection> DaoSplitView::DetectDropZone(
    DaoSplitLeafNode* leaf,
    const gfx::Point& point) {
  if (!leaf)
    return std::nullopt;

  const gfx::Rect& bounds = leaf->bounds();
  // Default to left/right splitting. Only when the pointer is clearly near the
  // top or bottom edge do we switch to a vertical split target.
  int top_bottom_zone = std::clamp(bounds.height() / 5, 56, 140);
  if (point.y() - bounds.y() < top_bottom_zone)
    return SplitDirection::kVertical;
  if (bounds.bottom() - point.y() < top_bottom_zone)
    return SplitDirection::kVertical;
  return SplitDirection::kHorizontal;
}

void DaoSplitView::BlockAllNativeEvents(bool block) {
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      if (block) {
        BlockWebContentNativeEvents(pane->web_contents());
      } else {
        UnblockWebContentNativeEvents(pane->web_contents());
      }
    }
  }
}

}  // namespace dao
