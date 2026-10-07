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
constexpr int kPreviewWidth = 240;
constexpr int kPreviewMaxHeight = 160;
constexpr float kPreviewCornerRadius = 6.0f;
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
                                    const gfx::Point& anchor,
                                    const gfx::ImageSkia& preview) {
  ApplyTheme();
  preview_ = preview;
  // A preview card stays as wide as its snapshot and wraps the title to two
  // lines before eliding; plain tooltips keep a single elided line.
  const bool has_preview = !preview_.isNull();
  title_label_->SetMultiLine(has_preview);
  title_label_->SetMaxLines(has_preview ? 2 : 0);
  title_label_->SetAllowCharacterBreak(has_preview);
  if (has_preview) {
    title_label_->SetMaximumWidth(kPreviewWidth);
  } else {
    title_label_->SetMaximumWidthSingleLine(kMaxWidth - 2 * kTooltipPaddingH);
  }
  title_label_->SetText(title);
  detail_label_1_->SetVisible(false);
  detail_label_2_->SetVisible(false);
  anchor_point_ = anchor;

  UpdatePreferredSize();
  if (parent()) {
    anchor_point_ = GetBoundsWithin(parent()->GetLocalBounds()).origin();
  }

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
  preview_ = gfx::ImageSkia();
  title_label_->SetMultiLine(true);
  title_label_->SetMaxLines(0);
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
  // A preview sits beside the sidebar rather than under the cursor, so it only
  // slides back into view instead of flipping above its tab.
  if (preview_.isNull() && bounds.bottom() > available_bounds.bottom()) {
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
  if (!preview_.isNull()) {
    const gfx::Size preview_size = GetPreviewSize();
    content_width = std::max(content_width, preview_size.width());
    content_height += preview_size.height() + kLineGap;
  }

  const int total_width =
      std::min(kMaxWidth, content_width + 2 * kTooltipPaddingH + 4);
  const int total_height = kTooltipPaddingV + content_height + kTooltipPaddingV;
  SetPreferredSize(gfx::Size(total_width, total_height));
}

gfx::Size DaoTabTooltipView::GetPreviewSize() const {
  if (preview_.isNull()) {
    return gfx::Size();
  }
  return gfx::Size(kPreviewWidth,
                   std::min(kPreviewMaxHeight,
                            kPreviewWidth * preview_.height() /
                                std::max(1, preview_.width())));
}

void DaoTabTooltipView::HideTooltip() {
  SetVisible(false);
  preview_ = gfx::ImageSkia();
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
  // Preview cards sit over page content, so they stay fully opaque.
  bg_flags.setColor(preview_.isNull()
                        ? background_color_
                        : SkColorSetA(background_color_, SK_AlphaOPAQUE));
  bg_flags.setAntiAlias(true);
  bg_flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawPath(make_path(bounds, kCornerRadius), bg_flags);

  // Position visible labels.
  const int label_x = static_cast<int>(bounds.x()) + kTooltipPaddingH;
  const int label_max_w =
      static_cast<int>(bounds.width()) - 2 * kTooltipPaddingH;
  int y = static_cast<int>(bounds.y()) + kTooltipPaddingV;
  if (!preview_.isNull()) {
    const gfx::Rect preview_rect(gfx::Point(label_x, y), GetPreviewSize());
    // Keep the top of the page; crop anything taller than the preview slot.
    const int source_height =
        std::min(preview_.height(), preview_.width() * preview_rect.height() /
                                        preview_rect.width());
    canvas->Save();
    canvas->ClipPath(
        SkPathBuilder()
            .addRRect(SkRRect::MakeRectXY(
                SkRect::MakeXYWH(preview_rect.x(), preview_rect.y(),
                                 preview_rect.width(), preview_rect.height()),
                kPreviewCornerRadius, kPreviewCornerRadius))
            .detach(),
        /*do_anti_alias=*/true);
    canvas->DrawImageInt(preview_, 0, 0, preview_.width(), source_height,
                         preview_rect.x(), preview_rect.y(),
                         preview_rect.width(), preview_rect.height(),
                         /*filter=*/true);
    canvas->Restore();
    y += preview_rect.height() + kLineGap;
  }
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
