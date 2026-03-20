// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_button.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/background.h"

namespace dao {

namespace {
constexpr int kButtonSize = 28;
constexpr int kIconSize = 14;
constexpr int kCornerRadius = 8;
constexpr SkColor kIconColor = SkColorSetARGB(140, 100, 100, 100);

}  // namespace

BEGIN_METADATA(DaoControlCenterButton)
END_METADATA

DaoControlCenterButton::DaoControlCenterButton(Browser* browser)
    : Button(base::BindRepeating(&DaoControlCenterButton::OnClicked,
                                 base::Unretained(this))),
      browser_(browser) {
  SetInstallFocusRingOnFocus(false);
  SetAccessibleName(u"Control Center");

}

DaoControlCenterButton::~DaoControlCenterButton() = default;

gfx::Size DaoControlCenterButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kButtonSize, kButtonSize);
}

void DaoControlCenterButton::PaintButtonContents(gfx::Canvas* canvas) {
  float icon_size = static_cast<float>(kIconSize);
  float ox = (width() - icon_size) / 2.0f;
  float oy = (height() - icon_size) / 2.0f;
  DrawLucideIcon(canvas, LucideIcon::kSlidersHorizontal,
                 gfx::RectF(ox, oy, icon_size, icon_size), kIconColor);
}

void DaoControlCenterButton::OnMouseEntered(const ui::MouseEvent& event) {
  Button::OnMouseEntered(event);
  hovered_ = true;
  UpdateBackground();
}

void DaoControlCenterButton::OnMouseExited(const ui::MouseEvent& event) {
  Button::OnMouseExited(event);
  hovered_ = false;
  UpdateBackground();
}

void DaoControlCenterButton::UpdateBackground() {
  if (hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(20, 0, 0, 0), kCornerRadius));
  } else {
    SetBackground(nullptr);
  }
  SchedulePaint();
}

void DaoControlCenterButton::OnClicked() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  auto* popup = browser_view->dao_control_center_popup();
  if (!popup) {
    return;
  }
  if (popup->GetVisible()) {
    popup->Hide();
  } else {
    // Position relative to this button in BrowserView coordinates
    gfx::Rect button_bounds_in_bv = GetBoundsInScreen();
    gfx::Rect bv_bounds = browser_view->GetBoundsInScreen();
    gfx::Point top_right(
        button_bounds_in_bv.right() - bv_bounds.x(),
        button_bounds_in_bv.bottom() - bv_bounds.y() + 4);
    popup->ShowAt(top_right);
  }
}

}  // namespace dao
