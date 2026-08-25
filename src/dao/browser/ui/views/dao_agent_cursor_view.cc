// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_cursor_view.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/transform.h"

namespace dao {

namespace {
// Pointer dimensions match the 23 x 24 CSS box used by Codex Desktop while
// leaving room for the accent glow.
constexpr float kPointerWidth = 19.0f;
constexpr float kPointerHeight = 24.0f;

// Ripple animation parameters.
constexpr float kRippleStartRadius = 4.0f;
constexpr float kRippleEndRadius = 24.0f;
constexpr float kRippleStartAlpha = 102.0f;  // 40%
constexpr float kRippleStartStrokeWidth = 2.0f;
constexpr float kRippleEndStrokeWidth = 0.5f;

// Animation frame interval (~60fps).
constexpr int kFrameIntervalMs = 16;
constexpr float kCurvedPathThreshold = 196.0f;
constexpr float kMaxCurveOffset = 64.0f;
constexpr float kPi = 3.14159265f;
constexpr float kShortMoveMaxRotationDegrees = 70.0f;
constexpr float kShortMoveCompression = 0.15f;
constexpr float kLongMovePointerOffsetDegrees = 135.0f;
constexpr float kLongMoveCompressionSpeed = 5500.0f;
constexpr float kLongMoveMinimumScale = 0.65f;
constexpr float kArrivalMaxRotationDegrees = 12.5f;
constexpr base::TimeDelta kArrivalDuration = base::Milliseconds(1410);
constexpr base::TimeDelta kArrivalCycle = base::Milliseconds(660);
}  // namespace

BEGIN_METADATA(DaoAgentCursorView)
END_METADATA

bool CanAnimateAgentCursorForTarget(content::WebContents* target) {
  Browser* browser = target ? chrome::FindBrowserWithTab(target) : nullptr;
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view && browser_view->GetActiveWebContents() == target &&
         browser_view->IsVisible() && !browser_view->IsMinimized() &&
         browser_view->IsActive();
}

DaoAgentCursorView::DaoAgentCursorView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  SetVisible(false);
}

DaoAgentCursorView::~DaoAgentCursorView() = default;

void DaoAgentCursorView::ShowAtCenter() {
  cursor_pos_ = gfx::PointF(width() / 2.0f, height() / 2.0f);
  cursor_rotation_degrees_ = 0.0f;
  cursor_scale_x_ = 1.0f;
  cursor_visible_ = true;
  state_ = State::kIdle;
  SetVisible(true);
  SchedulePaint();
}

void DaoAgentCursorView::Hide() {
  AnimationCompleteCallback callback = std::move(move_complete_callback_);
  cursor_visible_ = false;
  state_ = State::kIdle;
  cursor_rotation_degrees_ = 0.0f;
  cursor_scale_x_ = 1.0f;
  move_timer_.Stop();
  ripple_timer_.Stop();
  SetVisible(false);
  if (callback) {
    std::move(callback).Run();
  }
}

void DaoAgentCursorView::AnimateTo(float x,
                                    float y,
                                    AnimationCompleteCallback callback) {
  move_start_ = cursor_pos_;
  move_end_ = gfx::PointF(x, y);
  move_complete_callback_ = std::move(callback);
  move_timer_.Stop();
  cursor_rotation_degrees_ = 0.0f;
  cursor_scale_x_ = 1.0f;

  float dx = move_end_.x() - move_start_.x();
  float dy = move_end_.y() - move_start_.y();
  float distance = std::sqrt(dx * dx + dy * dy);
  if (gfx::Animation::PrefersReducedMotion()) {
    cursor_pos_ = move_end_;
    uses_curved_path_ = false;
    state_ = State::kArrived;
    cursor_visible_ = true;
    SetVisible(true);
    SchedulePaint();
    if (move_complete_callback_) {
      std::move(move_complete_callback_).Run();
    }
    return;
  }
  uses_curved_path_ = distance >= kCurvedPathThreshold;
  if (uses_curved_path_) {
    const float curve_offset = std::min(distance * 0.12f, kMaxCurveOffset);
    move_control_.set_x(std::clamp(
        (move_start_.x() + move_end_.x()) / 2.0f -
            dy / distance * curve_offset,
        0.0f, static_cast<float>(width())));
    move_control_.set_y(std::clamp(
        (move_start_.y() + move_end_.y()) / 2.0f +
            dx / distance * curve_offset,
        0.0f, static_cast<float>(height())));
  }
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
  move_timer_.Stop();
  cursor_rotation_degrees_ = 0.0f;
  cursor_scale_x_ = 1.0f;
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

  if (state_ == State::kArrived) {
    const float progress = std::min(
        1.0f, static_cast<float>(elapsed / kArrivalDuration));
    cursor_rotation_degrees_ =
        std::sin(2.0f * kPi * static_cast<float>(elapsed / kArrivalCycle)) *
        kArrivalMaxRotationDegrees * (1.0f - progress);
    SchedulePaint();
    if (progress >= 1.0f) {
      move_timer_.Stop();
      cursor_rotation_degrees_ = 0.0f;
      SchedulePaint();
    }
    return;
  }

  if (state_ != State::kMoving) {
    move_timer_.Stop();
    return;
  }

  float t = std::min(
      1.0f, static_cast<float>(elapsed.InMillisecondsF() /
                               move_duration_.InMillisecondsF()));
  float eased = EaseOut(t);

  if (uses_curved_path_) {
    const float remaining = 1.0f - eased;
    cursor_pos_.set_x(remaining * remaining * move_start_.x() +
                      2.0f * remaining * eased * move_control_.x() +
                      eased * eased * move_end_.x());
    cursor_pos_.set_y(remaining * remaining * move_start_.y() +
                      2.0f * remaining * eased * move_control_.y() +
                      eased * eased * move_end_.y());

    const float tangent_x =
        2.0f * remaining * (move_control_.x() - move_start_.x()) +
        2.0f * eased * (move_end_.x() - move_control_.x());
    const float tangent_y =
        2.0f * remaining * (move_control_.y() - move_start_.y()) +
        2.0f * eased * (move_end_.y() - move_control_.y());
    cursor_rotation_degrees_ =
        std::atan2(tangent_y, tangent_x) * 180.0f / kPi +
        kLongMovePointerOffsetDegrees;
    const float easing_velocity =
        3.0f * (1.0f - t) * (1.0f - t) /
        move_duration_.InSecondsF();
    const float speed = std::hypot(tangent_x, tangent_y) * easing_velocity;
    cursor_scale_x_ = std::clamp(
        1.0f - speed / kLongMoveCompressionSpeed,
        kLongMoveMinimumScale, 1.0f);
  } else {
    cursor_pos_.set_x(move_start_.x() +
                      (move_end_.x() - move_start_.x()) * eased);
    cursor_pos_.set_y(move_start_.y() +
                      (move_end_.y() - move_start_.y()) * eased);

    const float dx = move_end_.x() - move_start_.x();
    const float dy = move_end_.y() - move_start_.y();
    const float distance = std::hypot(dx, dy);
    const float direction = distance > 0.0f
                                ? std::clamp(dx / distance * 0.75f -
                                                 dy / distance * 0.62f,
                                             -1.0f, 1.0f)
                                : 0.0f;
    const float motion = std::sin(kPi * t);
    cursor_rotation_degrees_ =
        direction * kShortMoveMaxRotationDegrees * motion;
    cursor_scale_x_ = 1.0f - kShortMoveCompression * motion;
  }

  SchedulePaint();

  if (t >= 1.0f) {
    cursor_pos_ = move_end_;
    state_ = State::kArrived;
    move_start_time_ = base::TimeTicks::Now();
    cursor_rotation_degrees_ = 0.0f;
    cursor_scale_x_ = 1.0f;
    if (move_complete_callback_) {
      std::move(move_complete_callback_).Run();
    }
  }
}

void DaoAgentCursorView::OnRippleTick() {
  base::TimeDelta elapsed = base::TimeTicks::Now() - ripple_start_time_;
  if (elapsed >= kRippleDuration) {
    ripple_timer_.Stop();
    // Click is the terminal action: hide the cursor entirely once the
    // ripple finishes so it doesn't linger on the page. Subsequent
    // clicks/moves will re-show it at center via the UI handler.
    Hide();
    return;
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
  // Rounded Codex-style arrow. The pointer hotspot remains at the tip (0, 0).
  const float w = kPointerWidth;
  const float h = kPointerHeight;
  SkPath arrow = SkPathBuilder()
                     .moveTo(0.8f, 0.8f)
                     .quadTo(0.5f, 0.5f, 0.6f, 1.4f)
                     .lineTo(1.8f, h * 0.77f)
                     .quadTo(1.9f, h * 0.83f, 3.0f, h * 0.77f)
                     .lineTo(w * 0.37f, h * 0.63f)
                     .lineTo(w * 0.56f, h * 0.92f)
                     .quadTo(w * 0.61f, h * 0.99f, w * 0.68f, h * 0.94f)
                     .lineTo(w * 0.80f, h * 0.87f)
                     .quadTo(w * 0.86f, h * 0.83f, w * 0.81f, h * 0.77f)
                     .lineTo(w * 0.60f, h * 0.53f)
                     .lineTo(w * 0.94f, h * 0.48f)
                     .quadTo(w, h * 0.46f, w * 0.94f, h * 0.42f)
                     .lineTo(w * 0.09f, h * 0.03f)
                     .quadTo(w * 0.05f, 0.01f, 0.8f, 0.8f)
                     .close()
                     .detach();

  canvas->Save();
  gfx::Transform transform;
  transform.Translate(cursor_pos_.x(), cursor_pos_.y());
  transform.Rotate(cursor_rotation_degrees_);
  transform.Scale(cursor_scale_x_, 1.0f);
  canvas->Transform(transform);

  const SkColor accent = SpaceActive();

  cc::PaintFlags glow;
  glow.setAntiAlias(true);
  glow.setStyle(cc::PaintFlags::kFill_Style);
  glow.setColor(SkColorSetA(accent, 122));
  glow.setImageFilter(sk_make_sp<cc::BlurPaintFilter>(
      7.0f, 7.0f, SkTileMode::kDecal, nullptr));
  canvas->DrawPath(arrow, glow);
  glow.setColor(SkColorSetA(accent, 230));
  glow.setImageFilter(sk_make_sp<cc::BlurPaintFilter>(
      3.0f, 3.0f, SkTileMode::kDecal, nullptr));
  canvas->DrawPath(arrow, glow);

  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeJoin(cc::PaintFlags::kRound_Join);
  stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
  stroke.setStrokeWidth(4.5f);
  stroke.setColor(SkColorSetRGB(18, 18, 20));
  canvas->DrawPath(arrow, stroke);
  stroke.setStrokeWidth(2.6f);
  stroke.setColor(SK_ColorWHITE);
  canvas->DrawPath(arrow, stroke);

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setStyle(cc::PaintFlags::kFill_Style);
  fill.setColor(SkColorSetRGB(18, 18, 20));
  canvas->DrawPath(arrow, fill);

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
  ripple_flags.setColor(SkColorSetA(SpaceActive(), static_cast<int>(alpha)));

  canvas->DrawCircle(
      gfx::Point(static_cast<int>(ripple_center_.x()),
                  static_cast<int>(ripple_center_.y())),
      radius, ripple_flags);
}

}  // namespace dao
