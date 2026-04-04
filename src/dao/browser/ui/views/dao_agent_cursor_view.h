// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_AGENT_CURSOR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_AGENT_CURSOR_VIEW_H_

#include "base/functional/callback.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/view.h"

namespace dao {

// Renders a virtual mouse cursor on top of web content to visualize
// AI agent actions. Shows a branded purple arrow pointer with smooth
// Bezier movement animation and click ripple effects.
// This view is transparent and click-through — it never intercepts events.
class DaoAgentCursorView : public views::View {
  METADATA_HEADER(DaoAgentCursorView, views::View)

 public:
  using AnimationCompleteCallback = base::OnceClosure;

  DaoAgentCursorView();
  ~DaoAgentCursorView() override;

  DaoAgentCursorView(const DaoAgentCursorView&) = delete;
  DaoAgentCursorView& operator=(const DaoAgentCursorView&) = delete;

  // Show cursor at the center of the content area.
  void ShowAtCenter();

  // Hide and reset all state.
  void Hide();

  // Smoothly animate cursor from current position to (x, y) in view coords.
  // Calls |callback| when animation completes.
  void AnimateTo(float x, float y, AnimationCompleteCallback callback);

  // Play the click ripple effect at the current cursor position.
  void PlayClickRipple();

  bool is_visible() const { return cursor_visible_; }

 protected:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  enum class State { kIdle, kMoving, kArrived, kClicking };

  void OnAnimationTick();
  void OnRippleTick();
  float EaseOut(float t) const;
  int ComputeMoveDurationMs(float distance) const;
  void PaintCursor(gfx::Canvas* canvas);
  void PaintRipple(gfx::Canvas* canvas);

  State state_ = State::kIdle;
  bool cursor_visible_ = false;

  // Current cursor position in view coordinates.
  gfx::PointF cursor_pos_;

  // Move animation state.
  gfx::PointF move_start_;
  gfx::PointF move_end_;
  base::TimeTicks move_start_time_;
  base::TimeDelta move_duration_;
  base::RepeatingTimer move_timer_;
  AnimationCompleteCallback move_complete_callback_;

  // Click ripple animation state.
  gfx::PointF ripple_center_;
  base::TimeTicks ripple_start_time_;
  base::RepeatingTimer ripple_timer_;
  static constexpr base::TimeDelta kRippleDuration = base::Milliseconds(400);
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_AGENT_CURSOR_VIEW_H_
