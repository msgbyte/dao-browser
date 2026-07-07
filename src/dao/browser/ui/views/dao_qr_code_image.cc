// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_qr_code_image.h"

#include <algorithm>

#include "components/qr_code_generator/qr_code_generator.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkPaint.h"
#include "ui/gfx/image/image.h"

namespace dao {

namespace {

constexpr int kQuietZoneModules = 3;
constexpr int kLocatorSizeModules = 7;
constexpr float kCenterBadgeScale = 0.21f;
constexpr float kCenterIconScale = 0.14f;
constexpr SkColor kModuleColor = SkColorSetRGB(52, 98, 166);
constexpr SkColor kLocatorColor = SkColorSetRGB(31, 65, 115);
constexpr SkColor kBackgroundColor = SK_ColorWHITE;
constexpr SkColor kCenterBadgeBorderColor = SkColorSetARGB(38, 31, 65, 115);

bool IsLocatorModule(int x, int y, int qr_size) {
  const bool in_left = x < kLocatorSizeModules;
  const bool in_right = x >= qr_size - kLocatorSizeModules;
  const bool in_top = y < kLocatorSizeModules;
  const bool in_bottom = y >= qr_size - kLocatorSizeModules;
  return (in_left && in_top) || (in_right && in_top) ||
         (in_left && in_bottom);
}

SkRect ModuleRect(int x,
                  int y,
                  int module_span,
                  float module_size,
                  float quiet_zone) {
  return SkRect::MakeXYWH(quiet_zone + x * module_size,
                          quiet_zone + y * module_size,
                          module_span * module_size,
                          module_span * module_size);
}

void DrawLocator(SkCanvas* canvas,
                 int x,
                 int y,
                 float module_size,
                 float quiet_zone,
                 const SkPaint& locator_paint,
                 const SkPaint& background_paint) {
  const SkScalar outer_radius = std::max(2.0f, module_size * 1.35f);
  const SkScalar middle_radius = std::max(2.0f, module_size * 1.0f);
  const SkScalar inner_radius = std::max(2.0f, module_size * 0.7f);

  SkRect outer =
      ModuleRect(x, y, kLocatorSizeModules, module_size, quiet_zone);
  canvas->drawRoundRect(outer, outer_radius, outer_radius, locator_paint);

  SkRect middle = ModuleRect(x + 1, y + 1, kLocatorSizeModules - 2,
                             module_size, quiet_zone);
  canvas->drawRoundRect(middle, middle_radius, middle_radius,
                        background_paint);

  SkRect inner = ModuleRect(x + 2, y + 2, kLocatorSizeModules - 4,
                            module_size, quiet_zone);
  canvas->drawRoundRect(inner, inner_radius, inner_radius, locator_paint);
}

void DrawCenterIcon(SkCanvas* canvas,
                    int size,
                    const gfx::Image& center_icon,
                    const SkPaint& background_paint) {
  if (center_icon.IsEmpty()) {
    return;
  }

  const SkBitmap icon_bitmap = center_icon.AsBitmap();
  if (icon_bitmap.drawsNothing()) {
    return;
  }

  const float badge_size = size * kCenterBadgeScale;
  const float icon_size = size * kCenterIconScale;
  const float badge_origin = (size - badge_size) / 2.0f;
  const float icon_origin = (size - icon_size) / 2.0f;

  const SkRect badge =
      SkRect::MakeXYWH(badge_origin, badge_origin, badge_size, badge_size);
  const SkScalar badge_radius = badge_size * 0.28f;
  canvas->drawRoundRect(badge, badge_radius, badge_radius, background_paint);

  SkPaint badge_border_paint;
  badge_border_paint.setAntiAlias(true);
  badge_border_paint.setColor(kCenterBadgeBorderColor);
  badge_border_paint.setStyle(SkPaint::kStroke_Style);
  badge_border_paint.setStrokeWidth(1.0f);
  canvas->drawRoundRect(badge, badge_radius, badge_radius, badge_border_paint);

  SkRect source_bounds;
  icon_bitmap.getBounds(&source_bounds);
  const SkRect icon_bounds =
      SkRect::MakeXYWH(icon_origin, icon_origin, icon_size, icon_size);
  canvas->drawImageRect(icon_bitmap.asImage(), source_bounds, icon_bounds,
                        SkSamplingOptions(), nullptr,
                        SkCanvas::kStrict_SrcRectConstraint);
}

}  // namespace

gfx::ImageSkia RenderDaoQrCode(const qr_code_generator::GeneratedCode& code,
                               int size) {
  return RenderDaoQrCode(code, size, gfx::Image());
}

gfx::ImageSkia RenderDaoQrCode(const qr_code_generator::GeneratedCode& code,
                               int size,
                               const gfx::Image& center_icon) {
  const int qr_size = code.qr_size;
  if (size <= 0 || qr_size < kLocatorSizeModules ||
      code.data.size() < static_cast<size_t>(qr_size * qr_size)) {
    return gfx::ImageSkia();
  }

  SkBitmap bitmap;
  bitmap.allocN32Pixels(size, size);
  bitmap.eraseColor(kBackgroundColor);

  SkCanvas canvas(bitmap, SkSurfaceProps{});

  SkPaint module_paint;
  module_paint.setAntiAlias(true);
  module_paint.setColor(kModuleColor);
  module_paint.setStyle(SkPaint::kFill_Style);

  SkPaint locator_paint;
  locator_paint.setAntiAlias(true);
  locator_paint.setColor(kLocatorColor);
  locator_paint.setStyle(SkPaint::kFill_Style);

  SkPaint background_paint;
  background_paint.setAntiAlias(true);
  background_paint.setColor(kBackgroundColor);
  background_paint.setStyle(SkPaint::kFill_Style);

  const int total_modules = qr_size + 2 * kQuietZoneModules;
  const float module_size = static_cast<float>(size) / total_modules;
  const float quiet_zone = module_size * kQuietZoneModules;
  const float dot_inset = std::max(1.0f, module_size * 0.16f);

  for (int y = 0; y < qr_size; ++y) {
    for (int x = 0; x < qr_size; ++x) {
      const size_t idx = static_cast<size_t>(y * qr_size + x);
      if ((code.data[idx] & 1) == 0 || IsLocatorModule(x, y, qr_size)) {
        continue;
      }

      SkRect dot = ModuleRect(x, y, 1, module_size, quiet_zone);
      dot.inset(dot_inset, dot_inset);
      canvas.drawOval(dot, module_paint);
    }
  }

  DrawLocator(&canvas, 0, 0, module_size, quiet_zone, locator_paint,
              background_paint);
  DrawLocator(&canvas, qr_size - kLocatorSizeModules, 0, module_size,
              quiet_zone, locator_paint, background_paint);
  DrawLocator(&canvas, 0, qr_size - kLocatorSizeModules, module_size,
              quiet_zone, locator_paint, background_paint);
  DrawCenterIcon(&canvas, size, center_icon, background_paint);

  return gfx::ImageSkia::CreateFromBitmap(bitmap, 1.0f);
}

}  // namespace dao
