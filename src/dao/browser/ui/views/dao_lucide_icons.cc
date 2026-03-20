// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_lucide_icons.h"

#include "cc/paint/paint_flags.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"

namespace dao {

namespace {

// All icons are designed on a 24x24 grid.
constexpr float kDesignSize = 24.0f;

cc::PaintFlags MakeStrokeFlags(SkColor color, float scale) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1.5f * scale);
  flags.setStrokeCap(cc::PaintFlags::kRound_Cap);
  flags.setStrokeJoin(cc::PaintFlags::kRound_Join);
  flags.setColor(color);
  return flags;
}

cc::PaintFlags MakeFillFlags(SkColor color) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  return flags;
}

// Helper: parse an SVG path string, scale and translate, then draw.
void DrawSvgPath(gfx::Canvas* canvas,
                 const char* svg,
                 float scale,
                 float ox,
                 float oy,
                 const cc::PaintFlags& flags) {
  SkPath path;
  SkParsePath::FromSVGString(svg, &path);
  SkMatrix matrix;
  matrix.setScale(scale, scale);
  matrix.postTranslate(ox, oy);
  path.transform(matrix);
  canvas->DrawPath(path, flags);
}

// Lucide "plus"
void DrawPlus(gfx::Canvas* canvas,
              float s,
              float ox,
              float oy,
              const cc::PaintFlags& flags) {
  canvas->DrawLine(gfx::PointF(ox + 12 * s, oy + 5 * s),
                   gfx::PointF(ox + 12 * s, oy + 19 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 5 * s, oy + 12 * s),
                   gfx::PointF(ox + 19 * s, oy + 12 * s), flags);
}

// Lucide "settings"
void DrawSettings(gfx::Canvas* canvas,
                  float s,
                  float ox,
                  float oy,
                  const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M9.671 4.136"
              "a2.34 2.34 0 0 1 4.659 0"
              " 2.34 2.34 0 0 0 3.319 1.915"
              " 2.34 2.34 0 0 1 2.33 4.033"
              " 2.34 2.34 0 0 0 0 3.831"
              " 2.34 2.34 0 0 1-2.33 4.033"
              " 2.34 2.34 0 0 0-3.319 1.915"
              " 2.34 2.34 0 0 1-4.659 0"
              " 2.34 2.34 0 0 0-3.32-1.915"
              " 2.34 2.34 0 0 1-2.33-4.033"
              " 2.34 2.34 0 0 0 0-3.831"
              "A2.34 2.34 0 0 1 6.35 6.051"
              "a2.34 2.34 0 0 0 3.319-1.915Z",
              s, ox, oy, flags);
  canvas->DrawCircle(gfx::PointF(ox + 12 * s, oy + 12 * s), 3.0f * s, flags);
}

// Lucide "share"
void DrawShare(gfx::Canvas* canvas,
               float s,
               float ox,
               float oy,
               const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M4 12v8a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-8"
              "M16 6l-4-4-4 4"
              "M12 2v13",
              s, ox, oy, flags);
}

// Lucide "qr-code"
void DrawQrCode(gfx::Canvas* canvas,
                float s,
                float ox,
                float oy,
                const cc::PaintFlags& flags) {
  // Outer rectangles (stroke)
  canvas->DrawRect(gfx::RectF(ox + 3 * s, oy + 3 * s, 7 * s, 7 * s), flags);
  canvas->DrawRect(gfx::RectF(ox + 3 * s, oy + 14 * s, 7 * s, 7 * s), flags);
  canvas->DrawRect(gfx::RectF(ox + 14 * s, oy + 3 * s, 7 * s, 7 * s), flags);
  // Center dots (fill)
  cc::PaintFlags fill = flags;
  fill.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawRect(
      gfx::RectF(ox + 5.5f * s, oy + 5.5f * s, 2 * s, 2 * s), fill);
  canvas->DrawRect(
      gfx::RectF(ox + 5.5f * s, oy + 16.5f * s, 2 * s, 2 * s), fill);
  canvas->DrawRect(
      gfx::RectF(ox + 16.5f * s, oy + 5.5f * s, 2 * s, 2 * s), fill);
  // Bottom-right pattern
  canvas->DrawLine(gfx::PointF(ox + 14 * s, oy + 14 * s),
                   gfx::PointF(ox + 14 * s, oy + 17 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 14 * s, oy + 17 * s),
                   gfx::PointF(ox + 17 * s, oy + 17 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 17 * s, oy + 14 * s),
                   gfx::PointF(ox + 21 * s, oy + 14 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 21 * s, oy + 14 * s),
                   gfx::PointF(ox + 21 * s, oy + 17 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 17 * s, oy + 20 * s),
                   gfx::PointF(ox + 21 * s, oy + 20 * s), flags);
}

// Lucide "shield-check"
void DrawShieldCheck(gfx::Canvas* canvas,
                     float s,
                     float ox,
                     float oy,
                     const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M20 13c0 5-3.5 7.5-8 8.5-4.5-1-8-3.5-8-8.5V5l8-3 8 3Z"
              "M9 12l2 2 4-4",
              s, ox, oy, flags);
}

// Lucide "ellipsis"
void DrawEllipsis(gfx::Canvas* canvas,
                  float s,
                  float ox,
                  float oy,
                  SkColor color) {
  cc::PaintFlags fill = MakeFillFlags(color);
  float r = 1.2f * s;
  canvas->DrawCircle(gfx::PointF(ox + 5 * s, oy + 12 * s), r, fill);
  canvas->DrawCircle(gfx::PointF(ox + 12 * s, oy + 12 * s), r, fill);
  canvas->DrawCircle(gfx::PointF(ox + 19 * s, oy + 12 * s), r, fill);
}

// Lucide "sliders-horizontal"
void DrawSlidersHorizontal(gfx::Canvas* canvas,
                           float s,
                           float ox,
                           float oy,
                           const cc::PaintFlags& flags) {
  // Top line y=8, bottom line y=16
  float y1 = oy + 8 * s;
  float y2 = oy + 16 * s;
  float left = ox + 3 * s;
  float right = ox + 21 * s;
  canvas->DrawLine(gfx::PointF(left, y1), gfx::PointF(right, y1), flags);
  canvas->DrawLine(gfx::PointF(left, y2), gfx::PointF(right, y2), flags);
  // Circle on top line (right side)
  canvas->DrawCircle(gfx::PointF(ox + 15 * s, y1), 2.5f * s, flags);
  // Circle on bottom line (left side)
  canvas->DrawCircle(gfx::PointF(ox + 9 * s, y2), 2.5f * s, flags);
}

// Lucide "volume-2" (speaker with sound waves)
void DrawVolume2(gfx::Canvas* canvas,
                 float s,
                 float ox,
                 float oy,
                 const cc::PaintFlags& flags) {
  // Speaker body: polygon 11,5 6,9 2,9 2,15 6,15 11,19 11,5
  DrawSvgPath(canvas, "M11 5L6 9H2v6h4l5 4V5Z", s, ox, oy, flags);
  // Sound waves
  DrawSvgPath(canvas,
              "M15.54 8.46a5 5 0 0 1 0 7.07",
              s, ox, oy, flags);
  DrawSvgPath(canvas,
              "M19.07 4.93a10 10 0 0 1 0 14.14",
              s, ox, oy, flags);
}

// Lucide "volume-off" (speaker with X)
void DrawVolumeOff(gfx::Canvas* canvas,
                   float s,
                   float ox,
                   float oy,
                   const cc::PaintFlags& flags) {
  // Speaker body
  DrawSvgPath(canvas, "M11 5L6 9H2v6h4l5 4V5Z", s, ox, oy, flags);
  // X mark
  canvas->DrawLine(gfx::PointF(ox + 22 * s, oy + 9 * s),
                   gfx::PointF(ox + 16 * s, oy + 15 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 16 * s, oy + 9 * s),
                   gfx::PointF(ox + 22 * s, oy + 15 * s), flags);
}

}  // namespace

void DrawLucideIcon(gfx::Canvas* canvas,
                    LucideIcon icon,
                    const gfx::RectF& rect,
                    SkColor color) {
  float s = rect.width() / kDesignSize;
  float ox = rect.x();
  float oy = rect.y();
  cc::PaintFlags flags = MakeStrokeFlags(color, s);

  switch (icon) {
    case LucideIcon::kPlus:
      DrawPlus(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kSettings:
      DrawSettings(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kShare:
      DrawShare(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kQrCode:
      DrawQrCode(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kShieldCheck:
      DrawShieldCheck(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kEllipsis:
      DrawEllipsis(canvas, s, ox, oy, color);
      break;
    case LucideIcon::kSlidersHorizontal:
      DrawSlidersHorizontal(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kVolume2:
      DrawVolume2(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kVolumeOff:
      DrawVolumeOff(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kX:
      // Lucide X: two diagonal lines from (18,6)-(6,18) and (6,6)-(18,18)
      canvas->DrawLine(gfx::PointF(ox + 18 * s, oy + 6 * s),
                       gfx::PointF(ox + 6 * s, oy + 18 * s), flags);
      canvas->DrawLine(gfx::PointF(ox + 6 * s, oy + 6 * s),
                       gfx::PointF(ox + 18 * s, oy + 18 * s), flags);
      break;
  }
}

}  // namespace dao
