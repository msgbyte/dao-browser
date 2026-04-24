// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_corner_overlay_view.h"

#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkBlendMode.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"

namespace dao {

BEGIN_METADATA(DaoCornerOverlayView)
END_METADATA

DaoCornerOverlayView::DaoCornerOverlayView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
}

DaoCornerOverlayView::~DaoCornerOverlayView() = default;

void DaoCornerOverlayView::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  SchedulePaint();
}

void DaoCornerOverlayView::OnPaint(gfx::Canvas* canvas) {
  const int m = kContentShadowMargin;
  const int r = kContentCornerRadius;

  // Simulate a soft drop shadow by painting multiple expanding rings
  // with decreasing opacity around the content area.
  constexpr int kShadowSteps = 6;
  for (int i = kShadowSteps; i >= 1; --i) {
    float expand = static_cast<float>(i) * 1.2f;
    float alpha =
        CornerShadowAlphaBase() * (kShadowSteps - i + 1) / kShadowSteps;
    gfx::RectF shadow_rect(m - expand, m - expand + 1.0f,
                           width() - 2 * m + 2 * expand,
                           height() - 2 * m + 2 * expand);
    cc::PaintFlags flags;
    flags.setColor(SkColorSetARGB(static_cast<int>(alpha), 0, 0,
                                  0));  // theme-independent
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(shadow_rect, r + expand, flags);
  }

  // Clear the content area so the shadow rings don't dim the web content.
  // The overlay has its own transparent layer, so kClear erases to
  // transparent — letting the content underneath show through unaffected.
  gfx::RectF content_rect(m, m, width() - 2 * m, height() - 2 * m);
  cc::PaintFlags clear_flags;
  clear_flags.setBlendMode(SkBlendMode::kClear);
  clear_flags.setAntiAlias(true);
  canvas->DrawRoundRect(content_rect, r, clear_flags);
}

}  // namespace dao
