// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"

#include <algorithm>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/sidebar/dao_favorites_view.h"
#include "dao/browser/ui/views/sidebar/dao_new_tab_button.h"
#include "dao/browser/ui/views/sidebar/dao_tab_list_view.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/background.h"
#include "ui/views/style/typography.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

BEGIN_METADATA(DaoSidebarView)
END_METADATA

DaoSidebarView::DaoSidebarView(Browser* browser)
    : browser_(browser),
      collapse_animation_(base::Milliseconds(50), 60, this) {
  SetPaintToLayer();
  layer()->SetMasksToBounds(true);

  // Inner container always keeps full width; outer view clips it
  inner_container_ = AddChildView(std::make_unique<views::View>());
  auto* layout = inner_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::TLBR(0, 4, 0, 4), 16));
  inner_container_->SetBackground(
      views::CreateSolidBackground(dao::kSidebarBackground));

  // Header row: traffic-light spacer + toggle sidebar button
  auto header_row = std::make_unique<views::View>();
  auto* header_layout = header_row->SetLayoutManager(
      std::make_unique<views::FlexLayout>());
  header_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  header_layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  // Left inset clears macOS traffic lights (~70px)
  header_layout->SetInteriorMargin(gfx::Insets::TLBR(0, 70, 0, 0));
  header_row->SetPreferredSize(gfx::Size(0, 36));

  // Flexible spacer pushes toggle button to the right
  auto spacer = std::make_unique<views::View>();
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  header_row->AddChildView(std::move(spacer));

  // Toggle sidebar button
  auto toggle_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoSidebarView::ToggleCollapsed,
                          base::Unretained(this)),
      u"\u2630");  // ☰ hamburger icon
  toggle_btn->SetEnabledTextColors(dao::kTextSecondary);
  toggle_btn->SetTextSubpixelRenderingEnabled(false);
  toggle_btn->SetLabelStyle(views::style::STYLE_BODY_1);
  toggle_btn->SetPreferredSize(gfx::Size(32, 32));
  toggle_btn->SetInstallFocusRingOnFocus(false);
  toggle_btn->SetTooltipText(u"Toggle Sidebar (\u2318S)");
  toggle_button_ = header_row->AddChildView(std::move(toggle_btn));

  inner_container_->AddChildView(std::move(header_row));

  // Favorites row
  favorites_ = inner_container_->AddChildView(
      std::make_unique<DaoFavoritesView>(browser));

  // Tab list (pinned section + new-tab button + today section)
  tab_list_view_ = inner_container_->AddChildView(
      std::make_unique<DaoTabListView>(browser));
  layout->SetFlexForView(tab_list_view_, 1);

  // Wire active-tab click to show floating omnibox
  tab_list_view_->set_show_omnibox_callback(
      base::BindRepeating(&DaoSidebarView::ShowOmniboxPopup,
                          base::Unretained(this)));

  // Resize handle on the right edge
  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));
}

DaoSidebarView::~DaoSidebarView() = default;

gfx::Size DaoSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(current_width_, 0);
}

void DaoSidebarView::Layout(PassKey) {
  if (inner_container_) {
    // During collapse/expand animation, keep inner at user_width_ so content
    // is clipped smoothly. During resize, use current_width_ so background
    // fills the entire sidebar and content reflows in real time.
    int container_width =
        (collapsed_ || collapse_animation_.is_animating()) ? user_width_
                                                           : current_width_;
    inner_container_->SetBoundsRect(
        gfx::Rect(0, 0, container_width, height()));
  }
  if (resize_area_) {
    resize_area_->SetVisible(!collapsed_);
    resize_area_->SetBoundsRect(
        gfx::Rect(width() - kResizeAreaWidth, 0, kResizeAreaWidth, height()));
  }
}

// --- Resize ---------------------------------------------------------------

void DaoSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (collapsed_) {
    return;
  }
  if (!is_resizing_) {
    is_resizing_ = true;
    resize_start_width_ = current_width_;
  }
  int new_width = resize_start_width_ + resize_amount;
  new_width = std::clamp(new_width, kMinWidth, kMaxWidth);
  current_width_ = new_width;
  if (done_resizing) {
    is_resizing_ = false;
    user_width_ = new_width;
    target_width_ = new_width;
  }
  PreferredSizeChanged();
}

// --- Collapse / expand ---------------------------------------------------

void DaoSidebarView::ToggleCollapsed() {
  auto_expanded_ = false;
  collapsed_ = !collapsed_;
  start_width_ = current_width_;
  target_width_ = collapsed_ ? kCollapsedWidth : user_width_;
  collapse_animation_.Stop();
  collapse_animation_.Start();
}

void DaoSidebarView::AnimationProgressed(const gfx::Animation* animation) {
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                         animation->GetCurrentValue());
  current_width_ =
      start_width_ + static_cast<int>((target_width_ - start_width_) * t);
  PreferredSizeChanged();
}

void DaoSidebarView::AnimationEnded(const gfx::Animation* animation) {
  current_width_ = target_width_;
  PreferredSizeChanged();
}

// --- Keyboard shortcut (Cmd+\) -------------------------------------------

void DaoSidebarView::AddedToWidget() {
  View::AddedToWidget();
  if (GetFocusManager()) {
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_OEM_5, ui::EF_COMMAND_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_S, ui::EF_COMMAND_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
  }
}

void DaoSidebarView::RemovedFromWidget() {
  if (GetFocusManager()) {
    GetFocusManager()->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_OEM_5, ui::EF_COMMAND_DOWN), this);
    GetFocusManager()->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_S, ui::EF_COMMAND_DOWN), this);
  }
  View::RemovedFromWidget();
}

bool DaoSidebarView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  ToggleCollapsed();
  return true;
}

// --- Edge hover auto-expand ----------------------------------------------

void DaoSidebarView::OnMouseEntered(const ui::MouseEvent& event) {
  if (collapsed_ && !collapse_animation_.is_animating()) {
    auto_expanded_ = true;
    collapsed_ = false;
    start_width_ = current_width_;
    target_width_ = user_width_;
    collapse_animation_.Stop();
    collapse_animation_.Start();
  }
}

void DaoSidebarView::OnMouseExited(const ui::MouseEvent& event) {
  if (auto_expanded_) {
    auto_expanded_ = false;
    collapsed_ = true;
    start_width_ = current_width_;
    target_width_ = kCollapsedWidth;
    collapse_animation_.Stop();
    collapse_animation_.Start();
  }
}

// --- New tab button highlight -----------------------------------------------

void DaoSidebarView::SetNewTabHighlighted(bool highlighted) {
  if (tab_list_view_) {
    tab_list_view_->SetNewTabHighlighted(highlighted);
  }
}

// --- Command bar delegation -------------------------------------------------

void DaoSidebarView::ShowOmniboxPopup() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->Show();
  }
}

void DaoSidebarView::HideOmniboxPopup() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->Hide();
  }
}

}  // namespace dao
