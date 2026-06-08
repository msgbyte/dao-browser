// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/split/dao_split_view.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/task/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "dao/browser/ui/views/sidebar/dao_tab_tooltip_view.h"
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
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/display/screen.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"

namespace dao {

namespace {

constexpr auto kDeferredDropActionDelay = base::Milliseconds(25);
constexpr char kSplitLayoutGroupsKey[] = "groups";
constexpr char kSplitNodeTypeKey[] = "type";
constexpr char kSplitNodeBranchType[] = "branch";
constexpr char kSplitNodeLeafType[] = "leaf";
constexpr char kSplitNodeDirectionKey[] = "direction";
constexpr char kSplitNodeHorizontalDirection[] = "horizontal";
constexpr char kSplitNodeVerticalDirection[] = "vertical";
constexpr char kSplitNodeRatioKey[] = "ratio";
constexpr char kSplitNodeFirstKey[] = "first";
constexpr char kSplitNodeSecondKey[] = "second";
constexpr char kSplitNodeUrlKey[] = "url";
constexpr char kSplitNodeTabSessionIdKey[] = "tab_session_id";

void RefreshSplitLayout(Browser* browser, views::View* view) {
  if (view->parent()) {
    view->parent()->InvalidateLayout();
  }
  view->InvalidateLayout();
  view->SchedulePaint();

  if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser)) {
    // Split state changes are reflected in WebUI via PushFullState().
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

std::string SplitLayoutStorageKey(Browser* browser) {
  return browser ? base::NumberToString(browser->session_id().id())
                 : std::string();
}

content::WebContents* FindTabBySessionId(
    TabStripModel* tab_strip_model,
    int tab_session_id,
    const std::set<content::WebContents*>& used_contents) {
  if (!tab_strip_model)
    return nullptr;

  for (int index = 0; index < tab_strip_model->count(); ++index) {
    content::WebContents* contents = tab_strip_model->GetWebContentsAt(index);
    if (!contents || used_contents.contains(contents)) {
      continue;
    }
    const auto candidate_id = sessions::SessionTabHelper::IdForTab(contents);
    if (candidate_id.is_valid() && candidate_id.id() == tab_session_id) {
      return contents;
    }
  }
  return nullptr;
}

content::WebContents* FindTabByUrl(
    TabStripModel* tab_strip_model,
    const std::string& url,
    const std::set<content::WebContents*>& used_contents) {
  if (!tab_strip_model || url.empty())
    return nullptr;

  for (int index = 0; index < tab_strip_model->count(); ++index) {
    content::WebContents* contents = tab_strip_model->GetWebContentsAt(index);
    if (!contents || used_contents.contains(contents)) {
      continue;
    }
    if (contents->GetVisibleURL().spec() == url ||
        contents->GetLastCommittedURL().spec() == url) {
      return contents;
    }
  }
  return nullptr;
}

std::unique_ptr<DaoSplitNode> RestoreNodeFromLayout(
    const base::DictValue& dict,
    TabStripModel* tab_strip_model,
    std::set<content::WebContents*>* used_contents) {
  const std::string* type = dict.FindString(kSplitNodeTypeKey);
  if (!type || !used_contents)
    return nullptr;

  if (*type == kSplitNodeLeafType) {
    content::WebContents* contents = nullptr;
    const std::optional<int> tab_session_id =
        dict.FindInt(kSplitNodeTabSessionIdKey);
    if (tab_session_id.has_value()) {
      contents = FindTabBySessionId(tab_strip_model, tab_session_id.value(),
                                    *used_contents);
    }
    if (!contents) {
      const std::string* url = dict.FindString(kSplitNodeUrlKey);
      if (url) {
        contents = FindTabByUrl(tab_strip_model, *url, *used_contents);
      }
    }
    if (!contents)
      return nullptr;

    used_contents->insert(contents);
    return std::make_unique<DaoSplitLeafNode>(contents);
  }

  if (*type != kSplitNodeBranchType)
    return nullptr;

  const std::string* direction_string =
      dict.FindString(kSplitNodeDirectionKey);
  if (!direction_string)
    return nullptr;

  SplitDirection direction;
  if (*direction_string == kSplitNodeHorizontalDirection) {
    direction = SplitDirection::kHorizontal;
  } else if (*direction_string == kSplitNodeVerticalDirection) {
    direction = SplitDirection::kVertical;
  } else {
    return nullptr;
  }

  const base::DictValue* first_dict = dict.FindDict(kSplitNodeFirstKey);
  const base::DictValue* second_dict = dict.FindDict(kSplitNodeSecondKey);
  if (!first_dict || !second_dict)
    return nullptr;

  std::set<content::WebContents*> used_before_branch = *used_contents;
  auto first = RestoreNodeFromLayout(*first_dict, tab_strip_model,
                                     used_contents);
  auto second = RestoreNodeFromLayout(*second_dict, tab_strip_model,
                                      used_contents);
  if (!first || !second) {
    *used_contents = std::move(used_before_branch);
    return nullptr;
  }

  const std::optional<double> ratio = dict.FindDouble(kSplitNodeRatioKey);
  return std::make_unique<DaoSplitBranchNode>(
      direction, ratio.has_value() ? static_cast<float>(ratio.value()) : 0.5f,
      std::move(first), std::move(second));
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
    canvas->FillRect(GetLocalBounds(), DropZoneOverlay());
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

  if (browser_->is_session_restore()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DaoSplitView::RestoreLayout,
                                  weak_factory_.GetWeakPtr()));
  }
}

DaoSplitView::~DaoSplitView() {
  hover_timer_.Stop();
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoSplitView::SetTabDragActive(bool active) {
  LOG(ERROR) << "[Dao-Xwin] SetTabDragActive(" << active
             << ") browser_sessionId="
             << (browser_ ? browser_->session_id().id() : -1);
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

void DaoSplitView::OnMouseEntered(const ui::MouseEvent& event) {
  views::View::OnMouseEntered(event);
  ClearSidebarHoverState();
}

void DaoSplitView::OnMouseMoved(const ui::MouseEvent& event) {
  views::View::OnMouseMoved(event);
  ClearSidebarHoverState();
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
  if (split_state_changed_callback_)
    split_state_changed_callback_.Run();

  // Animate new pane sliding in from the split edge.
  constexpr auto kCreateDuration = base::Milliseconds(250);
  for (DaoSplitPaneView* pane : pane_views_) {
    if (!pane || !pane->web_contents())
      continue;
    if (pane->web_contents() == new_contents) {
      gfx::Rect target = pane->bounds();
      gfx::Rect start = target;
      if (direction == SplitDirection::kHorizontal) {
        start.set_width(0);
        if (!new_contents_first) {
          start.set_x(target.right());
        }
      } else {
        start.set_height(0);
        if (!new_contents_first) {
          start.set_y(target.bottom());
        }
      }
      AnimatePaneBounds(pane, start, target, kCreateDuration);
      break;
    }
  }

  UpdateHoverTracking();
  return true;
}

bool DaoSplitView::MovePane(content::WebContents* source_contents,
                            content::WebContents* target_contents,
                            SplitDirection direction,
                            bool source_first) {
  if (!source_contents || !target_contents || source_contents == target_contents) {
    return false;
  }

  SplitGroup* source_group = FindGroupForContents(source_contents);
  SplitGroup* target_group = FindGroupForContents(target_contents);
  if (!source_group || !target_group || !source_group->root ||
      !target_group->root) {
    return false;
  }

  const bool same_group = source_group == target_group;
  RemoveContentsFromGroup(source_group, source_contents);
  if (same_group && !source_group->root) {
    source_group->root = std::make_unique<DaoSplitLeafNode>(target_contents);
    source_group->root->Layout(GetLocalBounds());
  } else if (!same_group) {
    RemoveEmptyOrCollapsedGroups();
  }

  target_group = FindGroupForContents(target_contents);
  if (!target_group || !target_group->root) {
    return false;
  }

  DaoSplitLeafNode* target_leaf = target_group->root->FindLeaf(target_contents);
  if (!target_leaf) {
    return false;
  }

  // SplitLeaf updates target_group->root via the reference parameter.
  // Its return value is not the new root — do not overwrite root with it.
  dao::SplitLeaf(target_group->root, target_leaf, direction,
                 source_first, source_contents);
  if (!target_group->root || target_group->root->CountLeaves() <= 1) {
    return false;
  }

  target_group->root->Layout(GetLocalBounds());
  active_group_ = target_group;

  // Snapshot current pane bounds for animation.
  std::map<content::WebContents*, gfx::Rect> old_bounds;
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      old_bounds[pane->web_contents()] = pane->bounds();
    }
  }

  DetachPrimaryContentsHost(target_contents, source_contents);
  RebuildViews();
  UpdatePaneVisibility();

  if (int source_index = tab_strip_model_->GetIndexOfWebContents(source_contents);
      source_index != TabStripModel::kNoTab) {
    tab_strip_model_->ActivateTabAt(source_index);
  }
  SaveLayout();

  // Force all panes to cycle visibility so the native views reposition
  // to their new bounds (panes are reused, so contents_visible_ is already
  // true and EnsureAllPanesShown would be a no-op).
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      pane->SetContentsVisible(false);
      pane->SetContentsVisible(true);
    }
  }

  // Animate panes from old bounds to new bounds.
  constexpr auto kRearrangeDuration = base::Milliseconds(200);
  for (DaoSplitPaneView* pane : pane_views_) {
    if (!pane || !pane->web_contents())
      continue;
    auto it = old_bounds.find(pane->web_contents());
    if (it != old_bounds.end() && it->second != pane->bounds()) {
      AnimatePaneBounds(pane, it->second, pane->bounds(), kRearrangeDuration);
    }
  }

  SyncActivePaneWithActiveTab();
  RefreshSplitLayout(browser_, this);
  UpdateHoverTracking();
  return true;
}

bool DaoSplitView::ClosePane(content::WebContents* web_contents) {
  SplitGroup* group = FindGroupForContents(web_contents);
  if (!group || !group->root)
    return false;

  auto* leaf = group->root->FindLeaf(web_contents);
  if (!leaf)
    return false;

  if (group->root->CountLeaves() <= 1)
    return false;

  // Snapshot closing pane info for animation.
  gfx::Rect closing_bounds;
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents() == web_contents) {
      closing_bounds = pane->bounds();
      break;
    }
  }

  SplitDirection close_dir = SplitDirection::kHorizontal;
  bool is_first_child = false;
  if (leaf->parent()) {
    close_dir = leaf->parent()->direction();
    is_first_child = (leaf->parent()->first() == leaf);
  }

  dao::CloseLeaf(group->root, leaf);
  RemoveEmptyOrCollapsedGroups();

  SyncActiveGroupWithActiveTab();
  RebuildViews();
  SaveLayout();
  SyncActivePaneWithActiveTab();
  RefreshSplitLayout(browser_, this);
  UpdateHoverTracking();
  if (split_state_changed_callback_)
    split_state_changed_callback_.Run();

  // Animate surviving panes expanding to fill the closed pane's space.
  constexpr auto kCloseDuration = base::Milliseconds(200);
  for (DaoSplitPaneView* pane : pane_views_) {
    if (!pane)
      continue;
    gfx::Rect target = pane->bounds();
    gfx::Rect start = target;
    if (close_dir == SplitDirection::kHorizontal) {
      if (is_first_child) {
        start.set_x(start.x() + closing_bounds.width());
        start.set_width(start.width() - closing_bounds.width());
      } else {
        start.set_width(start.width() - closing_bounds.width());
      }
    } else {
      if (is_first_child) {
        start.set_y(start.y() + closing_bounds.height());
        start.set_height(start.height() - closing_bounds.height());
      } else {
        start.set_height(start.height() - closing_bounds.height());
      }
    }
    if (start != target) {
      AnimatePaneBounds(pane, start, target, kCloseDuration);
    }
  }

  return true;
}

void DaoSplitView::UnsplitKeepingPane(content::WebContents* keep_contents) {
  if (!keep_contents)
    return;

  // Activate the tab we want to keep so that GetActiveWebContents() returns
  // it during RebuildViews.
  if (tab_strip_model_) {
    int index = tab_strip_model_->GetIndexOfWebContents(keep_contents);
    if (index != TabStripModel::kNoTab) {
      tab_strip_model_->ActivateTabAt(index);
    }
  }

  // Find and close the OTHER pane(s) in the same group.
  SplitGroup* group = FindGroupForContents(keep_contents);
  if (!group || !group->root)
    return;

  std::vector<content::WebContents*> all_contents;
  CollectLeafContents(group->root.get(), &all_contents);
  for (auto* wc : all_contents) {
    if (wc != keep_contents) {
      ClosePane(wc);
      return;  // Group collapses after closing one pane in a 2-pane split.
    }
  }
}

void DaoSplitView::SetActivePane(DaoSplitPaneView* pane) {
  if (active_pane_ == pane)
    return;

  if (active_pane_)
    active_pane_->SetActive(false);

  active_pane_ = pane;

  if (active_pane_) {
    active_pane_->SetActive(true);

    content::WebContents* wc = active_pane_->web_contents();
    if (wc && tab_strip_model_) {
      int index = tab_strip_model_->GetIndexOfWebContents(wc);
      if (index != TabStripModel::kNoTab &&
          index != tab_strip_model_->active_index()) {
        tab_strip_model_->ActivateTabAt(index);
      }
    }
  }
}

void DaoSplitView::BeginPaneRearrange(content::WebContents* source_contents) {
  if (!source_contents || !IsSplitActive()) {
    return;
  }

  rearrange_source_contents_ = source_contents;
  drop_target_leaf_ = nullptr;
  drop_zone_direction_.reset();
  BlockAllNativeEvents(true);
}

void DaoSplitView::UpdatePaneRearrange(const gfx::Point& point_in_view) {
  if (!rearrange_source_contents_ || !IsSplitActive()) {
    return;
  }

  DaoSplitLeafNode* leaf = FindLeafAtPoint(point_in_view);
  if (!leaf || leaf->web_contents() == rearrange_source_contents_) {
    drop_target_leaf_ = nullptr;
    drop_zone_direction_.reset();
    if (drop_overlay_) {
      drop_overlay_->SetVisible(false);
    }
    return;
  }

  drop_target_leaf_ = leaf;
  drop_zone_direction_ = DetectDropZone(leaf, point_in_view);
  if (!drop_zone_direction_.has_value()) {
    if (drop_overlay_) {
      drop_overlay_->SetVisible(false);
    }
    return;
  }

  gfx::Rect overlay_bounds = leaf->bounds();
  switch (drop_zone_direction_.value()) {
    case SplitDirection::kHorizontal:
      if (point_in_view.x() > leaf->bounds().CenterPoint().x()) {
        overlay_bounds.set_x(overlay_bounds.CenterPoint().x());
        overlay_bounds.set_width(overlay_bounds.width() / 2);
      } else {
        overlay_bounds.set_width(overlay_bounds.width() / 2);
      }
      break;
    case SplitDirection::kVertical:
      if (point_in_view.y() > leaf->bounds().CenterPoint().y()) {
        overlay_bounds.set_y(overlay_bounds.CenterPoint().y());
        overlay_bounds.set_height(overlay_bounds.height() / 2);
      } else {
        overlay_bounds.set_height(overlay_bounds.height() / 2);
      }
      break;
  }

  if (drop_overlay_) {
    drop_overlay_->SetBoundsRect(overlay_bounds);
    drop_overlay_->SetVisible(true);
  }
}

void DaoSplitView::EndPaneRearrange(const gfx::Point& point_in_view) {
  if (!rearrange_source_contents_) {
    return;
  }

  UpdatePaneRearrange(point_in_view);

  content::WebContents* source_contents = rearrange_source_contents_.get();
  content::WebContents* target_contents =
      drop_target_leaf_ ? drop_target_leaf_->web_contents() : nullptr;

  if (target_contents && drop_zone_direction_.has_value() &&
      source_contents != target_contents) {
    const bool source_first =
        drop_zone_direction_.value() == SplitDirection::kHorizontal
            ? point_in_view.x() <= drop_target_leaf_->bounds().CenterPoint().x()
            : point_in_view.y() <= drop_target_leaf_->bounds().CenterPoint().y();
    MovePane(source_contents, target_contents, drop_zone_direction_.value(),
             source_first);
  }

  rearrange_source_contents_ = nullptr;
  drop_target_leaf_ = nullptr;
  drop_zone_direction_.reset();
  if (drop_overlay_) {
    drop_overlay_->SetVisible(false);
  }
  BlockAllNativeEvents(false);
}

bool DaoSplitView::PopOutPane(content::WebContents* web_contents) {
  if (!web_contents || !tab_strip_model_) {
    return false;
  }

  int index = tab_strip_model_->GetIndexOfWebContents(web_contents);
  if (index == TabStripModel::kNoTab) {
    return false;
  }

  std::unique_ptr<content::WebContents> detached_contents =
      tab_strip_model_->DetachWebContentsAtForInsertion(index);
  if (!detached_contents) {
    return false;
  }

  Browser::CreateParams params(browser_->profile(), /*user_gesture=*/true);
  Browser* new_browser = Browser::Create(params);
  if (!new_browser) {
    return false;
  }

  new_browser->window()->Show();
  new_browser->tab_strip_model()->InsertWebContentsAt(
      -1, std::move(detached_contents), AddTabTypes::ADD_ACTIVE);
  new_browser->window()->Activate();
  return true;
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
  if (!browser_ || !browser_->profile())
    return;
  if ((tab_strip_model_ && tab_strip_model_->closing_all()) ||
      browser_->IsAttemptingToCloseBrowser()) {
    return;
  }

  PrefService* prefs = browser_->profile()->GetPrefs();
  if (!prefs)
    return;

  const std::string storage_key = SplitLayoutStorageKey(browser_);
  if (storage_key.empty())
    return;

  base::ListValue groups;
  for (const auto& group : split_groups_) {
    if (!group || !group->root || group->root->CountLeaves() <= 1) {
      continue;
    }
    groups.Append(group->root->Serialize());
  }

  ScopedDictPrefUpdate update(prefs, dao::prefs::kDaoSplitLayout);
  base::DictValue& layouts = update.Get();
  if (groups.empty()) {
    layouts.Remove(storage_key);
    return;
  }

  base::DictValue window_layout;
  window_layout.Set(kSplitLayoutGroupsKey, std::move(groups));
  layouts.Set(storage_key, std::move(window_layout));
}

void DaoSplitView::RestoreLayout() {
  if (!browser_ || !browser_->profile() || !tab_strip_model_)
    return;
  if (!split_groups_.empty())
    return;

  PrefService* prefs = browser_->profile()->GetPrefs();
  if (!prefs)
    return;

  const base::DictValue& layouts =
      prefs->GetDict(dao::prefs::kDaoSplitLayout);
  const std::string current_storage_key = SplitLayoutStorageKey(browser_);
  if (current_storage_key.empty())
    return;

  struct RestoreCandidate {
    std::string storage_key;
    std::vector<std::unique_ptr<SplitGroup>> groups;
    int leaf_count = 0;
  };

  auto build_candidate =
      [this](const std::string& storage_key,
             const base::DictValue& window_layout,
             RestoreCandidate* candidate) {
        const base::ListValue* serialized_groups =
            window_layout.FindList(kSplitLayoutGroupsKey);
        if (!serialized_groups)
          return false;

        std::vector<std::unique_ptr<SplitGroup>> restored_groups;
        std::set<content::WebContents*> used_contents;
        int restored_leaf_count = 0;
        bool saw_serialized_group = false;
        for (const base::Value& serialized_group : *serialized_groups) {
          const base::DictValue* group_dict = serialized_group.GetIfDict();
          if (!group_dict)
            continue;

          saw_serialized_group = true;
          std::set<content::WebContents*> used_before_group = used_contents;
          auto root = RestoreNodeFromLayout(*group_dict, tab_strip_model_,
                                            &used_contents);
          if (!root || root->CountLeaves() <= 1) {
            used_contents = std::move(used_before_group);
            return false;
          }

          restored_leaf_count += root->CountLeaves();
          auto group = std::make_unique<SplitGroup>();
          group->root = std::move(root);
          restored_groups.push_back(std::move(group));
        }

        if (!saw_serialized_group || restored_groups.empty())
          return false;

        candidate->storage_key = storage_key;
        candidate->groups = std::move(restored_groups);
        candidate->leaf_count = restored_leaf_count;
        return true;
      };

  RestoreCandidate selected_candidate;
  bool has_candidate = false;

  if (const base::DictValue* current_window_layout =
          layouts.FindDict(current_storage_key)) {
    has_candidate =
        build_candidate(current_storage_key, *current_window_layout,
                        &selected_candidate);
  }

  if (!has_candidate) {
    for (const auto [storage_key, window_layout] : layouts) {
      if (storage_key == current_storage_key || !window_layout.is_dict()) {
        continue;
      }

      RestoreCandidate candidate;
      if (!build_candidate(storage_key, window_layout.GetDict(), &candidate)) {
        continue;
      }
      if (!has_candidate ||
          candidate.leaf_count > selected_candidate.leaf_count) {
        selected_candidate = std::move(candidate);
        has_candidate = true;
      }
    }
  }

  if (!has_candidate)
    return;

  const std::string restored_storage_key = selected_candidate.storage_key;
  split_groups_ = std::move(selected_candidate.groups);
  SyncActiveGroupWithActiveTab();
  if (active_group_) {
    DetachPrimaryContentsHostForActiveGroup();
    RebuildViews();
    UpdatePaneVisibility();
    EnsureAllPanesShown();
    SyncActivePaneWithActiveTab();
  }

  RefreshSplitLayout(browser_, this);
  UpdateHoverTracking();
  SaveLayout();

  if (restored_storage_key != current_storage_key) {
    ScopedDictPrefUpdate update(prefs, dao::prefs::kDaoSplitLayout);
    update->Remove(restored_storage_key);
  }

  if (split_state_changed_callback_)
    split_state_changed_callback_.Run();
}

// --- TabStripModelObserver ---------------------------------------------------

void DaoSplitView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kRemoved) {
    if (tab_strip_model->closing_all() ||
        (browser_ && browser_->IsAttemptingToCloseBrowser())) {
      return;
    }

    for (const auto& removed : change.GetRemove()->contents) {
      if (ContainsWebContents(removed.contents)) {
        ClosePane(removed.contents);
        break;
      }
    }
  }

  SyncSinglePaneRootWithActiveTab();
  if (split_groups_.empty() && browser_ && browser_->is_session_restore()) {
    RestoreLayout();
  }

  if (selection.active_tab_changed()) {
    SplitGroup* previous_group = active_group_;
    SyncActiveGroupWithActiveTab();
    const bool group_changed = previous_group != active_group_;
    if (group_changed) {
      RebuildViews();
      UpdatePaneVisibility();
      EnsureAllPanesShown();
      RefreshSplitLayout(browser_, this);
      UpdateHoverTracking();
    }
    SyncActivePaneWithActiveTab();
  }
}

// --- Layout ------------------------------------------------------------------

void DaoSplitView::Layout(PassKey) {
  DaoSplitNode* root = GetLayoutRoot();
  if (!root)
    return;

  root->Layout(GetLocalBounds());

  if (!IsSplitActive())
    return;

  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      DaoSplitLeafNode* leaf = active_group_->root->FindLeaf(pane->web_contents());
      if (leaf) {
        pane->SetBoundsRect(leaf->bounds());
      }
    }
  }

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
  bool ok = text.has_value() && text->starts_with(u"dao-tab-drag:");
  LOG(ERROR) << "[Dao-Xwin] DaoSplitView::CanDrop returning " << ok
             << " text_has_value=" << text.has_value()
             << " tab_drag_active=" << tab_drag_active_;
  return ok;
}

void DaoSplitView::OnDragEntered(const ui::DropTargetEvent& event) {
  LOG(ERROR) << "[Dao-Xwin] DaoSplitView::OnDragEntered at ("
             << event.location().x() << "," << event.location().y()
             << ") tab_drag_active=" << tab_drag_active_;
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

// ---- Native bypass entry points -------------------------------------------
// See header: these are called from DaoEventInterceptor (macOS) when
// BridgedContentView rejects our renderer-initiated drag at the Views
// DropHelper layer.

void DaoSplitView::UpdateNativeDropIndicator(
    const gfx::Point& location_in_view) {
  // The Views drag pipeline normally flips tab_drag_active_ via
  // SetTabDragActive; when we're driving from the native interceptor we
  // can land here before that flip. Force it on so hit-testing + the
  // overlay work.
  if (!tab_drag_active_) {
    SetTabDragActive(true);
  }

  DaoSplitLeafNode* leaf = FindLeafAtPoint(location_in_view);
  if (!leaf) {
    drop_target_leaf_ = nullptr;
    drop_zone_direction_.reset();
    drop_overlay_->SetVisible(false);
    return;
  }

  drop_target_leaf_ = leaf;
  drop_zone_direction_ = DetectDropZone(leaf, location_in_view);

  if (drop_zone_direction_.has_value()) {
    gfx::Rect overlay_bounds = leaf->bounds();
    switch (drop_zone_direction_.value()) {
      case SplitDirection::kHorizontal:
        if (location_in_view.x() > leaf->bounds().CenterPoint().x()) {
          overlay_bounds.set_x(overlay_bounds.CenterPoint().x());
          overlay_bounds.set_width(overlay_bounds.width() / 2);
        } else {
          overlay_bounds.set_width(overlay_bounds.width() / 2);
        }
        break;
      case SplitDirection::kVertical:
        if (location_in_view.y() > leaf->bounds().CenterPoint().y()) {
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
}

void DaoSplitView::HideNativeDropIndicator() {
  drop_target_leaf_ = nullptr;
  drop_zone_direction_.reset();
  drop_overlay_->SetVisible(false);
}

bool DaoSplitView::ProcessNativeTabDrop(const gfx::Point& location_in_view,
                                        const std::string& payload) {
  drop_overlay_->SetVisible(false);

  if (!tab_strip_model_) {
    return false;
  }

  DaoSplitLeafNode* target_leaf = FindLeafAtPoint(location_in_view);
  if (!target_leaf) {
    return false;
  }
  std::optional<SplitDirection> target_zone =
      DetectDropZone(target_leaf, location_in_view);

  // Parse "dao-tab-drag:<sid>:<idx>".
  const std::string kPrefix = "dao-tab-drag:";
  if (payload.size() <= kPrefix.size() ||
      payload.compare(0, kPrefix.size(), kPrefix) != 0) {
    return false;
  }
  std::string body = payload.substr(kPrefix.size());
  size_t colon = body.find(':');
  if (colon == std::string::npos) {
    return false;
  }
  int source_sid = 0;
  int tab_index = 0;
  if (!base::StringToInt(body.substr(0, colon), &source_sid) ||
      !base::StringToInt(body.substr(colon + 1), &tab_index)) {
    return false;
  }

  // Resolve source browser. Same-window when it matches ours.
  Browser* source_browser = browser_;
  bool is_cross_window = false;
  if (static_cast<int>(browser_->session_id().id()) != source_sid) {
    is_cross_window = true;
    source_browser = nullptr;
    for (Browser* b :
         chrome::FindAllBrowsersWithProfile(browser_->profile())) {
      if (static_cast<int>(b->session_id().id()) == source_sid) {
        source_browser = b;
        break;
      }
    }
    if (!source_browser) {
      return false;
    }
  }

  TabStripModel* source_model = source_browser->tab_strip_model();
  if (tab_index < 0 || tab_index >= source_model->count()) {
    return false;
  }
  content::WebContents* dragged_contents =
      source_model->GetWebContentsAt(tab_index);
  if (!dragged_contents) {
    return false;
  }

  // Capture the split target's existing WC BEFORE the cross-window insert.
  // The cross-window insert uses ADD_ACTIVE which fires
  // SyncSinglePaneRootWithActiveTab on our TabStripModelObserver path,
  // rewriting target_leaf->web_contents() to the dragged tab. If we read
  // it after the insert, `existing == dragged_contents` and the split is
  // silently skipped.
  content::WebContents* existing_at_target = target_leaf->web_contents();
  if (!existing_at_target && !IsSplitActive()) {
    existing_at_target = tab_strip_model_->GetActiveWebContents();
  }

  // Cross-window: detach and insert into our model first.
  if (is_cross_window) {
    std::unique_ptr<content::WebContents> detached =
        source_model->DetachWebContentsAtForInsertion(tab_index);
    if (!detached) {
      return false;
    }
    dragged_contents = detached.get();
    tab_strip_model_->InsertWebContentsAt(tab_strip_model_->count(),
                                          std::move(detached),
                                          AddTabTypes::ADD_ACTIVE);
    tab_index = tab_strip_model_->GetIndexOfWebContents(dragged_contents);
    if (source_model->count() == 0 && source_browser->window()) {
      source_browser->window()->Close();
    }
  }

  if (target_zone.has_value()) {
    // Split drop.
    content::WebContents* existing = existing_at_target;
    if (existing && !IsSplitActive()) {
      target_leaf->set_web_contents(existing);
    }
    if (existing && existing != dragged_contents) {
      const bool new_first =
          target_zone.value() == SplitDirection::kHorizontal
              ? location_in_view.x() <=
                    target_leaf->bounds().CenterPoint().x()
              : location_in_view.y() <=
                    target_leaf->bounds().CenterPoint().y();
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&DaoSplitView::PerformDeferredSplit,
                         weak_factory_.GetWeakPtr(), existing,
                         target_zone.value(), new_first, dragged_contents),
          kDeferredDropActionDelay);
    }
  } else {
    // Center drop — swap or activate.
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
      target_leaf->set_web_contents(dragged_contents);
      tab_strip_model_->ActivateTabAt(tab_index);
    }
  }

  drop_target_leaf_ = nullptr;
  drop_zone_direction_.reset();
  SetTabDragActive(false);
  return true;
}

views::View::DropCallback DaoSplitView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  return base::BindOnce(&DaoSplitView::OnDrop, base::Unretained(this));
}

void DaoSplitView::OnDrop(
    const ui::DropTargetEvent& event,
    ui::mojom::DragOperation& output_drag_op,
    std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
  LOG(ERROR) << "[Dao-Xwin] DaoSplitView::OnDrop ENTRY at ("
             << event.location().x() << "," << event.location().y()
             << ") tab_drag_active=" << tab_drag_active_;
  drop_overlay_->SetVisible(false);
  BlockAllNativeEvents(false);

  DaoSplitLeafNode* target_leaf = FindLeafAtPoint(event.location());
  std::optional<SplitDirection> target_zone =
      target_leaf ? DetectDropZone(target_leaf, event.location()) : std::nullopt;

  if (!target_leaf || !tab_strip_model_) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  // Parse drag data: "dao-tab-drag:<session_id>:<tab_index>" or legacy
  // "dao-tab-drag:<tab_index>".
  auto text = event.data().GetString();
  if (!text.has_value() || !text->starts_with(u"dao-tab-drag:")) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  std::u16string payload = text->substr(13);  // len("dao-tab-drag:") = 13
  int tab_index = 0;
  bool is_cross_window = false;
  Browser* source_browser = browser_;

  size_t colon_pos = payload.find(u':');
  if (colon_pos != std::u16string::npos) {
    // New format: "<session_id>:<tab_index>"
    int source_session_id = 0;
    if (!base::StringToInt(payload.substr(0, colon_pos),
                           &source_session_id) ||
        !base::StringToInt(payload.substr(colon_pos + 1), &tab_index)) {
      output_drag_op = ui::mojom::DragOperation::kNone;
      return;
    }
    if (static_cast<int>(browser_->session_id().id()) !=
        source_session_id) {
      is_cross_window = true;
      source_browser = nullptr;
      for (Browser* b :
           chrome::FindAllBrowsersWithProfile(browser_->profile())) {
        if (static_cast<int>(b->session_id().id()) == source_session_id) {
          source_browser = b;
          break;
        }
      }
      if (!source_browser) {
        output_drag_op = ui::mojom::DragOperation::kNone;
        return;
      }
    }
  } else {
    // Legacy format: "<tab_index>"
    if (!base::StringToInt(payload, &tab_index)) {
      output_drag_op = ui::mojom::DragOperation::kNone;
      return;
    }
  }

  TabStripModel* source_model = source_browser->tab_strip_model();
  content::WebContents* dragged_contents =
      source_model->GetWebContentsAt(tab_index);
  if (!dragged_contents) {
    output_drag_op = ui::mojom::DragOperation::kNone;
    return;
  }

  // Cross-window: detach from source and insert into local model.
  if (is_cross_window) {
    std::unique_ptr<content::WebContents> detached =
        source_model->DetachWebContentsAtForInsertion(tab_index);
    if (!detached) {
      output_drag_op = ui::mojom::DragOperation::kNone;
      return;
    }
    dragged_contents = detached.get();
    tab_strip_model_->InsertWebContentsAt(
        tab_strip_model_->count(), std::move(detached),
        AddTabTypes::ADD_ACTIVE);
    tab_index = tab_strip_model_->GetIndexOfWebContents(dragged_contents);
    // Auto-close source window if empty.
    if (source_model->count() == 0) {
      source_browser->window()->Close();
    }
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
    if (split_active) {
      pane->EnsureContentsAttached();
    }
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

  // Re-attachment of the active WebContents to the primary ContentsWebView
  // is handled inside RebuildViews() (via TakeWebContents + forced resize).
}

void DaoSplitView::ReattachWebContentsToPrimary(
    content::WebContents* web_contents) {
  if (!web_contents || !browser_)
    return;
  if (tab_strip_model_ &&
      tab_strip_model_->GetActiveWebContents() != web_contents) {
    return;
  }
  auto* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!bv)
    return;
  auto* cwv = bv->contents_web_view();
  if (!cwv || cwv->web_contents() == web_contents)
    return;
  cwv->SetWebContents(web_contents);
}

void DaoSplitView::UpdateHoverTracking() {
  if (IsSplitActive()) {
    if (!hover_timer_.IsRunning()) {
      hover_timer_.Start(FROM_HERE, base::Milliseconds(120),
                         this, &DaoSplitView::CheckCursorOverPanes);
    }
  } else {
    hover_timer_.Stop();
    for (DaoSplitPaneView* pane : pane_views_) {
      if (pane) {
        pane->SetHeaderHovered(false);
      }
    }
  }
}

void DaoSplitView::CheckCursorOverPanes() {
  if (!IsSplitActive() || !GetWidget()) {
    hover_timer_.Stop();
    return;
  }

  gfx::Point cursor_screen =
      display::Screen::Get()->GetCursorScreenPoint();
  bool cursor_over_pane = false;

  for (DaoSplitPaneView* pane : pane_views_) {
    if (!pane || !pane->GetVisible())
      continue;

    gfx::Point local = cursor_screen;
    views::View::ConvertPointFromScreen(pane, &local);

    const bool in_pane = local.x() >= 0 && local.x() < pane->width() &&
                         local.y() >= 0 && local.y() < pane->height();
    cursor_over_pane = cursor_over_pane || in_pane;

    bool in_top_area = in_pane && local.y() < DaoAddressBarView::kBarHeight;
    pane->SetHeaderHovered(in_top_area);
  }

  if (cursor_over_pane) {
    ClearSidebarHoverState();
  }
}

void DaoSplitView::ClearSidebarHoverState() {
  if (!IsSplitActive() || !browser_) {
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }

  if (auto* tooltip = browser_view->dao_tab_tooltip()) {
    tooltip->HideTooltip();
  }
  if (auto* sidebar = browser_view->dao_sidebar()) {
    sidebar->NotifySidebarPointerExited();
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

void DaoSplitView::DetachPrimaryContentsHostForActiveGroup() {
  if (!active_group_ || !active_group_->root) {
    return;
  }

  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }

  auto* contents_web_view = browser_view->contents_web_view();
  if (!contents_web_view) {
    return;
  }

  content::WebContents* hosted_contents = contents_web_view->web_contents();
  if (hosted_contents && active_group_->root->FindLeaf(hosted_contents)) {
    contents_web_view->SetWebContents(nullptr);
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

  if (!active_group_ || !active_group_->root) {
    // Split is being deactivated.  Transfer the active WebContents
    // back to the primary ContentsWebView.
    //
    // Note: reparenting the native view may cause a transient duplicate
    // paint chunk ID in the renderer's PaintController (stale cached
    // subsequences from the old view).  This is harmless — the renderer
    // self-corrects on the next paint cycle.  We patch CheckNewChunkId
    // to downgrade the DCHECK-only DUMP_WILL_BE_NOTREACHED to a
    // non-fatal LOG(ERROR).
    content::WebContents* active_wc =
        tab_strip_model_ ? tab_strip_model_->GetActiveWebContents() : nullptr;

    // Detach active WC from its pane without triggering WasHidden
    // (TakeWebContents just clears the pane's internal state).
    if (active_wc) {
      for (auto& [wc, pane] : existing_panes) {
        if (wc == active_wc && pane) {
          pane->TakeWebContents();
          break;
        }
      }
    }

    // Re-attach to primary CWV immediately for seamless visual transition.
    if (active_wc) {
      if (auto* bv = BrowserView::GetBrowserViewForBrowser(browser_)) {
        if (auto* cwv = bv->contents_web_view()) {
          cwv->SetWebContents(active_wc);
        }
      }
    }

    // Destroy remaining panes.
    for (const auto& [wc, pane] : existing_panes) {
      static_cast<void>(wc);
      if (!pane)
        continue;
      if (pane->web_contents()) {
        pane->SetContentsVisible(false);
        pane->SetWebContents(nullptr);
      }
      RemoveChildView(pane);
      delete pane;
    }
    pane_views_.clear();
    return;
  }

  DetachPrimaryContentsHostForActiveGroup();

  std::vector<raw_ptr<DaoSplitPaneView>> rebuilt_panes;
  std::function<void(DaoSplitNode*)> rebuild_node = [&](DaoSplitNode* node) {
    if (node->IsLeaf()) {
      DaoSplitLeafNode* leaf = node->AsLeaf();
      DaoSplitPaneView* pane = nullptr;
      auto existing_it = existing_panes.find(leaf->web_contents());
      if (existing_it != existing_panes.end()) {
        pane = existing_it->second;
        existing_panes.erase(existing_it);
        pane->EnsureContentsAttached();
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
    pane->SetContentsVisible(false);
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
  // Collect every WebContents we care about. Each one only serves to locate
  // the NSWindow whose contentView should host the DaoEventInterceptor; the
  // interceptor covers the whole window so one WebContents per window is
  // enough.
  std::vector<content::WebContents*> targets;
  for (DaoSplitPaneView* pane : pane_views_) {
    if (pane && pane->web_contents()) {
      targets.push_back(pane->web_contents());
    }
  }
  // When the window has NO split panes (single-tab view), pane_views_ is
  // empty. Without a fallback, the interceptor never gets installed, so the
  // sidebar WebView's HTML5 drag never escapes to the native Views tree and
  // DaoSplitView's OnDragEntered/OnDrop are never called on other windows.
  // Fall back to the currently active tab so the interceptor gets mounted
  // in single-tab mode too — this is what makes cross-window tab drag
  // actually work for most users.
  if (targets.empty() && browser_) {
    if (auto* active = browser_->tab_strip_model()->GetActiveWebContents()) {
      targets.push_back(active);
    }
  }

  LOG(ERROR) << "[Dao-Xwin] BlockAllNativeEvents(" << block
             << ") target_count=" << targets.size();

  for (auto* wc : targets) {
    if (block) {
      BlockWebContentNativeEvents(wc);
    } else {
      UnblockWebContentNativeEvents(wc);
    }
  }
}

void DaoSplitView::AnimatePaneBounds(DaoSplitPaneView* pane,
                                     const gfx::Rect& start,
                                     const gfx::Rect& end,
                                     base::TimeDelta duration) {
  if (!pane || !pane->layer())
    return;

  pane->SetBoundsRect(start);

  ui::ScopedLayerAnimationSettings settings(pane->layer()->GetAnimator());
  settings.SetTransitionDuration(duration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  pane->SetBoundsRect(end);
}

void DaoSplitView::AnimateDividerBounds(DaoSplitDividerView* divider,
                                        const gfx::Rect& start,
                                        const gfx::Rect& end,
                                        base::TimeDelta duration) {
  if (!divider || !divider->layer())
    return;

  divider->SetBoundsRect(start);

  ui::ScopedLayerAnimationSettings settings(divider->layer()->GetAnimator());
  settings.SetTransitionDuration(duration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  settings.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  divider->SetBoundsRect(end);
}

}  // namespace dao
