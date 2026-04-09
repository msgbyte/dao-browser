// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_TOOLTIP_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_TOOLTIP_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace dao {

// A lightweight tooltip that appears next to the sidebar when hovering a tab.
// Rendered as a BrowserView child so it can extend beyond the sidebar WebView.
class DaoTabTooltipView : public views::View {
  METADATA_HEADER(DaoTabTooltipView, views::View)

 public:
  DaoTabTooltipView();
  DaoTabTooltipView(const DaoTabTooltipView&) = delete;
  DaoTabTooltipView& operator=(const DaoTabTooltipView&) = delete;
  ~DaoTabTooltipView() override;

  // Show the tooltip with the given title at the anchor point
  // (in BrowserView coordinates).
  void ShowTooltip(const std::u16string& title, const gfx::Point& anchor);

  // Hide the tooltip immediately.
  void HideTooltip();

  // The anchor point in parent (BrowserView) coordinates.
  const gfx::Point& anchor_point() const { return anchor_point_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  raw_ptr<views::Label> title_label_ = nullptr;
  gfx::Point anchor_point_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_TOOLTIP_VIEW_H_
