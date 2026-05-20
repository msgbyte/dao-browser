// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_load_progress_view.h"

#include <algorithm>
#include <cmath>

#include "base/functional/bind.h"
#include "base/time/time.h"
#include "cc/paint/paint_filter.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkPoint.h"
#include "third_party/skia/include/core/SkTileMode.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"

namespace dao {

namespace {
// Lighter sky-blue, intentionally one shade brighter than dao::SpaceActive()'s
// (70,120,190) brand accent. The progress bar is an ambient signal, not a
// strong action color, so the lighter hue blends with both light and dark
// content cards without competing with the brand blue used for selection.
constexpr SkColor kProgressGlow = SkColorSetRGB(140, 200, 245);

// Painted strip layout. The view is taller than the visible "core" so the
// glow has room to fade out softly. Total view height (kCoreHeight +
// kGlowHeight) is what BrowserViewTabbedLayoutImpl gives us via SetBounds.
// The glow is intentionally tall and blurred (Dia-style soft luminous band
// spreading upward from the content card's top edge), not a thin halo.
constexpr int kCoreHeight = 1;
constexpr int kGlowHeight = 35;
// Skia blur sigma applied to the glow rect. Horizontal sigma softens the
// left/right edges of the band; vertical sigma is intentionally small so
// the bottom-to-top fade in the gradient stays visible rather than being
// smoothed into a flat translucent slab.
constexpr SkScalar kGlowBlurSigmaX = 4.0f;
constexpr SkScalar kGlowBlurSigmaY = 1.5f;
// Soft luminous band, not a fill. Alpha is bumped back up a bit since the
// vertical blur is now light — the gradient itself carries most of the
// fade now.
constexpr SkAlpha kGlowMaxAlpha = 110;
constexpr SkAlpha kCoreAlpha = 220;

constexpr base::TimeDelta kProgressAnimDuration = base::Milliseconds(120);
constexpr base::TimeDelta kCompleteHoldDuration = base::Milliseconds(150);
constexpr base::TimeDelta kFadeOutDuration = base::Milliseconds(200);
}  // namespace

BEGIN_METADATA(DaoLoadProgressView)
END_METADATA

DaoLoadProgressView::DaoLoadProgressView() {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetOpacity(0.0f);
  SetCanProcessEventsWithinSubtree(false);
}

DaoLoadProgressView::~DaoLoadProgressView() = default;

void DaoLoadProgressView::SetTargetProgress(double progress, bool animate) {
  progress = std::clamp(progress, 0.0, 1.0);
  // Backward progress (e.g. a new navigation reset to 0) snaps to avoid an
  // odd shrinking animation.
  const bool snap = !animate || progress < displayed_progress_;
  target_progress_ = progress;
  if (snap) {
    if (progress_animation_) {
      progress_animation_->Stop();
    }
    displayed_progress_ = progress;
    SchedulePaint();
    return;
  }
  StartProgressAnimation(displayed_progress_, progress);
}

void DaoLoadProgressView::StartLoading() {
  hold_timer_.Stop();
  state_ = State::kLoading;
  displayed_progress_ = 0.0;
  target_progress_ = 0.0;
  animation_start_progress_ = 0.0;
  if (progress_animation_) {
    progress_animation_->Stop();
  }
  // Snap to opaque — cancel any in-flight fade.
  layer()->GetAnimator()->AbortAllAnimations();
  layer()->SetOpacity(1.0f);
  SchedulePaint();
}

void DaoLoadProgressView::FinishLoading() {
  if (state_ == State::kHidden) {
    return;  // Stray DidStopLoading before any load — ignore.
  }
  state_ = State::kCompleting;
  // Animate the fill to 1.0 if not already there.
  if (displayed_progress_ < 1.0) {
    StartProgressAnimation(displayed_progress_, 1.0);
  }
  // Schedule fade-out after the hold.
  hold_timer_.Start(
      FROM_HERE, kCompleteHoldDuration,
      base::BindOnce(&DaoLoadProgressView::StartFadeOut,
                     base::Unretained(this)));
}

void DaoLoadProgressView::HideImmediately() {
  hold_timer_.Stop();
  if (progress_animation_) {
    progress_animation_->Stop();
  }
  // Stop observing any in-flight layer animation before aborting it, so the
  // abort doesn't reenter EnterHidden via OnImplicitAnimationsCompleted.
  layer()->GetAnimator()->StopAnimating();
  EnterHidden();
}

void DaoLoadProgressView::OnPaint(gfx::Canvas* canvas) {
  if (state_ == State::kHidden) {
    return;
  }

  const double p = std::clamp(displayed_progress_, 0.0, 1.0);
  const int fill_w = static_cast<int>(std::round(width() * p));
  if (fill_w <= 0) {
    return;
  }

  // Layout (bottom-anchored): the bright core line sits flush against the
  // bottom of the view, and the glow rises upward, fading from opaque at the
  // core to transparent at the top. The view's bottom edge is aligned with
  // the content card's top edge by the layout pass.
  const int view_h = height();
  const int core_top = view_h - kCoreHeight;
  const int glow_top = std::max(0, core_top - kGlowHeight);

  // The bar fades horizontally — fully opaque at the leading edge and
  // tapering to transparent toward x=0. We paint the glow + core into a
  // saveLayer, then composite a horizontal alpha mask via DstIn so the
  // fade applies uniformly to both layers (vertical gradient × horizontal
  // alpha) instead of having to bake the horizontal taper into each shader.
  const int kHorizontalBleed = 6;
  const gfx::Rect bar_rect(0, glow_top, fill_w + kHorizontalBleed,
                           view_h - glow_top);
  canvas->SaveLayerWithFlags(cc::PaintFlags());

  // Diffuse glow band — vertical linear gradient with a Skia blur filter on
  // top. The blur runs as a layer effect so the gradient's fade integrates
  // with the blur kernel, producing the soft Dia-style luminous spread
  // rather than a hard-edged shaded rectangle.
  SkPoint points[] = {
      SkPoint::Make(0, static_cast<SkScalar>(core_top)),  // bottom (opaque)
      SkPoint::Make(0, static_cast<SkScalar>(glow_top)),  // top (transparent)
  };
  SkColor4f colors[] = {
      SkColor4f::FromColor(SkColorSetA(kProgressGlow, kGlowMaxAlpha)),
      SkColor4f::FromColor(SkColorSetA(kProgressGlow, 0)),
  };
  cc::PaintFlags glow_flags;
  glow_flags.setAntiAlias(false);
  glow_flags.setStyle(cc::PaintFlags::kFill_Style);
  glow_flags.setShader(cc::PaintShader::MakeLinearGradient(
      points, colors, /*pos=*/nullptr, std::size(points),
      SkTileMode::kClamp));
  glow_flags.setImageFilter(sk_make_sp<cc::BlurPaintFilter>(
      kGlowBlurSigmaX, kGlowBlurSigmaY, SkTileMode::kDecal,
      /*input=*/nullptr));
  // Overscan the rect slightly past the leading edge so the horizontal blur
  // softens the seam between filled and unfilled regions instead of
  // chopping off mid-kernel. The clamp tile mode on the gradient keeps the
  // overscan visually flat.
  canvas->DrawRect(gfx::Rect(0, glow_top, fill_w + kHorizontalBleed,
                             core_top - glow_top),
                   glow_flags);

  // Sharp bright core line at the bottom edge.
  cc::PaintFlags core_flags;
  core_flags.setAntiAlias(false);
  core_flags.setStyle(cc::PaintFlags::kFill_Style);
  core_flags.setColor(SkColorSetA(kProgressGlow, kCoreAlpha));
  canvas->DrawRect(gfx::Rect(0, core_top, fill_w, kCoreHeight), core_flags);

  // Horizontal alpha mask: 30% at the left, 100% at the leading edge.
  // DstIn multiplies the saved layer's alpha by this mask, so the entire
  // bar (glow + core) attenuates the further it is from the head. The
  // floor of 30% keeps the whole bar visible — the fade is a gentle taper
  // rather than dissolving the tail to nothing.
  cc::PaintFlags mask_flags;
  mask_flags.setBlendMode(SkBlendMode::kDstIn);
  SkPoint mask_points[] = {
      SkPoint::Make(0, 0),
      SkPoint::Make(static_cast<SkScalar>(fill_w), 0),
  };
  SkColor4f mask_colors[] = {
      SkColor4f{1.0f, 1.0f, 1.0f, 0.3f},
      SkColor4f{1.0f, 1.0f, 1.0f, 1.0f},
  };
  mask_flags.setShader(cc::PaintShader::MakeLinearGradient(
      mask_points, mask_colors, /*pos=*/nullptr, std::size(mask_colors),
      SkTileMode::kClamp));
  canvas->DrawRect(bar_rect, mask_flags);

  canvas->Restore();
}

void DaoLoadProgressView::AnimationProgressed(const gfx::Animation* animation) {
  if (animation != progress_animation_.get()) {
    return;
  }
  const double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                              animation->GetCurrentValue());
  displayed_progress_ =
      animation_start_progress_ + (target_progress_ - animation_start_progress_) * t;
  SchedulePaint();
}

void DaoLoadProgressView::AnimationEnded(const gfx::Animation* animation) {
  if (animation != progress_animation_.get()) {
    return;
  }
  displayed_progress_ = target_progress_;
  SchedulePaint();
}

void DaoLoadProgressView::AnimationCanceled(const gfx::Animation* animation) {
  // Leave displayed_progress_ where it is.
}

void DaoLoadProgressView::StartProgressAnimation(double from, double to) {
  animation_start_progress_ = from;
  target_progress_ = to;
  if (!progress_animation_) {
    progress_animation_ = std::make_unique<gfx::LinearAnimation>(
        kProgressAnimDuration, gfx::LinearAnimation::kDefaultFrameRate, this);
  } else {
    progress_animation_->Stop();
  }
  progress_animation_->Start();
}

void DaoLoadProgressView::StartFadeOut() {
  if (state_ != State::kCompleting) {
    return;
  }
  state_ = State::kFadingOut;
  ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
  settings.SetTransitionDuration(kFadeOutDuration);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  settings.AddObserver(this);
  layer()->SetOpacity(0.0f);
}

void DaoLoadProgressView::OnImplicitAnimationsCompleted() {
  if (state_ == State::kFadingOut) {
    EnterHidden();
  }
}

void DaoLoadProgressView::EnterHidden() {
  state_ = State::kHidden;
  displayed_progress_ = 0.0;
  target_progress_ = 0.0;
  animation_start_progress_ = 0.0;
  layer()->SetOpacity(0.0f);
  SchedulePaint();
}

}  // namespace dao
