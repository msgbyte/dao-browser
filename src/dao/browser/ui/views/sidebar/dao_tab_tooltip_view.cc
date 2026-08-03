// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_tab_tooltip_view.h"

#include <algorithm>

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
constexpr int kDetailFontSize = 11;
constexpr int kLineGap = 4;
constexpr int kAnchorGap = 8;
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
  title_label_->SetEnabledColor(ToastTextColor());
  title_label_->SetBackgroundColor(SK_ColorTRANSPARENT);
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetMaximumWidthSingleLine(kMaxWidth - 2 * kTooltipPaddingH);

  auto create_detail_label = [this]() {
    auto label = std::make_unique<views::Label>();
    label->SetFontList(gfx::FontList().DeriveWithSizeDelta(
        kDetailFontSize - gfx::FontList().GetFontSize()));
    label->SetEnabledColor(ToastTextColor());
    label->SetBackgroundColor(SK_ColorTRANSPARENT);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetMaximumWidthSingleLine(kMaxWidth - 2 * kTooltipPaddingH);
    label->SetVisible(false);
    return AddChildView(std::move(label));
  };
  detail_label_1_ = create_detail_label();
  detail_label_2_ = create_detail_label();

  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}

DaoTabTooltipView::~DaoTabTooltipView() = default;

void DaoTabTooltipView::ApplyTheme() {
  background_color_ = SkColorSetA(ToastBackground(), 242);
  if (title_label_) {
    title_label_->SetEnabledColor(ToastTextColor());
  }
  if (detail_label_1_) {
    detail_label_1_->SetEnabledColor(ToastTextColor());
  }
  if (detail_label_2_) {
    detail_label_2_->SetEnabledColor(ToastTextColor());
  }
}

void DaoTabTooltipView::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaint();
}

void DaoTabTooltipView::ShowTooltip(const std::u16string& title,
                                    const gfx::Point& anchor) {
  ApplyTheme();
  title_label_->SetMultiLine(false);
  title_label_->SetAllowCharacterBreak(false);
  title_label_->SetMaximumWidthSingleLine(kMaxWidth - 2 * kTooltipPaddingH);
  title_label_->SetText(title);
  detail_label_1_->SetVisible(false);
  detail_label_2_->SetVisible(false);
  anchor_point_ = anchor;

  UpdatePreferredSize();

  SetVisible(true);
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

void DaoTabTooltipView::ShowDetailedTooltip(
    const std::u16string& title,
    const std::vector<std::u16string>& details,
    const gfx::Point& anchor) {
  ApplyTheme();
  title_label_->SetMultiLine(true);
  title_label_->SetAllowCharacterBreak(true);
  title_label_->SetMaximumWidth(kMaxWidth - 2 * kTooltipPaddingH);
  title_label_->SetText(title);
  anchor_point_ = anchor;

  auto set_detail = [&details](views::Label* label, size_t index) {
    const std::u16string detail =
        index < details.size() ? details.at(index) : std::u16string();
    label->SetVisible(!detail.empty());
    label->SetText(detail);
  };
  set_detail(detail_label_1_, 0);
  set_detail(detail_label_2_, 1);

  UpdatePreferredSize();
  if (parent()) {
    anchor_point_ = GetBoundsWithin(parent()->GetLocalBounds()).origin();
  }

  SetVisible(true);
  if (parent()) {
    parent()->InvalidateLayout();
  }
}

gfx::Rect DaoTabTooltipView::GetBoundsWithin(
    const gfx::Rect& available_bounds) const {
  gfx::Rect bounds(anchor_point_, GetPreferredSize());
  if (bounds.bottom() > available_bounds.bottom()) {
    bounds.set_y(anchor_point_.y() - bounds.height() - kAnchorGap);
  }
  bounds.AdjustToFit(available_bounds);
  return bounds;
}

void DaoTabTooltipView::UpdatePreferredSize() {
  views::Label* labels[] = {title_label_, detail_label_1_, detail_label_2_};
  int content_width = 0;
  int content_height = 0;
  int visible_count = 0;
  for (views::Label* label : labels) {
    if (!label->GetVisible()) {
      continue;
    }
    const gfx::Size size = label->GetPreferredSize();
    content_width = std::max(content_width, size.width());
    content_height += size.height();
    ++visible_count;
  }
  if (visible_count > 1) {
    content_height += (visible_count - 1) * kLineGap;
  }

  const int total_width =
      std::min(kMaxWidth, content_width + 2 * kTooltipPaddingH + 4);
  const int total_height = kTooltipPaddingV + content_height + kTooltipPaddingV;
  SetPreferredSize(gfx::Size(total_width, total_height));
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
        {0, 0},            // top-left: sharp
        {radius, radius},  // top-right
        {radius, radius},  // bottom-right
        {radius, radius},  // bottom-left
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
  bg_flags.setColor(background_color_);
  bg_flags.setAntiAlias(true);
  bg_flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawPath(make_path(bounds, kCornerRadius), bg_flags);

  // Position visible labels.
  const int label_x = static_cast<int>(bounds.x()) + kTooltipPaddingH;
  const int label_max_w =
      static_cast<int>(bounds.width()) - 2 * kTooltipPaddingH;
  int y = static_cast<int>(bounds.y()) + kTooltipPaddingV;
  views::Label* labels[] = {title_label_, detail_label_1_, detail_label_2_};
  for (views::Label* label : labels) {
    if (!label->GetVisible()) {
      label->SetBoundsRect(gfx::Rect());
      continue;
    }
    const int label_height = label->GetPreferredSize().height();
    label->SetBoundsRect(gfx::Rect(label_x, y, label_max_w, label_height));
    y += label_height + kLineGap;
  }
}

}  // namespace dao
