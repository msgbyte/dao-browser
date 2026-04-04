// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_cursor_view.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "cc/paint/paint_filter.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"

namespace dao {

namespace {
// Pointer dimensions.
constexpr float kPointerWidth = 20.0f;
constexpr float kPointerHeight = 24.0f;

// Ripple animation parameters.
constexpr float kRippleStartRadius = 4.0f;
constexpr float kRippleEndRadius = 24.0f;
constexpr float kRippleStartAlpha = 102.0f;  // 40%
constexpr float kRippleStartStrokeWidth = 2.0f;
constexpr float kRippleEndStrokeWidth = 0.5f;

// Animation frame interval (~60fps).
constexpr int kFrameIntervalMs = 16;
}  // namespace

BEGIN_METADATA(DaoAgentCursorView)
END_METADATA

DaoAgentCursorView::DaoAgentCursorView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  SetVisible(false);
}

DaoAgentCursorView::~DaoAgentCursorView() = default;

void DaoAgentCursorView::ShowAtCenter() {
  cursor_pos_ = gfx::PointF(width() / 2.0f, height() / 2.0f);
  cursor_visible_ = true;
  state_ = State::kIdle;
  SetVisible(true);
  SchedulePaint();
}

void DaoAgentCursorView::Hide() {
  cursor_visible_ = false;
  state_ = State::kIdle;
  move_timer_.Stop();
  ripple_timer_.Stop();
  move_complete_callback_.Reset();
  SetVisible(false);
}

void DaoAgentCursorView::AnimateTo(float x,
                                    float y,
                                    AnimationCompleteCallback callback) {
  move_start_ = cursor_pos_;
  move_end_ = gfx::PointF(x, y);
  move_complete_callback_ = std::move(callback);

  float dx = move_end_.x() - move_start_.x();
  float dy = move_end_.y() - move_start_.y();
  float distance = std::sqrt(dx * dx + dy * dy);
  move_duration_ = base::Milliseconds(ComputeMoveDurationMs(distance));
  move_start_time_ = base::TimeTicks::Now();
  state_ = State::kMoving;
  cursor_visible_ = true;
  SetVisible(true);

  move_timer_.Start(
      FROM_HERE, base::Milliseconds(kFrameIntervalMs),
      base::BindRepeating(&DaoAgentCursorView::OnAnimationTick,
                          base::Unretained(this)));
}

void DaoAgentCursorView::PlayClickRipple() {
  ripple_center_ = cursor_pos_;
  ripple_start_time_ = base::TimeTicks::Now();
  state_ = State::kClicking;

  ripple_timer_.Start(
      FROM_HERE, base::Milliseconds(kFrameIntervalMs),
      base::BindRepeating(&DaoAgentCursorView::OnRippleTick,
                          base::Unretained(this)));
}

void DaoAgentCursorView::OnAnimationTick() {
  base::TimeDelta elapsed = base::TimeTicks::Now() - move_start_time_;
  float t = std::min(1.0f,
      static_cast<float>(elapsed.InMillisecondsF() /
                         move_duration_.InMillisecondsF()));
  float eased = EaseOut(t);

  cursor_pos_.set_x(move_start_.x() +
                     (move_end_.x() - move_start_.x()) * eased);
  cursor_pos_.set_y(move_start_.y() +
                     (move_end_.y() - move_start_.y()) * eased);

  SchedulePaint();

  if (t >= 1.0f) {
    move_timer_.Stop();
    cursor_pos_ = move_end_;
    state_ = State::kArrived;
    if (move_complete_callback_) {
      std::move(move_complete_callback_).Run();
    }
  }
}

void DaoAgentCursorView::OnRippleTick() {
  base::TimeDelta elapsed = base::TimeTicks::Now() - ripple_start_time_;
  if (elapsed >= kRippleDuration) {
    ripple_timer_.Stop();
    state_ = State::kIdle;
  }
  SchedulePaint();
}

float DaoAgentCursorView::EaseOut(float t) const {
  // cubic-bezier(0.25, 0.1, 0.25, 1.0) approximation via ease-out cubic.
  return 1.0f - std::pow(1.0f - t, 3.0f);
}

int DaoAgentCursorView::ComputeMoveDurationMs(float distance) const {
  if (distance < 100.0f) {
    return 150;
  }
  if (distance < 500.0f) {
    return 300;
  }
  return 450;
}

void DaoAgentCursorView::OnPaint(gfx::Canvas* canvas) {
  if (!cursor_visible_) {
    return;
  }

  PaintCursor(canvas);

  if (state_ == State::kClicking) {
    PaintRipple(canvas);
  }
}

void DaoAgentCursorView::PaintCursor(gfx::Canvas* canvas) {
  // Build arrow pointer path. Hotspot at top-left (0,0).
  SkPath arrow;
  arrow.moveTo(0, 0);
  arrow.lineTo(0, kPointerHeight);
  arrow.lineTo(kPointerWidth * 0.35f, kPointerHeight * 0.75f);
  arrow.lineTo(kPointerWidth * 0.55f, kPointerHeight);
  arrow.lineTo(kPointerWidth * 0.7f, kPointerHeight * 0.9f);
  arrow.lineTo(kPointerWidth * 0.45f, kPointerHeight * 0.65f);
  arrow.lineTo(kPointerWidth, kPointerHeight * 0.45f);
  arrow.close();

  canvas->Save();
  canvas->Translate(
      gfx::Vector2d(static_cast<int>(cursor_pos_.x()),
                     static_cast<int>(cursor_pos_.y())));

  // Glow (purple shadow behind the pointer).
  cc::PaintFlags glow_flags;
  glow_flags.setAntiAlias(true);
  glow_flags.setStyle(cc::PaintFlags::kFill_Style);
  glow_flags.setColor(SkColorSetA(kSpaceActive, 51));  // 20%
  glow_flags.setImageFilter(
      sk_make_sp<cc::BlurPaintFilter>(2.0f, 2.0f, SkTileMode::kDecal,
                                      nullptr));
  canvas->DrawPath(arrow, glow_flags);

  // Fill (purple).
  cc::PaintFlags fill_flags;
  fill_flags.setAntiAlias(true);
  fill_flags.setStyle(cc::PaintFlags::kFill_Style);
  fill_flags.setColor(kSpaceActive);
  canvas->DrawPath(arrow, fill_flags);

  // Stroke (white outline).
  cc::PaintFlags stroke_flags;
  stroke_flags.setAntiAlias(true);
  stroke_flags.setStyle(cc::PaintFlags::kStroke_Style);
  stroke_flags.setStrokeWidth(1.0f);
  stroke_flags.setColor(SK_ColorWHITE);
  canvas->DrawPath(arrow, stroke_flags);

  canvas->Restore();
}

void DaoAgentCursorView::PaintRipple(gfx::Canvas* canvas) {
  base::TimeDelta elapsed = base::TimeTicks::Now() - ripple_start_time_;
  float t = std::min(1.0f,
      static_cast<float>(elapsed.InMillisecondsF() /
                         kRippleDuration.InMillisecondsF()));

  float radius = kRippleStartRadius + (kRippleEndRadius - kRippleStartRadius) * t;
  float alpha = kRippleStartAlpha * (1.0f - t);
  float stroke_width = kRippleStartStrokeWidth +
      (kRippleEndStrokeWidth - kRippleStartStrokeWidth) * t;

  cc::PaintFlags ripple_flags;
  ripple_flags.setAntiAlias(true);
  ripple_flags.setStyle(cc::PaintFlags::kStroke_Style);
  ripple_flags.setStrokeWidth(stroke_width);
  ripple_flags.setColor(SkColorSetA(kSpaceActive, static_cast<int>(alpha)));

  canvas->DrawCircle(
      gfx::Point(static_cast<int>(ripple_center_.x()),
                  static_cast<int>(ripple_center_.y())),
      radius, ripple_flags);
}

}  // namespace dao
