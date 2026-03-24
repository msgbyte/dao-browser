// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_AGENT_SIDEBAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_AGENT_SIDEBAR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class WebView;
}

namespace dao {

class DaoAgentSidebarView : public views::View,
                            public content::WebContentsDelegate {
  METADATA_HEADER(DaoAgentSidebarView, views::View)

 public:
  static constexpr int kDefaultWidth = 360;

  explicit DaoAgentSidebarView(Browser* browser);
  ~DaoAgentSidebarView() override;

  // Toggle visibility; returns new expanded state.
  bool Toggle();

  bool is_expanded() const { return expanded_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;

 private:
  void EnsureLoaded();

  raw_ptr<Browser> browser_;
  raw_ptr<views::WebView> web_view_ = nullptr;

  bool loaded_ = false;
  bool expanded_ = false;
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_AGENT_SIDEBAR_VIEW_H_
