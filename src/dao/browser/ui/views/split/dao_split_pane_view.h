// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_PANE_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_PANE_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/view.h"

class Browser;
class ContentsWebView;

namespace content {
class WebContents;
}

namespace dao {

class DaoAddressBarView;
class DaoSplitView;

// A single pane in the split view tree.  Each pane owns its own address bar
// and ContentsWebView, bound to a specific WebContents from TabStripModel.
class DaoSplitPaneView : public views::View,
                         public gfx::AnimationDelegate {
  METADATA_HEADER(DaoSplitPaneView, views::View)

 public:
  DaoSplitPaneView(Browser* browser,
                   DaoSplitView* split_view,
                   int pane_index);
  DaoSplitPaneView(const DaoSplitPaneView&) = delete;
  DaoSplitPaneView& operator=(const DaoSplitPaneView&) = delete;
  ~DaoSplitPaneView() override;

  // Bind this pane to display |web_contents|.
  void SetWebContents(content::WebContents* web_contents);

  // The currently displayed WebContents.
  content::WebContents* web_contents() const { return web_contents_; }

  // Mark this pane as the focused/active one.
  void SetActive(bool active);
  bool is_active() const { return is_active_; }

  // Address bar accessor (for bubble anchoring in split mode).
  DaoAddressBarView* address_bar() const { return address_bar_; }
  DaoSplitView* split_view() const { return split_view_; }

  // Update address bar with current tab's URL.
  void UpdateAddressBar();

  void SetContentsVisible(bool visible);
  void EnsureContentsAttached(bool force_reattach = false);
  void SetHeaderHovered(bool hovered);
  void SetHeaderDragActive(bool active);

  // Detach and return the WebContents WITHOUT calling WasHidden().
  // Used when transferring a visible WebContents to the primary
  // ContentsWebView so the renderer avoids a hidden→shown transition
  // that corrupts the PaintController state.
  content::WebContents* TakeWebContents();

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnBoundsChanged(const gfx::Rect& previous_bounds) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  void OnPaneFocused();
  void AnimateHeaderVisibility(bool visible);

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

  raw_ptr<Browser> browser_;
  raw_ptr<DaoSplitView> split_view_;
  raw_ptr<DaoAddressBarView> address_bar_ = nullptr;
  raw_ptr<ContentsWebView> contents_web_view_ = nullptr;
  raw_ptr<views::View> header_container_ = nullptr;
  raw_ptr<content::WebContents> web_contents_ = nullptr;
  bool is_active_ = false;
  bool contents_visible_ = false;
  bool header_hovered_ = false;
  bool header_drag_active_ = false;
  float glow_opacity_ = 0.0f;
  bool glow_target_active_ = false;
  gfx::LinearAnimation glow_animation_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_PANE_VIEW_H_
