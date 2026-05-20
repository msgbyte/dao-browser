// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_VIEW_H_

#include <memory>

#include "base/timer/timer.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/views/view.h"

namespace dao {

// Thin (2 px) blue progress bar with a soft halo, painted along the top edge
// of the rounded content card. Driven by DaoLoadProgressController which
// pushes real load progress values from the active tab's WebContents.
//
// Visual states (internal, not exposed):
//   Hidden     — opacity 0, no paint
//   Loading    — opacity 1, displayed_progress_ eases toward target_progress_
//   Completing — target pinned to 1.0, brief hold timer running
//   FadingOut  — layer opacity animates 1 → 0
class DaoLoadProgressView : public views::View,
                            public gfx::AnimationDelegate,
                            public ui::ImplicitAnimationObserver {
  METADATA_HEADER(DaoLoadProgressView, views::View)

 public:
  DaoLoadProgressView();
  DaoLoadProgressView(const DaoLoadProgressView&) = delete;
  DaoLoadProgressView& operator=(const DaoLoadProgressView&) = delete;
  ~DaoLoadProgressView() override;

  // Called by DaoLoadProgressController. `animate=false` snaps without easing
  // (used on tab switch and on backward progress).
  void SetTargetProgress(double progress, bool animate);
  // Reset to 0, enter Loading state, make visible.
  void StartLoading();
  // Pin to 1.0, hold ~150 ms, then fade out.
  void FinishLoading();
  // Cancel everything and hide instantly (no fade).
  void HideImmediately();

  // For tests.
  double displayed_progress_for_testing() const { return displayed_progress_; }
  bool is_loading_for_testing() const { return state_ == State::kLoading; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;
  void AnimationCanceled(const gfx::Animation* animation) override;

  // ui::ImplicitAnimationObserver:
  void OnImplicitAnimationsCompleted() override;

 private:
  enum class State {
    kHidden,
    kLoading,
    kCompleting,
    kFadingOut,
  };

  void StartProgressAnimation(double from, double to);
  void StartFadeOut();
  void EnterHidden();

  State state_ = State::kHidden;
  double displayed_progress_ = 0.0;
  double target_progress_ = 0.0;
  double animation_start_progress_ = 0.0;

  std::unique_ptr<gfx::LinearAnimation> progress_animation_;
  base::OneShotTimer hold_timer_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_VIEW_H_
