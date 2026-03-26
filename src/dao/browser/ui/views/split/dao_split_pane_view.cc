// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/split/dao_split_pane_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/tab_contents/chrome_web_contents_view_focus_helper.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/base/metadata/metadata_impl_macros.h"

namespace dao {

BEGIN_METADATA(DaoSplitPaneView)
END_METADATA

DaoSplitPaneView::DaoSplitPaneView(Browser* browser,
                                     DaoSplitView* split_view,
                                     int pane_index)
    : browser_(browser), split_view_(split_view) {
  static_cast<void>(pane_index);
  GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);

  // Create per-pane address bar.
  address_bar_ = AddChildView(
      std::make_unique<DaoAddressBarView>(browser));

  // Create the web contents view.
  contents_web_view_ = AddChildView(
      std::make_unique<ContentsWebView>(browser->profile()));
}

DaoSplitPaneView::~DaoSplitPaneView() = default;

void DaoSplitPaneView::SetWebContents(content::WebContents* web_contents) {
  if (web_contents_ == web_contents)
    return;

  if (web_contents_ && contents_visible_) {
    web_contents_->WasHidden();
  }

  web_contents_ = web_contents;
  contents_web_view_->SetWebContents(web_contents);

  if (web_contents && contents_visible_) {
    web_contents->WasShown();
  }

  UpdateAddressBar();
}

void DaoSplitPaneView::SetContentsVisible(bool visible) {
  if (contents_visible_ == visible) {
    return;
  }

  contents_visible_ = visible;
  if (!web_contents_) {
    return;
  }

  if (contents_visible_) {
    web_contents_->WasShown();
  } else {
    web_contents_->WasHidden();
  }
}

void DaoSplitPaneView::SetActive(bool active) {
  if (is_active_ == active)
    return;
  is_active_ = active;

  if (active) {
    SetBorder(views::CreateRoundedRectBorder(
        kActivePaneBorderWidth, kContentCornerRadius, kActivePaneBorder));
  } else {
    SetBorder(nullptr);
  }
  SchedulePaint();
}

void DaoSplitPaneView::UpdateAddressBar() {
  if (!web_contents_ || !address_bar_)
    return;
  // The address bar observes the active tab via TabStripModel, but in split
  // mode each pane's address bar should show its own pane's URL.
  // For now we trigger a repaint; full per-pane URL tracking is handled
  // by the address bar's WebContentsObserver when we wire it up.
}

void DaoSplitPaneView::Layout(PassKey) {
  int addr_height = DaoAddressBarView::kBarHeight;
  int w = width();
  int h = height();

  if (address_bar_) {
    address_bar_->SetBounds(0, 0, w, addr_height);
    address_bar_->SetVisible(true);
  }

  if (contents_web_view_) {
    contents_web_view_->SetBounds(0, addr_height, w,
                                   std::max(0, h - addr_height));
  }
}

bool DaoSplitPaneView::OnMousePressed(const ui::MouseEvent& event) {
  OnPaneFocused();
  return false;  // Let event propagate.
}

void DaoSplitPaneView::OnPaneFocused() {
  if (split_view_) {
    split_view_->SetActivePane(this);
  }
}

}  // namespace dao
