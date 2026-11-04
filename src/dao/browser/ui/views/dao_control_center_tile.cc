// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_tile.h"

#include <algorithm>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {

constexpr int kActionTileHeight = 56;
constexpr int kActionTileTopInset = 10;
constexpr int kActionTileInsetH = 4;
constexpr int kActionIconSize = 18;
constexpr int kActionLabelGap = 6;
constexpr int kLabelFontSize = 11;

}  // namespace

BEGIN_METADATA(DaoControlCenterTileButton)
END_METADATA

DaoControlCenterTileButton::DaoControlCenterTileButton(PressedCallback callback)
    : Button(std::move(callback)) {
  SetInstallFocusRingOnFocus(false);
}

void DaoControlCenterTileButton::ApplyTheme() {}

void DaoControlCenterTileButton::SetVisualEnabled(bool enabled) {
  visually_enabled_ = enabled;
  if (!enabled) {
    SetBackground(nullptr);
  }
  SchedulePaint();
}

void DaoControlCenterTileButton::OnMouseEntered(const ui::MouseEvent& event) {
  Button::OnMouseEntered(event);
  if (visually_enabled_) {
    SetBackground(views::CreateRoundedRectBackground(
        SuggestionHover(), DaoControlCenterPopup::kTileCornerRadius));
    SchedulePaint();
  }
}

void DaoControlCenterTileButton::OnMouseExited(const ui::MouseEvent& event) {
  Button::OnMouseExited(event);
  SetBackground(nullptr);
  SchedulePaint();
}

void DaoControlCenterTileButton::OnPaintBackground(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(ControlCenterTileBackground());
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                        DaoControlCenterPopup::kTileCornerRadius, flags);
  // The hover tint, when set as the background, paints over the surface.
  Button::OnPaintBackground(canvas);
}

BEGIN_METADATA(DaoControlCenterActionTile)
END_METADATA

DaoControlCenterActionTile::DaoControlCenterActionTile(
    const std::u16string& label_text,
    LucideIcon icon,
    PressedCallback callback)
    : DaoControlCenterTileButton(std::move(callback)), icon_(icon) {
  label_ = AddChildView(std::make_unique<views::Label>(label_text));
  label_->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL,
                                    kLabelFontSize, gfx::Font::Weight::NORMAL));
  label_->SetElideBehavior(gfx::ELIDE_TAIL);
  label_->SetSkipSubpixelRenderingOpacityCheck(true);
  label_->SetCanProcessEventsWithinSubtree(false);
  label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

  SetPreferredSize(gfx::Size(0, kActionTileHeight));
  SetAccessibleName(label_text);
  ApplyTheme();
}

void DaoControlCenterActionTile::ApplyTheme() {
  label_->SetEnabledColor(ControlCenterTileLabelColor());
}

void DaoControlCenterActionTile::Layout(PassKey) {
  label_->SetBounds(kActionTileInsetH,
                    kActionTileTopInset + kActionIconSize + kActionLabelGap,
                    std::max(0, width() - 2 * kActionTileInsetH),
                    label_->font_list().GetHeight());
}

void DaoControlCenterActionTile::PaintButtonContents(gfx::Canvas* canvas) {
  const gfx::RectF icon_rect((width() - kActionIconSize) / 2.0f,
                             kActionTileTopInset, kActionIconSize,
                             kActionIconSize);
  DrawLucideIcon(canvas, icon_, icon_rect, ControlCenterTileIconColor());
}

std::unique_ptr<views::View> CreateControlCenterTileRow() {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      DaoControlCenterPopup::kTileSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  // Tiles report a zero preferred width, so equal flex splits the row evenly.
  layout->SetDefaultFlex(1);
  return row;
}

}  // namespace dao
