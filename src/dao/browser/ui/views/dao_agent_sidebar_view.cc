// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_sidebar_view.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
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

  web_view_ = AddChildView(
      std::make_unique<views::WebView>(browser->profile()));
  web_view_->LoadInitialURL(
      GURL(std::string(content::kChromeUIScheme) + "://agent"));
}

DaoAgentSidebarView::~DaoAgentSidebarView() = default;

bool DaoAgentSidebarView::Toggle() {
  bool new_visible = !GetVisible();
  SetVisible(new_visible);
  PreferredSizeChanged();
  return new_visible;
}

gfx::Size DaoAgentSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!GetVisible()) {
    return gfx::Size(0, 0);
  }
  return gfx::Size(kDefaultWidth, 0);
}

}  // namespace dao
