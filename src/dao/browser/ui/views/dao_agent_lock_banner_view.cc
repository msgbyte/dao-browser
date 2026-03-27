// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_lock_banner_view.h"

#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/geometry/rect_f.h"

namespace dao {

namespace {
constexpr int kBannerHeight = 32;
constexpr SkColor kBannerBackground = SkColorSetARGB(200, 50, 42, 58);
constexpr int kIconSize = 16;
constexpr int kHorizontalPadding = 12;
constexpr int kIconTextGap = 8;
}  // namespace

BEGIN_METADATA(DaoAgentLockBannerView)
END_METADATA

DaoAgentLockBannerView::DaoAgentLockBannerView() {
  SetPreferredSize(gfx::Size(0, kBannerHeight));
  SetVisible(false);
  // Purely informational — don't block events.
  SetCanProcessEventsWithinSubtree(false);
}

DaoAgentLockBannerView::~DaoAgentLockBannerView() = default;

void DaoAgentLockBannerView::SetLocked(bool locked) {
  if (GetVisible() == locked) {
    return;
  }
  SetVisible(locked);
}

void DaoAgentLockBannerView::OnPaint(gfx::Canvas* canvas) {
  // Background.
  cc::PaintFlags bg_flags;
  bg_flags.setAntiAlias(true);
  bg_flags.setStyle(cc::PaintFlags::kFill_Style);
  bg_flags.setColor(kBannerBackground);
  canvas->DrawRect(gfx::RectF(GetLocalBounds()), bg_flags);

  float y_center = height() / 2.0f;

  // Bot icon.
  float icon_x = kHorizontalPadding;
  float icon_y = y_center - kIconSize / 2.0f;
  gfx::RectF icon_rect(icon_x, icon_y, kIconSize, kIconSize);
  DrawLucideIcon(canvas, LucideIcon::kBot, icon_rect, dao::kTextPrimary);

  // Text label.
  float text_x = icon_x + kIconSize + kIconTextGap;
  gfx::FontList font({"sans-serif"}, gfx::Font::FontStyle::NORMAL, 13,
                      gfx::Font::Weight::NORMAL);
  std::u16string text = u"AI is operating on this page";
  canvas->DrawStringRectWithFlags(
      text, font, dao::kTextPrimary,
      gfx::Rect(static_cast<int>(text_x),
                 0,
                 width() - static_cast<int>(text_x) - kHorizontalPadding,
                 height()),
      gfx::Canvas::TEXT_ALIGN_LEFT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
}

}  // namespace dao
