// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LUCIDE_ICONS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LUCIDE_ICONS_H_

#include "third_party/skia/include/core/SkColor.h"

namespace gfx {
class Canvas;
class RectF;
}  // namespace gfx

namespace dao {

// Identifiers for Lucide icons used throughout the Dao UI.
// Each icon is drawn from the canonical 24x24 Lucide grid,
// scaled to fit the given rect.
enum class LucideIcon {
  kPlus,
  kSettings,
  kShare,
  kQrCode,
  kShieldCheck,
  kEllipsis,
  kSlidersHorizontal,
};

// Draw a Lucide icon into |rect| on |canvas| using |color|.
// The icon is centered and scaled from its 24x24 design space.
// Stroke width is 1.5px (scaled proportionally).
void DrawLucideIcon(gfx::Canvas* canvas,
                    LucideIcon icon,
                    const gfx::RectF& rect,
                    SkColor color);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LUCIDE_ICONS_H_
