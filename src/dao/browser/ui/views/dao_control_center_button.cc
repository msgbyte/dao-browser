// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_button.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/background.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kButtonSize = 28;
constexpr int kIconSize = 14;
constexpr int kCornerRadius = 8;

// Inner view that paints the Lucide "sliders-horizontal" icon.
class SlidersIconView : public views::View {
 public:
  SlidersIconView() {
    SetCanProcessEventsWithinSubtree(false);
    SetPreferredSize(gfx::Size(kIconSize, kIconSize));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.5f);
    flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
    flags.setColor(SkColorSetARGB(140, 100, 100, 100));

    float cx = width() / 2.0f;
    float cy = height() / 2.0f;
    float half = kIconSize / 2.0f;
    float left = cx - half;
    float right = cx + half;

    // Top horizontal line
    float y1 = cy - 3.5f;
    canvas->DrawLine(gfx::PointF(left, y1), gfx::PointF(right, y1), flags);

    // Bottom horizontal line
    float y2 = cy + 3.5f;
    canvas->DrawLine(gfx::PointF(left, y2), gfx::PointF(right, y2), flags);

    // Circle on top line (right side)
    float circle_r = 2.5f;
    canvas->DrawCircle(gfx::PointF(cx + 2.5f, y1), circle_r, flags);

    // Circle on bottom line (left side)
    canvas->DrawCircle(gfx::PointF(cx - 2.5f, y2), circle_r, flags);
  }
};

}  // namespace

BEGIN_METADATA(DaoControlCenterButton)
END_METADATA

DaoControlCenterButton::DaoControlCenterButton(Browser* browser)
    : Button(base::BindRepeating(&DaoControlCenterButton::OnClicked,
                                 base::Unretained(this))),
      browser_(browser) {
  SetInstallFocusRingOnFocus(false);
  SetAccessibleName(u"Control Center");

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  AddChildView(static_cast<views::View*>(
      std::make_unique<SlidersIconView>().release()));
}

DaoControlCenterButton::~DaoControlCenterButton() = default;

gfx::Size DaoControlCenterButton::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(kButtonSize, kButtonSize);
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
