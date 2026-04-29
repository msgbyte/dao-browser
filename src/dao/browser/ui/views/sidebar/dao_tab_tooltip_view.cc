// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_tab_tooltip_view.h"

#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/controls/label.h"

namespace dao {

namespace {
constexpr int kTooltipPaddingH = 10;
constexpr int kTooltipPaddingV = 6;
constexpr int kTitleFontSize = 12;
constexpr int kMaxWidth = 320;
constexpr float kCornerRadius = 8.0f;
}  // namespace

BEGIN_METADATA(DaoTabTooltipView)
END_METADATA

DaoTabTooltipView::DaoTabTooltipView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);
  SetCanProcessEventsWithinSubtree(false);

  title_label_ = AddChildView(std::make_unique<views::Label>());
  title_label_->SetFontList(
      gfx::FontList()
          .DeriveWithSizeDelta(kTitleFontSize - gfx::FontList().GetFontSize())
          .DeriveWithWeight(gfx::Font::Weight::MEDIUM));
  title_label_->SetEnabledColor(SkColorSetA(TextPrimary(), 217));
  title_label_->SetBackgroundColor(SK_ColorTRANSPARENT);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetMaximumWidthSingleLine(kMaxWidth - 2 * kTooltipPaddingH);
}

DaoTabTooltipView::~DaoTabTooltipView() = default;

void DaoTabTooltipView::ShowTooltip(const std::u16string& title,
                                     const gfx::Point& anchor) {
  title_label_->SetText(title);
  anchor_point_ = anchor;

  // Calculate preferred size.
  gfx::Size title_size = title_label_->GetPreferredSize();
  int total_width =
      std::min(kMaxWidth, title_size.width() + 2 * kTooltipPaddingH + 4);
  int total_height = kTooltipPaddingV + title_size.height() + kTooltipPaddingV;
  SetPreferredSize(gfx::Size(total_width, total_height));

  SetVisible(true);
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void DaoTabTooltipView::HideTooltip() {
  SetVisible(false);
}

void DaoTabTooltipView::OnPaint(gfx::Canvas* canvas) {
  gfx::RectF bounds(GetLocalBounds());
  bounds.Inset(gfx::InsetsF(2));

  // Helper to create a path with per-corner radii.
  // Top-left is 0 (sharp corner pointing at cursor), others are rounded.
  auto make_path = [](const gfx::RectF& r, float radius) {
    // radii: top-left, top-right, bottom-right, bottom-left
    const SkVector radii[4] = {
        {0, 0},               // top-left: sharp
        {radius, radius},     // top-right
        {radius, radius},     // bottom-right
        {radius, radius},     // bottom-left
    };
    return SkPathBuilder()
        .addRRect(SkRRect::MakeRectRadii(
            SkRect::MakeXYWH(r.x(), r.y(), r.width(), r.height()), radii))
        .detach();
  };

  // Draw shadow rings.
  constexpr int kShadowSteps = 3;
  for (int i = kShadowSteps; i >= 1; --i) {
    float expand = i * 1.0f;
    float alpha = 15.0f * (kShadowSteps - i + 1) / kShadowSteps;
    gfx::RectF shadow_rect(bounds.x() - expand, bounds.y() - expand + 0.5f,
                            bounds.width() + 2 * expand,
                            bounds.height() + 2 * expand);
    cc::PaintFlags shadow_flags;
    shadow_flags.setAntiAlias(true);
    shadow_flags.setStyle(cc::PaintFlags::kFill_Style);
    shadow_flags.setColor(
        SkColorSetARGB(static_cast<int>(alpha), 0, 0, 0));  // theme-independent
    canvas->DrawPath(make_path(shadow_rect, kCornerRadius + expand),
                     shadow_flags);
  }

  // Draw background.
  cc::PaintFlags bg_flags;
  bg_flags.setColor(SkColorSetA(ToastBackground(), 242));
  bg_flags.setAntiAlias(true);
  bg_flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawPath(make_path(bounds, kCornerRadius), bg_flags);

  // Position label.
  gfx::Size title_size = title_label_->GetPreferredSize();
  int label_x = static_cast<int>(bounds.x()) + kTooltipPaddingH;
  int label_max_w = static_cast<int>(bounds.width()) - 2 * kTooltipPaddingH;
  int y = static_cast<int>(bounds.y()) + kTooltipPaddingV;

  title_label_->SetBoundsRect(
      gfx::Rect(label_x, y, label_max_w, title_size.height()));
}

}  // namespace dao
