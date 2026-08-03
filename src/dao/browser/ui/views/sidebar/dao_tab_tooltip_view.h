// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_TOOLTIP_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_TOOLTIP_VIEW_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace dao {

// A lightweight tooltip that appears next to the sidebar when hovering a tab.
// Rendered as a BrowserView child so it can extend beyond the sidebar WebView.
class DaoTabTooltipView : public views::View, public ui::NativeThemeObserver {
  METADATA_HEADER(DaoTabTooltipView, views::View)

 public:
  DaoTabTooltipView();
  DaoTabTooltipView(const DaoTabTooltipView&) = delete;
  DaoTabTooltipView& operator=(const DaoTabTooltipView&) = delete;
  ~DaoTabTooltipView() override;

  // Show the tooltip with the given title at the anchor point
  // (in BrowserView coordinates).
  void ShowTooltip(const std::u16string& title, const gfx::Point& anchor);

  // Show the tooltip with a title and up to two detail lines.
  void ShowDetailedTooltip(const std::u16string& title,
                           const std::vector<std::u16string>& details,
                           const gfx::Point& anchor);

  // Hide the tooltip immediately.
  void HideTooltip();

  // The anchor point in parent (BrowserView) coordinates.
  const gfx::Point& anchor_point() const { return anchor_point_; }

  // Returns popup bounds that stay within the available parent area, flipping
  // above the anchor when there is not enough room below it.
  gfx::Rect GetBoundsWithin(const gfx::Rect& available_bounds) const;

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  void ApplyTheme();
  void UpdatePreferredSize();

  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> detail_label_1_ = nullptr;
  raw_ptr<views::Label> detail_label_2_ = nullptr;
  gfx::Point anchor_point_;
  SkColor background_color_ = SK_ColorTRANSPARENT;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_TOOLTIP_VIEW_H_
