// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_sidebar_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/tween.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace dao {

BEGIN_METADATA(DaoAgentSidebarView)
END_METADATA

DaoAgentSidebarView::DaoAgentSidebarView(Browser* browser)
    : browser_(browser),
      animation_(base::Milliseconds(150), 60, this) {
  SetLayoutManager(std::make_unique<views::FillLayout>());

  web_view_ = AddChildView(
      std::make_unique<views::WebView>(browser->profile()));
  web_view_->LoadInitialURL(
      GURL(std::string(content::kChromeUIScheme) + "://agent"));

  // Set this view as the WebContentsDelegate so that unhandled keyboard
  // events (Cmd+C, Cmd+V, Cmd+A, etc.) are forwarded to the focus manager.
  if (web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(this);
  }
}

DaoAgentSidebarView::~DaoAgentSidebarView() {
  // Clear the delegate to avoid dangling pointer.
  if (web_view_ && web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(nullptr);
  }
}

bool DaoAgentSidebarView::Toggle() {
  expanded_ = !expanded_;
  start_width_ = current_width_;
  target_width_ = expanded_ ? kDefaultWidth : 0;

  // Ensure visible during animation so it paints while collapsing.
  SetVisible(true);

  animation_.Stop();
  animation_.Start();

  return expanded_;
}

gfx::Size DaoAgentSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(current_width_, 0);
}

void DaoAgentSidebarView::AnimationProgressed(
    const gfx::Animation* animation) {
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                         animation->GetCurrentValue());
  current_width_ =
      start_width_ + static_cast<int>((target_width_ - start_width_) * t);
  PreferredSizeChanged();
}

void DaoAgentSidebarView::AnimationEnded(const gfx::Animation* animation) {
  current_width_ = target_width_;
  if (!expanded_) {
    SetVisible(false);
  }
  PreferredSizeChanged();
}

bool DaoAgentSidebarView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

}  // namespace dao
