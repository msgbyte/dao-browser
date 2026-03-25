// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_VIEW_H_

#include <set>

#include "base/memory/raw_ptr.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class LabelButton;
}

namespace dao {

class DaoDownloadButtonView;
class DaoFavoritesView;
class DaoNewTabButton;
class DaoTabListView;

class DaoSidebarView : public views::View,
                       public gfx::AnimationDelegate,
                       public views::ResizeAreaDelegate,
                       public ui::ImplicitAnimationObserver {
  METADATA_HEADER(DaoSidebarView, views::View)

 public:
  static constexpr int kDefaultWidth = 212;
  static constexpr int kCollapsedWidth = 6;
  static constexpr int kMinWidth = 150;
  static constexpr int kMaxWidth = 400;
  static constexpr int kResizeAreaWidth = 6;

  explicit DaoSidebarView(Browser* browser);
  DaoSidebarView(const DaoSidebarView&) = delete;
  DaoSidebarView& operator=(const DaoSidebarView&) = delete;
  ~DaoSidebarView() override;

  void ToggleCollapsed();
  bool collapsed() const { return collapsed_; }

  DaoTabListView* tab_list_view() { return tab_list_view_; }
  DaoNewTabButton* new_tab_button() { return new_tab_button_; }
  // Returns the header row and toggle button bounds in the sidebar's own
  // coordinate space (caller adds sidebar->bounds().origin() for BrowserView).
  gfx::Rect header_bounds_in_sidebar() const;
  gfx::Rect toggle_button_bounds_in_sidebar() const;

  void SetNewTabHighlighted(bool highlighted);
  void ShowOmniboxPopup();
  void HideOmniboxPopup();

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  // gfx::AnimationDelegate (used only for resize):
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnPaint(gfx::Canvas* canvas) override;

  // views::View (drop target):
  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;

 private:
  raw_ptr<Browser> browser_;
  raw_ptr<views::View> inner_container_ = nullptr;
  raw_ptr<views::View> header_row_ = nullptr;
  raw_ptr<DaoFavoritesView> favorites_ = nullptr;
  raw_ptr<DaoTabListView> tab_list_view_ = nullptr;
  raw_ptr<DaoNewTabButton> new_tab_button_ = nullptr;
  raw_ptr<views::LabelButton> toggle_button_ = nullptr;
  raw_ptr<DaoDownloadButtonView> download_button_ = nullptr;
  raw_ptr<views::ResizeArea> resize_area_ = nullptr;
  raw_ptr<views::View> drop_overlay_ = nullptr;

  void AnimateLayerSlide(int old_width, int new_width);

  bool collapsed_ = false;
  bool auto_expanded_ = false;
  bool is_drop_target_active_ = false;
  bool drop_auto_expanded_ = false;
  bool is_resizing_ = false;
  int user_width_ = kDefaultWidth;
  int current_width_ = kDefaultWidth;
  int start_width_ = kDefaultWidth;
  int target_width_ = kDefaultWidth;
  int resize_start_width_ = kDefaultWidth;
  gfx::LinearAnimation collapse_animation_;

  int drop_target_index_ = -1;   // Tab model index where file will be inserted
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_VIEW_H_
