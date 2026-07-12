// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PIP_DAO_PIP_RESIZE_UTILS_H_
#define DAO_BROWSER_PIP_DAO_PIP_RESIZE_UTILS_H_

#include <algorithm>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/geometry/vector2d.h"

namespace dao {

enum class PipOverlayResizeTarget {
  kNone,
  kTopLeft,
  kTopRight,
};

inline constexpr int kPipOverlayCornerResizeSize = 16;

inline gfx::Size GetMaximumPipWindowSize(const gfx::Rect& work_area) {
  return work_area.size();
}

inline PipOverlayResizeTarget GetPipOverlayResizeTarget(
    const gfx::Point& point,
    const gfx::Size& overlay_size) {
  if (overlay_size.width() <= 0 || overlay_size.height() <= 0 ||
      point.x() < 0 || point.y() < 0 ||
      point.x() >= overlay_size.width() ||
      point.y() >= overlay_size.height() ||
      point.y() >= kPipOverlayCornerResizeSize) {
    return PipOverlayResizeTarget::kNone;
  }
  if (point.x() < kPipOverlayCornerResizeSize) {
    return PipOverlayResizeTarget::kTopLeft;
  }
  if (point.x() >= overlay_size.width() - kPipOverlayCornerResizeSize) {
    return PipOverlayResizeTarget::kTopRight;
  }
  return PipOverlayResizeTarget::kNone;
}

inline gfx::Rect ResizePipWindowFromOverlayCorner(
    const gfx::Rect& start_bounds,
    const gfx::Point& drag_start_screen,
    const gfx::Point& current_screen,
    PipOverlayResizeTarget resize_target,
    const gfx::Size& minimum_size,
    const gfx::Size& maximum_size) {
  if (resize_target == PipOverlayResizeTarget::kNone) {
    return start_bounds;
  }

  const gfx::Vector2d delta = current_screen - drag_start_screen;
  int width = start_bounds.width();
  int height = start_bounds.height();
  switch (resize_target) {
    case PipOverlayResizeTarget::kTopLeft:
      width -= delta.x();
      height -= delta.y();
      break;
    case PipOverlayResizeTarget::kTopRight:
      width += delta.x();
      height -= delta.y();
      break;
    case PipOverlayResizeTarget::kNone:
      break;
  }

  const int min_width = minimum_size.width();
  const int min_height = minimum_size.height();
  const int max_width = std::max(min_width, maximum_size.width());
  const int max_height = std::max(min_height, maximum_size.height());
  width = std::clamp(width, min_width, max_width);
  height = std::clamp(height, min_height, max_height);

  gfx::Rect new_bounds = start_bounds;
  new_bounds.set_width(width);
  new_bounds.set_height(height);
  new_bounds.set_y(start_bounds.bottom() - height);
  if (resize_target == PipOverlayResizeTarget::kTopLeft) {
    new_bounds.set_x(start_bounds.right() - width);
  }
  return new_bounds;
}

}  // namespace dao

#endif  // DAO_BROWSER_PIP_DAO_PIP_RESIZE_UTILS_H_
