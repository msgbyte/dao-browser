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
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/multi_contents_view.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkPathBuilder.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/views/animation/animation_builder.h"

namespace dao {

namespace {
// Orb geometry. The hotspot is the orb center.
constexpr float kRingRadius = 7.0f;
constexpr float kCoreRadius = 4.2f;
constexpr float kCorePressDepth = 1.8f;
constexpr float kHaloRadius = 9.0f;
constexpr float kHaloBlurSigma = 6.0f;
constexpr float kHaloAlpha = 0.42f;
constexpr float kBreathScale = 0.45f;
constexpr SkColor kOrbShadow = SkColorSetARGB(89, 16, 36, 72);

// Motion trail.
constexpr base::TimeDelta kTrailDuration = base::Milliseconds(200);
constexpr float kTrailWidth = 8.0f;
constexpr float kTrailAlpha = 0.38f;

// Click shock waves.
constexpr base::TimeDelta kClickDuration = base::Milliseconds(560);
constexpr base::TimeDelta kCorePressDuration = base::Milliseconds(200);
constexpr base::TimeDelta kWaveDuration = base::Milliseconds(460);
constexpr base::TimeDelta kSecondWaveDelay = base::Milliseconds(100);
constexpr float kWaveTravel = 22.0f;
constexpr float kWaveAlpha = 0.6f;

// Focus brackets around the target element.
constexpr float kAimPadding = 4.0f;
constexpr float kAimMinSize = 28.0f;
constexpr float kAimViewInset = 2.0f;
constexpr float kAimMaxViewportCoverage = 0.8f;
constexpr float kAimStartInflate = 14.0f;
constexpr float kAimPressDepth = 2.0f;
constexpr float kAimCornerRadius = 7.0f;
constexpr float kAimBracketLength = 9.0f;
constexpr float kAimBracketRadius = 3.0f;
constexpr float kAimFillAlpha = 0.08f;
constexpr base::TimeDelta kAimFadeIn = base::Milliseconds(160);
constexpr base::TimeDelta kAimFadeOut = base::Milliseconds(260);
constexpr base::TimeDelta kMinAimDuration = base::Milliseconds(220);

// Appearance and idle behavior.
constexpr float kAppearStartScale = 0.85f;
constexpr float kAppearOffsetX = -36.0f;
constexpr float kAppearOffsetY = -28.0f;
constexpr base::TimeDelta kFadeInDuration = base::Milliseconds(120);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(200);
constexpr base::TimeDelta kBreathDuration = base::Milliseconds(700);
constexpr base::TimeDelta kIdleDimDelay = base::Milliseconds(2500);
constexpr base::TimeDelta kIdleDimDuration = base::Milliseconds(400);
constexpr base::TimeDelta kIdleHideDelay = base::Seconds(6);
constexpr float kIdleOpacity = 0.55f;

// Animation frame interval (~60fps).
constexpr base::TimeDelta kFrameInterval = base::Milliseconds(16);
constexpr float kCurvedPathThreshold = 196.0f;
constexpr float kMaxCurveOffset = 64.0f;
constexpr float kPi = 3.14159265f;

float Progress(base::TimeDelta elapsed, base::TimeDelta duration) {
  return std::clamp(static_cast<float>(elapsed / duration), 0.0f, 1.0f);
}

float EaseOutCubic(float t) {
  return 1.0f - std::pow(1.0f - std::clamp(t, 0.0f, 1.0f), 3.0f);
}

SkColor WithAlpha(SkColor color, float alpha) {
  return SkColorSetA(color,
                     static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
}

int ComputeMoveDurationMs(float distance) {
  if (distance < 100.0f) {
    return 150;
  }
  if (distance < 500.0f) {
    return 300;
  }
  return 450;
}

gfx::RectF ExpandToMinSize(const gfx::RectF& rect, float min_size) {
  const gfx::PointF center = rect.CenterPoint();
  const float width = std::max(rect.width(), min_size);
  const float height = std::max(rect.height(), min_size);
  return gfx::RectF(center.x() - width / 2.0f, center.y() - height / 2.0f,
                    width, height);
}

SkPath BuildBrackets(const gfx::RectF& r, float length, float radius) {
  const float left = r.x();
  const float top = r.y();
  const float right = r.right();
  const float bottom = r.bottom();
  SkPathBuilder builder;
  builder.moveTo(left, top + length)
      .lineTo(left, top + radius)
      .quadTo(left, top, left + radius, top)
      .lineTo(left + length, top);
  builder.moveTo(right - length, top)
      .lineTo(right - radius, top)
      .quadTo(right, top, right, top + radius)
      .lineTo(right, top + length);
  builder.moveTo(right, bottom - length)
      .lineTo(right, bottom - radius)
      .quadTo(right, bottom, right - radius, bottom)
      .lineTo(right - length, bottom);
  builder.moveTo(left + length, bottom)
      .lineTo(left + radius, bottom)
      .quadTo(left, bottom, left, bottom - radius)
      .lineTo(left, bottom - length);
  return builder.detach();
}
}  // namespace

BEGIN_METADATA(DaoAgentCursorView)
END_METADATA

bool CanAnimateAgentCursorForTarget(content::WebContents* target) {
  BrowserWindowInterface* browser =
      target ? GlobalBrowserCollection::GetInstance()
               ->FindBrowserWithTab(target)
             : nullptr;
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  return browser_view && browser_view->GetActiveWebContents() == target &&
         browser_view->IsVisible() && !browser_view->IsMinimized() &&
         browser_view->IsActive();
}

std::optional<gfx::RectF> ComputeAgentCursorFrame(
    const gfx::RectF& target_bounds,
    const gfx::RectF& viewport) {
  // Framing a page-sized container would only tint the whole page.
  gfx::RectF visible = target_bounds;
  visible.Intersect(viewport);
  if (visible.IsEmpty() || visible.size().GetArea() >=
                               kAimMaxViewportCoverage *
                                   viewport.size().GetArea()) {
    return std::nullopt;
  }
  gfx::RectF frame = target_bounds;
  frame.Outset(kAimPadding);
  frame = ExpandToMinSize(frame, kAimMinSize);
  // Keep the frame inside the target's own pane, e.g. in Split View.
  gfx::RectF clip = viewport;
  clip.Inset(kAimViewInset);
  frame.Intersect(clip);
  if (frame.IsEmpty()) {
    return std::nullopt;
  }
  return frame;
}

void AnimateAgentCursorInViewport(BrowserView* browser_view,
                                  content::WebContents* target,
                                  double x,
                                  double y,
                                  std::optional<gfx::RectF> target_bounds,
                                  base::OnceCallback<void(bool)> callback) {
  DaoAgentCursorView* cursor_view = browser_view->dao_agent_cursor();
  const gfx::Rect viewport_screen =
      target->GetRenderWidgetHostView()
          ? target->GetRenderWidgetHostView()->GetViewBounds()
          : browser_view->multi_contents_view()->GetBoundsInScreen();
  const gfx::Vector2d offset =
      viewport_screen.origin() - cursor_view->GetBoundsInScreen().origin();
  const gfx::RectF viewport(static_cast<float>(offset.x()),
                            static_cast<float>(offset.y()),
                            static_cast<float>(viewport_screen.width()),
                            static_cast<float>(viewport_screen.height()));
  // Page tools report CSS pixels, which page zoom scales into view DIPs.
  // ponytail: pinch zoom is ignored; map through the visual viewport if
  // pinch-zoomed pages need exact framing.
  const float zoom = static_cast<float>(blink::ZoomLevelToZoomFactor(
      content::HostZoomMap::GetZoomLevel(target)));
  std::optional<gfx::RectF> frame;
  if (target_bounds) {
    gfx::RectF bounds = gfx::ScaleRect(*target_bounds, zoom);
    bounds.Offset(viewport.x(), viewport.y());
    frame = ComputeAgentCursorFrame(bounds, viewport);
  }
  cursor_view->AnimateTo(
      viewport.x() + static_cast<float>(x) * zoom,
      viewport.y() + static_cast<float>(y) * zoom, frame,
      base::BindOnce(
          [](base::WeakPtr<content::WebContents> target,
             base::OnceCallback<void(bool)> done, bool moved) {
            std::move(done).Run(moved && target &&
                                CanAnimateAgentCursorForTarget(target.get()));
          },
          target->GetWeakPtr(), std::move(callback)));
}

DaoAgentCursorView::DaoAgentCursorView()
    : idle_dim_delay_(kIdleDimDelay), idle_hide_delay_(kIdleHideDelay) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  SetVisible(false);
}

DaoAgentCursorView::~DaoAgentCursorView() = default;

void DaoAgentCursorView::ShowAtCenter() {
  ShowAt(gfx::PointF(width() / 2.0f, height() / 2.0f));
}

void DaoAgentCursorView::OnActiveTabChanged(bool locked) {
  active_tab_locked_ = locked;
  if (locked) {
    ShowAtCenter();
  } else {
    Hide();
  }
}

void DaoAgentCursorView::OnActiveTabUpdated(bool locked) {
  if (locked != active_tab_locked_) {
    OnActiveTabChanged(locked);
  }
}

void DaoAgentCursorView::SetIdleDelaysForTesting(base::TimeDelta dim_delay,
                                                 base::TimeDelta hide_delay) {
  idle_dim_delay_ = dim_delay;
  idle_hide_delay_ = hide_delay;
}

void DaoAgentCursorView::ShowAt(const gfx::PointF& position) {
  AnimationCompleteCallback callback = std::move(move_complete_callback_);
  cursor_pos_ = position;
  cursor_visible_ = true;
  state_ = State::kIdle;
  trail_.clear();
  target_frame_.reset();
  aim_fade_start_time_.reset();
  arrive_time_.reset();
  show_time_ = base::TimeTicks::Now();
  SetVisible(true);
  // Abort first: setting opacity directly would finish a pending fade-out,
  // which hides the cursor.
  layer()->GetAnimator()->AbortAllAnimations();
  layer()->SetOpacity(0.0f);
  AnimateOpacity(1.0f, kFadeInDuration, /*hide_when_done=*/false);
  StartFrames();
  StartIdleTimer();
  SchedulePaint();
  if (callback) {
    std::move(callback).Run(false);
  }
}

void DaoAgentCursorView::Hide() {
  cursor_visible_ = false;
  CancelAnimation();
}

void DaoAgentCursorView::CancelAnimation() {
  AnimationCompleteCallback callback = std::move(move_complete_callback_);
  state_ = State::kIdle;
  frame_timer_.Stop();
  idle_timer_.Stop();
  trail_.clear();
  target_frame_.reset();
  aim_fade_start_time_.reset();
  show_time_.reset();
  arrive_time_.reset();
  if (cursor_visible_ && active_tab_locked_) {
    layer()->GetAnimator()->AbortAllAnimations();
    Wake();
    StartIdleTimer();
    SchedulePaint();
  } else {
    cursor_visible_ = false;
    // Leave the layer animator alone when called by the idle fade-out.
    SetVisible(false);
  }
  if (callback) {
    std::move(callback).Run(false);
  }
}

void DaoAgentCursorView::Wake() {
  idle_timer_.Stop();
  if (layer()->GetTargetOpacity() < 1.0f) {
    AnimateOpacity(1.0f, kFadeInDuration, /*hide_when_done=*/false);
  }
}

void DaoAgentCursorView::AnimateOpacity(float opacity,
                                        base::TimeDelta duration,
                                        bool hide_when_done) {
  views::AnimationBuilder builder;
  builder.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  if (hide_when_done) {
    builder.OnEnded(base::BindOnce(&DaoAgentCursorView::Hide,
                                   weak_factory_.GetWeakPtr()));
  }
  builder.Once().SetDuration(duration).SetOpacity(layer(), opacity);
}

void DaoAgentCursorView::AnimateTo(float x,
                                   float y,
                                   std::optional<gfx::RectF> target_frame,
                                   AnimationCompleteCallback callback) {
  if (state_ == State::kMoving) {
    if (callback) {
      std::move(callback).Run(false);
    }
    return;
  }

  // Reappear near the target instead of flying in from the center.
  if (cursor_visible_) {
    Wake();
  } else {
    ShowAt(gfx::PointF(
        std::clamp(x + kAppearOffsetX, 0.0f, static_cast<float>(width())),
        std::clamp(y + kAppearOffsetY, 0.0f, static_cast<float>(height()))));
  }

  const base::TimeTicks now = base::TimeTicks::Now();
  idle_timer_.Stop();
  arrive_time_.reset();
  move_start_ = cursor_pos_;
  move_end_ = gfx::PointF(x, y);
  move_complete_callback_ = std::move(callback);
  target_frame_ = target_frame;
  aim_fade_start_time_.reset();
  aim_start_time_ = now;

  const float dx = move_end_.x() - move_start_.x();
  const float dy = move_end_.y() - move_start_.y();
  const float distance = std::hypot(dx, dy);
  move_duration_ = base::Milliseconds(ComputeMoveDurationMs(distance));
  if (gfx::Animation::PrefersReducedMotion()) {
    cursor_pos_ = move_end_;
    uses_curved_path_ = false;
    state_ = State::kArrived;
    if (!target_frame_) {
      StartIdleTimer();
    }
    SchedulePaint();
    if (move_complete_callback_) {
      std::move(move_complete_callback_).Run(true);
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
  move_start_time_ = now;
  state_ = State::kMoving;
  StartFrames();
}

void DaoAgentCursorView::PlayClickRipple() {
  // A delayed click response must not interrupt a newer move.
  if (state_ == State::kMoving || !cursor_visible_) {
    return;
  }
  Wake();
  click_start_time_ = base::TimeTicks::Now();
  state_ = State::kClicking;
  StartFrames();
}

void DaoAgentCursorView::StartFrames() {
  if (!frame_timer_.IsRunning()) {
    frame_timer_.Start(FROM_HERE, kFrameInterval,
                       base::BindRepeating(&DaoAgentCursorView::OnFrame,
                                           base::Unretained(this)));
  }
}

void DaoAgentCursorView::OnFrame() {
  const base::TimeTicks now = base::TimeTicks::Now();
  bool arrived = false;
  if (state_ == State::kMoving) {
    const float t = Progress(now - move_start_time_, move_duration_);
    const float eased = EaseOutCubic(t);
    if (uses_curved_path_) {
      const float remaining = 1.0f - eased;
      cursor_pos_.SetPoint(remaining * remaining * move_start_.x() +
                               2.0f * remaining * eased * move_control_.x() +
                               eased * eased * move_end_.x(),
                           remaining * remaining * move_start_.y() +
                               2.0f * remaining * eased * move_control_.y() +
                               eased * eased * move_end_.y());
    } else {
      cursor_pos_.SetPoint(
          move_start_.x() + (move_end_.x() - move_start_.x()) * eased,
          move_start_.y() + (move_end_.y() - move_start_.y()) * eased);
    }
    trail_.push_back({cursor_pos_, now});
    if (t >= 1.0f) {
      cursor_pos_ = move_end_;
      state_ = State::kArrived;
      arrive_time_ = now;
      // A framed target is about to be clicked; idle only after the click.
      if (!target_frame_) {
        StartIdleTimer();
      }
      arrived = true;
    }
  } else if (state_ == State::kClicking &&
             now - click_start_time_ >= kClickDuration) {
    // The cursor stays where it clicked; only the focus brackets go away.
    state_ = State::kIdle;
    if (target_frame_ && !aim_fade_start_time_) {
      aim_fade_start_time_ = now;
    }
    StartIdleTimer();
  }

  while (!trail_.empty() && now - trail_.front().time > kTrailDuration) {
    trail_.pop_front();
  }
  if (aim_fade_start_time_ && now - *aim_fade_start_time_ >= kAimFadeOut) {
    target_frame_.reset();
    aim_fade_start_time_.reset();
  }

  // Frames only run while geometry changes; opacity animates on the layer.
  SchedulePaint();
  if (!IsAnimating(now)) {
    frame_timer_.Stop();
  }
  if (arrived && move_complete_callback_) {
    std::move(move_complete_callback_).Run(true);
  }
}

void DaoAgentCursorView::StartIdleTimer() {
  idle_timer_.Start(FROM_HERE, idle_dim_delay_,
                    base::BindOnce(&DaoAgentCursorView::OnIdleDim,
                                   base::Unretained(this)));
}

void DaoAgentCursorView::OnIdleDim() {
  if (target_frame_ && !aim_fade_start_time_) {
    aim_fade_start_time_ = base::TimeTicks::Now();
    StartFrames();
  }
  AnimateOpacity(kIdleOpacity, kIdleDimDuration, /*hide_when_done=*/false);
  if (!active_tab_locked_) {
    idle_timer_.Start(FROM_HERE, idle_hide_delay_ - idle_dim_delay_,
                      base::BindOnce(&DaoAgentCursorView::OnIdleHide,
                                     base::Unretained(this)));
  }
}

void DaoAgentCursorView::OnIdleHide() {
  AnimateOpacity(0.0f, kFadeOutDuration, /*hide_when_done=*/true);
}

bool DaoAgentCursorView::IsAnimating(base::TimeTicks now) const {
  return state_ == State::kMoving || state_ == State::kClicking ||
         !trail_.empty() || aim_fade_start_time_.has_value() ||
         (show_time_ && now - *show_time_ < kFadeInDuration) ||
         (arrive_time_ && now - *arrive_time_ < kBreathDuration) ||
         (target_frame_ && now - aim_start_time_ < AimDuration());
}

base::TimeDelta DaoAgentCursorView::AimDuration() const {
  return std::max(move_duration_, kMinAimDuration);
}

void DaoAgentCursorView::OnPaint(gfx::Canvas* canvas) {
  if (!cursor_visible_) {
    return;
  }
  const base::TimeTicks now = base::TimeTicks::Now();
  PaintTargetFrame(canvas, now);
  PaintTrail(canvas, now);
  PaintOrb(canvas, now);
  if (state_ == State::kClicking) {
    PaintClickWaves(canvas, now);
  }
}

void DaoAgentCursorView::PaintTargetFrame(gfx::Canvas* canvas,
                                          base::TimeTicks now) {
  if (!target_frame_) {
    return;
  }
  const bool reduce_motion = gfx::Animation::PrefersReducedMotion();
  float alpha =
      reduce_motion ? 1.0f : Progress(now - aim_start_time_, kAimFadeIn);
  if (aim_fade_start_time_) {
    alpha *= 1.0f - Progress(now - *aim_fade_start_time_, kAimFadeOut);
  }
  if (alpha <= 0.0f) {
    return;
  }
  // The brackets start loose and tighten onto the target as the cursor
  // arrives, then squeeze briefly on click.
  const float converge =
      reduce_motion
          ? 1.0f
          : EaseOutCubic(Progress(now - aim_start_time_, AimDuration()));
  float inflate = kAimStartInflate * (1.0f - converge);
  if (state_ == State::kClicking && !reduce_motion) {
    inflate -= kAimPressDepth *
               std::sin(kPi * Progress(now - click_start_time_,
                                       kCorePressDuration));
  }
  gfx::RectF frame = *target_frame_;
  frame.Outset(inflate);
  const SkColor accent = SpaceActive();

  cc::PaintFlags fill;
  fill.setAntiAlias(true);
  fill.setStyle(cc::PaintFlags::kFill_Style);
  fill.setColor(WithAlpha(accent, kAimFillAlpha * converge * alpha));
  canvas->DrawRoundRect(frame, kAimCornerRadius, fill);

  const float length = std::max(
      kAimBracketRadius, std::min({kAimBracketLength, frame.width() / 3.0f,
                                   frame.height() / 3.0f}));
  const SkPath brackets = BuildBrackets(frame, length, kAimBracketRadius);
  cc::PaintFlags stroke;
  stroke.setAntiAlias(true);
  stroke.setStyle(cc::PaintFlags::kStroke_Style);
  stroke.setStrokeCap(cc::PaintFlags::kRound_Cap);
  stroke.setStrokeJoin(cc::PaintFlags::kRound_Join);
  stroke.setStrokeWidth(4.0f);
  stroke.setColor(WithAlpha(SK_ColorWHITE, 0.9f * alpha));
  canvas->DrawPath(brackets, stroke);
  stroke.setStrokeWidth(2.0f);
  stroke.setColor(WithAlpha(accent, alpha));
  canvas->DrawPath(brackets, stroke);
}

void DaoAgentCursorView::PaintTrail(gfx::Canvas* canvas, base::TimeTicks now) {
  if (trail_.size() < 2) {
    return;
  }
  const SkColor accent = SpaceActive();
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  // Butt caps keep overlapping segments from beading at the joints.
  flags.setStrokeCap(cc::PaintFlags::kButt_Cap);
  for (size_t i = 1; i < trail_.size(); ++i) {
    const float life = 1.0f - Progress(now - trail_[i].time, kTrailDuration);
    if (life <= 0.0f) {
      continue;
    }
    flags.setStrokeWidth(kTrailWidth * life);
    flags.setColor(WithAlpha(accent, kTrailAlpha * life));
    canvas->DrawLine(trail_[i - 1].position, trail_[i].position, flags);
  }
}

void DaoAgentCursorView::PaintOrb(gfx::Canvas* canvas, base::TimeTicks now) {
  const float appear =
      show_time_ && !gfx::Animation::PrefersReducedMotion()
          ? kAppearStartScale +
                (1.0f - kAppearStartScale) *
                    EaseOutCubic(Progress(now - *show_time_, kFadeInDuration))
          : 1.0f;
  // One soft breath on arrival replaces the old pointer wobble.
  float breath = 1.0f;
  if (arrive_time_ && state_ != State::kMoving) {
    breath += kBreathScale *
              std::sin(kPi * Progress(now - *arrive_time_, kBreathDuration));
  }
  const SkColor accent = SpaceActive();

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);

  flags.setColor(WithAlpha(accent, kHaloAlpha));
  flags.setImageFilter(sk_make_sp<cc::BlurPaintFilter>(
      kHaloBlurSigma * breath, kHaloBlurSigma * breath, SkTileMode::kDecal,
      nullptr));
  canvas->DrawCircle(cursor_pos_, kHaloRadius * appear * breath, flags);

  flags.setColor(kOrbShadow);
  flags.setImageFilter(sk_make_sp<cc::BlurPaintFilter>(
      2.0f, 2.0f, SkTileMode::kDecal, nullptr));
  canvas->DrawCircle(gfx::PointF(cursor_pos_.x(), cursor_pos_.y() + 1.0f),
                     kRingRadius * appear, flags);
  flags.setImageFilter(nullptr);

  flags.setColor(accent);
  canvas->DrawCircle(cursor_pos_, kRingRadius * appear, flags);

  float core = kCoreRadius;
  if (state_ == State::kClicking) {
    core -= kCorePressDepth *
            std::sin(kPi * Progress(now - click_start_time_,
                                    kCorePressDuration));
  }
  flags.setColor(SK_ColorWHITE);
  canvas->DrawCircle(cursor_pos_, core * appear, flags);
}

void DaoAgentCursorView::PaintClickWaves(gfx::Canvas* canvas,
                                         base::TimeTicks now) {
  const base::TimeDelta elapsed = now - click_start_time_;
  const SkColor accent = SpaceActive();
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  for (const base::TimeDelta delay : {base::TimeDelta(), kSecondWaveDelay}) {
    if (elapsed < delay) {
      continue;
    }
    const float k = EaseOutCubic(Progress(elapsed - delay, kWaveDuration));
    flags.setStrokeWidth(2.0f * (1.0f - k) + 0.5f);
    flags.setColor(WithAlpha(accent, kWaveAlpha * (1.0f - k)));
    // The cursor cannot move while clicking, so the waves stay centered on it.
    canvas->DrawCircle(cursor_pos_, kRingRadius + kWaveTravel * k, flags);
  }
}

}  // namespace dao
