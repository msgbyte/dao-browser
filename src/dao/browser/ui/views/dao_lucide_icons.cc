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
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/canvas_image_source.h"

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
              "a2.34 2.34 0 0 0 3.319-1.915",
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
  // Top-left rounded rect 5x5 at (3,3) rx=1
  DrawSvgPath(canvas,
              "M4 3h3a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1H4"
              "a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Z",
              s, ox, oy, flags);
  // Top-right rounded rect 5x5 at (16,3) rx=1
  DrawSvgPath(canvas,
              "M17 3h3a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1h-3"
              "a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1Z",
              s, ox, oy, flags);
  // Bottom-left rounded rect 5x5 at (3,16) rx=1
  DrawSvgPath(canvas,
              "M4 16h3a1 1 0 0 1 1 1v3a1 1 0 0 1-1 1H4"
              "a1 1 0 0 1-1-1v-3a1 1 0 0 1 1-1Z",
              s, ox, oy, flags);
  // Bottom-right curved path
  DrawSvgPath(canvas, "M21 16h-3a2 2 0 0 0-2 2v3", s, ox, oy, flags);
  // Data pattern dots and lines
  DrawSvgPath(canvas, "M21 21v.01", s, ox, oy, flags);
  DrawSvgPath(canvas, "M12 7v3a2 2 0 0 1-2 2H7", s, ox, oy, flags);
  DrawSvgPath(canvas, "M3 12h.01", s, ox, oy, flags);
  DrawSvgPath(canvas, "M12 3h.01", s, ox, oy, flags);
  DrawSvgPath(canvas, "M12 16v.01", s, ox, oy, flags);
  DrawSvgPath(canvas, "M16 12h1", s, ox, oy, flags);
  DrawSvgPath(canvas, "M21 12v.01", s, ox, oy, flags);
  DrawSvgPath(canvas, "M12 21v-1", s, ox, oy, flags);
}

// Lucide "shield-check"
void DrawShieldCheck(gfx::Canvas* canvas,
                     float s,
                     float ox,
                     float oy,
                     const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01"
              "C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72"
              "a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z",
              s, ox, oy, flags);
  DrawSvgPath(canvas, "M9 12l2 2 4-4", s, ox, oy, flags);
}

// Lucide "ellipsis"
void DrawEllipsis(gfx::Canvas* canvas,
                  float s,
                  float ox,
                  float oy,
                  const cc::PaintFlags& flags) {
  canvas->DrawCircle(gfx::PointF(ox + 12 * s, oy + 12 * s), 1.0f * s, flags);
  canvas->DrawCircle(gfx::PointF(ox + 19 * s, oy + 12 * s), 1.0f * s, flags);
  canvas->DrawCircle(gfx::PointF(ox + 5 * s, oy + 12 * s), 1.0f * s, flags);
}

// Lucide "sliders-horizontal"
void DrawSlidersHorizontal(gfx::Canvas* canvas,
                           float s,
                           float ox,
                           float oy,
                           const cc::PaintFlags& flags) {
  // Top slider (y=5), handle at x=14
  DrawSvgPath(canvas, "M10 5H3", s, ox, oy, flags);
  DrawSvgPath(canvas, "M21 5h-7", s, ox, oy, flags);
  DrawSvgPath(canvas, "M14 3v4", s, ox, oy, flags);
  // Middle slider (y=12), handle at x=8
  DrawSvgPath(canvas, "M8 12H3", s, ox, oy, flags);
  DrawSvgPath(canvas, "M21 12h-9", s, ox, oy, flags);
  DrawSvgPath(canvas, "M8 10v4", s, ox, oy, flags);
  // Bottom slider (y=19), handle at x=16
  DrawSvgPath(canvas, "M12 19H3", s, ox, oy, flags);
  DrawSvgPath(canvas, "M21 19h-5", s, ox, oy, flags);
  DrawSvgPath(canvas, "M16 17v4", s, ox, oy, flags);
}

// Lucide "volume-2" (speaker with sound waves)
void DrawVolume2(gfx::Canvas* canvas,
                 float s,
                 float ox,
                 float oy,
                 const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587"
              "A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6"
              "a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413"
              "l3.383 3.384A.705.705 0 0 0 11 19.298z",
              s, ox, oy, flags);
  DrawSvgPath(canvas, "M16 9a5 5 0 0 1 0 6", s, ox, oy, flags);
  DrawSvgPath(canvas, "M19.364 18.364a9 9 0 0 0 0-12.728",
              s, ox, oy, flags);
}

// Lucide "volume-x" (speaker with X)
void DrawVolumeX(gfx::Canvas* canvas,
                 float s,
                 float ox,
                 float oy,
                 const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M11 4.702a.705.705 0 0 0-1.203-.498L6.413 7.587"
              "A1.4 1.4 0 0 1 5.416 8H3a1 1 0 0 0-1 1v6"
              "a1 1 0 0 0 1 1h2.416a1.4 1.4 0 0 1 .997.413"
              "l3.383 3.384A.705.705 0 0 0 11 19.298z",
              s, ox, oy, flags);
  canvas->DrawLine(gfx::PointF(ox + 22 * s, oy + 9 * s),
                   gfx::PointF(ox + 16 * s, oy + 15 * s), flags);
  canvas->DrawLine(gfx::PointF(ox + 16 * s, oy + 9 * s),
                   gfx::PointF(ox + 22 * s, oy + 15 * s), flags);
}

// Lucide "panel-left-close"
void DrawPanelLeftClose(gfx::Canvas* canvas,
                        float s,
                        float ox,
                        float oy,
                        const cc::PaintFlags& flags) {
  SkPath rect_path;
  rect_path.addRoundRect(SkRect::MakeXYWH(3, 3, 18, 18), 2, 2);
  SkMatrix matrix;
  matrix.setScale(s, s);
  matrix.postTranslate(ox, oy);
  rect_path.transform(matrix);
  canvas->DrawPath(rect_path, flags);
  canvas->DrawLine(gfx::PointF(ox + 9 * s, oy + 3 * s),
                   gfx::PointF(ox + 9 * s, oy + 21 * s), flags);
  DrawSvgPath(canvas, "M16 15l-3-3 3-3", s, ox, oy, flags);
}

// Lucide "panel-left-open"
void DrawPanelLeftOpen(gfx::Canvas* canvas,
                       float s,
                       float ox,
                       float oy,
                       const cc::PaintFlags& flags) {
  SkPath rect_path;
  rect_path.addRoundRect(SkRect::MakeXYWH(3, 3, 18, 18), 2, 2);
  SkMatrix matrix;
  matrix.setScale(s, s);
  matrix.postTranslate(ox, oy);
  rect_path.transform(matrix);
  canvas->DrawPath(rect_path, flags);
  canvas->DrawLine(gfx::PointF(ox + 9 * s, oy + 3 * s),
                   gfx::PointF(ox + 9 * s, oy + 21 * s), flags);
  DrawSvgPath(canvas, "M14 9l3 3-3 3", s, ox, oy, flags);
}

// Lucide "grip-horizontal" (6 dots in a 3x2 grid — drag handle)
void DrawGripHorizontal(gfx::Canvas* canvas,
                        float s,
                        float ox,
                        float oy,
                        const cc::PaintFlags& flags) {
  cc::PaintFlags fill_flags = flags;
  fill_flags.setStyle(cc::PaintFlags::kFill_Style);
  canvas->DrawCircle(gfx::PointF(ox + 12 * s, oy + 9 * s), 1.0f * s, fill_flags);
  canvas->DrawCircle(gfx::PointF(ox + 19 * s, oy + 9 * s), 1.0f * s, fill_flags);
  canvas->DrawCircle(gfx::PointF(ox + 5 * s, oy + 9 * s), 1.0f * s, fill_flags);
  canvas->DrawCircle(gfx::PointF(ox + 12 * s, oy + 15 * s), 1.0f * s, fill_flags);
  canvas->DrawCircle(gfx::PointF(ox + 19 * s, oy + 15 * s), 1.0f * s, fill_flags);
  canvas->DrawCircle(gfx::PointF(ox + 5 * s, oy + 15 * s), 1.0f * s, fill_flags);
}

// Lucide "external-link" (arrow pointing out of a box)
void DrawExternalLink(gfx::Canvas* canvas,
                      float s,
                      float ox,
                      float oy,
                      const cc::PaintFlags& flags) {
  DrawSvgPath(canvas, "M15 3h6v6", s, ox, oy, flags);
  DrawSvgPath(canvas, "M10 14 21 3", s, ox, oy, flags);
  DrawSvgPath(canvas, "M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6",
              s, ox, oy, flags);
}

// Lucide "square-arrow-down-left" (arrow into corner of a square)
void DrawSquareArrowDownLeft(gfx::Canvas* canvas,
                             float s,
                             float ox,
                             float oy,
                             const cc::PaintFlags& flags) {
  // Rounded rect: x=3 y=3 w=18 h=18 rx=2
  SkPath rect;
  rect.addRoundRect(SkRect::MakeXYWH(3, 3, 18, 18), 2, 2);
  SkMatrix matrix;
  matrix.setScale(s, s);
  matrix.postTranslate(ox, oy);
  rect.transform(matrix);
  canvas->DrawPath(rect, flags);
  // Diagonal arrow: M16 8 L8 16
  DrawSvgPath(canvas, "M16 8 8 16", s, ox, oy, flags);
  // Arrow head: M16 16 H8 V8
  DrawSvgPath(canvas, "M16 16H8V8", s, ox, oy, flags);
}

// Lucide "bot" (robot face)
void DrawBot(gfx::Canvas* canvas,
             float s,
             float ox,
             float oy,
             const cc::PaintFlags& flags) {
  // Head: rounded rect x=4 y=8 w=16 h=12 rx=2
  SkPath head;
  head.addRoundRect(SkRect::MakeXYWH(4, 8, 16, 12), 2, 2);
  SkMatrix matrix;
  matrix.setScale(s, s);
  matrix.postTranslate(ox, oy);
  head.transform(matrix);
  canvas->DrawPath(head, flags);
  // Left ear
  DrawSvgPath(canvas, "M2 14h2", s, ox, oy, flags);
  // Right ear
  DrawSvgPath(canvas, "M20 14h2", s, ox, oy, flags);
  // Left eye
  DrawSvgPath(canvas, "M9 13v2", s, ox, oy, flags);
  // Right eye
  DrawSvgPath(canvas, "M15 13v2", s, ox, oy, flags);
  // Antenna stem
  canvas->DrawLine(gfx::PointF(ox + 12 * s, oy + 8 * s),
                   gfx::PointF(ox + 12 * s, oy + 4 * s), flags);
  // Antenna ball
  canvas->DrawCircle(gfx::PointF(ox + 12 * s, oy + 4 * s), 1.0f * s, flags);
}

// Lucide "sparkles" — a large 4-pointed sparkle with a small plus-cross
// and a filled dot decoration.
void DrawSparkles(gfx::Canvas* canvas,
                  float s,
                  float ox,
                  float oy,
                  const cc::PaintFlags& flags) {
  DrawSvgPath(canvas,
              "M11.017 2.814a1 1 0 0 1 1.966 0"
              "l1.051 5.558a2 2 0 0 0 1.594 1.594"
              "l5.558 1.051a1 1 0 0 1 0 1.966"
              "l-5.558 1.051a2 2 0 0 0-1.594 1.594"
              "l-1.051 5.558a1 1 0 0 1-1.966 0"
              "l-1.051-5.558a2 2 0 0 0-1.594-1.594"
              "l-5.558-1.051a1 1 0 0 1 0-1.966"
              "l5.558-1.051a2 2 0 0 0 1.594-1.594z",
              s, ox, oy, flags);
  DrawSvgPath(canvas, "M20 2v4", s, ox, oy, flags);
  DrawSvgPath(canvas, "M22 4h-4", s, ox, oy, flags);
  canvas->DrawCircle(gfx::PointF(ox + 4 * s, oy + 20 * s), 2.0f * s, flags);
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
      DrawEllipsis(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kSlidersHorizontal:
      DrawSlidersHorizontal(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kVolume2:
      DrawVolume2(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kVolumeX:
      DrawVolumeX(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kX:
      // Lucide X: two diagonal lines from (18,6)-(6,18) and (6,6)-(18,18)
      canvas->DrawLine(gfx::PointF(ox + 18 * s, oy + 6 * s),
                       gfx::PointF(ox + 6 * s, oy + 18 * s), flags);
      canvas->DrawLine(gfx::PointF(ox + 6 * s, oy + 6 * s),
                       gfx::PointF(ox + 18 * s, oy + 18 * s), flags);
      break;
    case LucideIcon::kChevronLeft:
      // Lucide chevron-left: polyline (15,18)-(9,12)-(15,6)
      DrawSvgPath(canvas, "M15 18l-6-6 6-6", s, ox, oy, flags);
      break;
    case LucideIcon::kChevronRight:
      // Lucide chevron-right: polyline (9,18)-(15,12)-(9,6)
      DrawSvgPath(canvas, "M9 18l6-6-6-6", s, ox, oy, flags);
      break;
    case LucideIcon::kArrowLeft:
      DrawSvgPath(canvas, "M12 19l-7-7 7-7", s, ox, oy, flags);
      canvas->DrawLine(gfx::PointF(ox + 19 * s, oy + 12 * s),
                       gfx::PointF(ox + 5 * s, oy + 12 * s), flags);
      break;
    case LucideIcon::kArrowRight:
      canvas->DrawLine(gfx::PointF(ox + 5 * s, oy + 12 * s),
                       gfx::PointF(ox + 19 * s, oy + 12 * s), flags);
      DrawSvgPath(canvas, "M12 5l7 7-7 7", s, ox, oy, flags);
      break;
    case LucideIcon::kRotateCw:
      // Lucide rotate-cw: curved arrow path + polyline arrow head
      DrawSvgPath(canvas, "M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8",
                  s, ox, oy, flags);
      DrawSvgPath(canvas, "M21 3v5h-5", s, ox, oy, flags);
      break;
    case LucideIcon::kMessageCircle:
      // Lucide message-circle
      DrawSvgPath(canvas,
                  "M7.9 20A9 9 0 1 0 4 16.1L2 22z",
                  s, ox, oy, flags);
      break;
    case LucideIcon::kPanelLeftClose:
      DrawPanelLeftClose(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kPanelLeftOpen:
      DrawPanelLeftOpen(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kGripHorizontal:
      DrawGripHorizontal(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kExternalLink:
      DrawExternalLink(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kSquareArrowDownLeft:
      DrawSquareArrowDownLeft(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kBot:
      DrawBot(canvas, s, ox, oy, flags);
      break;
    case LucideIcon::kSparkles:
      DrawSparkles(canvas, s, ox, oy, flags);
      break;
  }
}

namespace {

class LucideCanvasImageSource : public gfx::CanvasImageSource {
 public:
  LucideCanvasImageSource(LucideIcon icon, int size, SkColor color)
      : gfx::CanvasImageSource(gfx::Size(size, size)),
        icon_(icon),
        color_(color) {}

  LucideCanvasImageSource(const LucideCanvasImageSource&) = delete;
  LucideCanvasImageSource& operator=(const LucideCanvasImageSource&) = delete;

  void Draw(gfx::Canvas* canvas) override {
    DrawLucideIcon(canvas, icon_,
                   gfx::RectF(0, 0, size().width(), size().height()),
                   color_);
  }

 private:
  LucideIcon icon_;
  SkColor color_;
};

}  // namespace

gfx::ImageSkia CreateLucideImageSkia(LucideIcon icon, int size, SkColor color) {
  return gfx::CanvasImageSource::MakeImageSkia<LucideCanvasImageSource>(
      icon, size, color);
}

}  // namespace dao
