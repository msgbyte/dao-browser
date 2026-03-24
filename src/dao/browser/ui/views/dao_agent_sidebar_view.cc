// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_sidebar_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace dao {

BEGIN_METADATA(DaoAgentSidebarView)
END_METADATA

DaoAgentSidebarView::DaoAgentSidebarView(Browser* browser)
    : browser_(browser) {
  SetLayoutManager(std::make_unique<views::FillLayout>());

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
  web_view_->LoadInitialURL(
      GURL(std::string(content::kChromeUIScheme) + "://agent"));

  if (web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(this);
  }
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
  return gfx::Size(expanded_ ? kDefaultWidth : 0, 0);
}

bool DaoAgentSidebarView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

}  // namespace dao
