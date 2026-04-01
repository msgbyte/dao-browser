// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_sidebar_view.h"

#include <algorithm>

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/web_contents_set_background_color.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace dao {

BEGIN_METADATA(DaoAgentSidebarView)
END_METADATA

DaoAgentSidebarView::DaoAgentSidebarView(Browser* browser)
    : browser_(browser) {
  SetBackground(views::CreateSolidBackground(dao::kSidebarBackground));

  // Resize handle on the left edge (drag to resize).
  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));

  // WebView is created but NOT loaded until the first Toggle() expansion.
  web_view_ = AddChildView(
      std::make_unique<views::WebView>(browser->profile()));
}

DaoAgentSidebarView::~DaoAgentSidebarView() {
  if (web_view_ && web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(nullptr);
  }
}

void DaoAgentSidebarView::EnsureLoaded() {
  if (loaded_) {
    return;
  }

  loaded_ = true;

  if (content::WebContents* web_contents = web_view_->GetWebContents()) {
    web_contents->SetPageBaseBackgroundColor(dao::kSidebarBackground);
    views::WebContentsSetBackgroundColor::CreateForWebContentsWithColor(
        web_contents, dao::kSidebarBackground);
    if (content::RenderWidgetHostView* host_view =
            web_contents->GetRenderWidgetHostView()) {
      host_view->SetBackgroundColor(dao::kSidebarBackground);
    }
    web_contents->SetDelegate(this);
  }

  web_view_->LoadInitialURL(
      GURL(std::string(content::kChromeUIScheme) + "://agent"));
}

bool DaoAgentSidebarView::Toggle() {
  expanded_ = !expanded_;

  if (expanded_) {
    EnsureLoaded();
    SetVisible(true);
  } else {
    SetVisible(false);
  }

  // Single layout pass — web content repaints exactly once.
  PreferredSizeChanged();

  return expanded_;
}

gfx::Size DaoAgentSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(expanded_ ? current_width_ : 0, 0);
}

void DaoAgentSidebarView::Layout(PassKey) {
  // Position the resize area on the left edge.
  if (resize_area_) {
    resize_area_->SetBoundsRect(
        gfx::Rect(0, 0, kResizeAreaWidth, height()));
  }

  // WebView fills the remaining area after the resize handle.
  if (web_view_) {
    web_view_->SetBoundsRect(
        gfx::Rect(kResizeAreaWidth, 0,
                   std::max(0, width() - kResizeAreaWidth), height()));
  }
}

bool DaoAgentSidebarView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

void DaoAgentSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (!expanded_) {
    return;
  }

  if (!is_resizing_) {
    is_resizing_ = true;
    resize_start_width_ = current_width_;
  }

  // Left-edge drag: dragging left (negative resize_amount) should increase
  // width, dragging right should decrease it.
  int new_width = resize_start_width_ - resize_amount;
  new_width = std::clamp(new_width, kMinWidth, kMaxWidth);
  current_width_ = new_width;

  if (done_resizing) {
    is_resizing_ = false;
    user_width_ = new_width;
  }

  PreferredSizeChanged();
}

}  // namespace dao
