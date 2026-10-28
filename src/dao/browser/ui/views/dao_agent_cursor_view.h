// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_AGENT_CURSOR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_AGENT_CURSOR_VIEW_H_

#include <optional>

#include "base/containers/circular_deque.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/view.h"

class BrowserView;

namespace content {
class WebContents;
}  // namespace content

namespace dao {

bool CanAnimateAgentCursorForTarget(content::WebContents* target);

// Returns the focus-bracket frame for |target_bounds| inside |viewport|, both
// in cursor view coordinates, or nullopt when the target is off screen or
// fills most of the viewport.
std::optional<gfx::RectF> ComputeAgentCursorFrame(
    const gfx::RectF& target_bounds,
    const gfx::RectF& viewport);

// Animates the agent cursor of |browser_view| to (x, y) in |target|'s
// viewport, in CSS pixels, and frames |target_bounds| (same coordinates) when
// given. Callers first check CanAnimateAgentCursorForTarget(). |callback|
// receives true only when the move finished while |target| could still
// animate.
void AnimateAgentCursorInViewport(BrowserView* browser_view,
                                  content::WebContents* target,
                                  double x,
                                  double y,
                                  std::optional<gfx::RectF> target_bounds,
                                  base::OnceCallback<void(bool)> callback);

// Renders a virtual cursor on top of web content to visualize AI agent
// actions: a white-core blue orb with a fading motion trail, focus brackets
// that frame the target element, and click shock waves. The cursor stays at
// its last position while the tab is agent-locked, dims when idle, and hides
// when the lock ends. Unlocked cursors fade out after a longer pause. This
// view is transparent and click-through — it never intercepts events.
class DaoAgentCursorView : public views::View {
  METADATA_HEADER(DaoAgentCursorView, views::View)

 public:
  using AnimationCompleteCallback = base::OnceCallback<void(bool)>;

  DaoAgentCursorView();
  ~DaoAgentCursorView() override;

  DaoAgentCursorView(const DaoAgentCursorView&) = delete;
  DaoAgentCursorView& operator=(const DaoAgentCursorView&) = delete;

  // Cancel any active move and show cursor at the center of the content area.
  void ShowAtCenter();

  // Hide and reset all state.
  void Hide();

  // Cancel movement and effects, keeping a visible locked cursor in place.
  void CancelAnimation();

  // A newly active tab shows the cursor at the center when it is agent-locked
  // and hides it otherwise.
  void OnActiveTabChanged(bool locked);

  // Other updates of the active tab only act when its lock starts or ends, so
  // page loads and title changes never move, reveal, or hide the cursor.
  void OnActiveTabUpdated(bool locked);

  // Smoothly animate cursor from current position to (x, y) in view coords.
  // A hidden cursor first fades in near the target. |target_frame|, in view
  // coords, is drawn as focus brackets; the cursor then waits for the click
  // before it starts idling. Calls |callback| with true on arrival, or false
  // if canceled or already moving.
  void AnimateTo(float x,
                 float y,
                 std::optional<gfx::RectF> target_frame,
                 AnimationCompleteCallback callback);

  // Play the click shock waves at the current cursor position.
  void PlayClickRipple();

  bool is_visible() const { return cursor_visible_; }
  bool uses_curved_path_for_testing() const { return uses_curved_path_; }
  const std::optional<gfx::RectF>& target_frame_for_testing() const {
    return target_frame_;
  }
  bool is_animating_for_testing() const { return frame_timer_.IsRunning(); }
  gfx::PointF cursor_position_for_testing() const { return cursor_pos_; }
  void SetIdleDelaysForTesting(base::TimeDelta dim_delay,
                               base::TimeDelta hide_delay);

 protected:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  enum class State { kIdle, kMoving, kArrived, kClicking };

  struct TrailPoint {
    gfx::PointF position;
    base::TimeTicks time;
  };

  void ShowAt(const gfx::PointF& position);
  void Wake();
  void AnimateOpacity(float opacity,
                      base::TimeDelta duration,
                      bool hide_when_done);
  void StartFrames();
  void OnFrame();
  void StartIdleTimer();
  void OnIdleDim();
  void OnIdleHide();
  bool IsAnimating(base::TimeTicks now) const;
  base::TimeDelta AimDuration() const;
  void PaintTargetFrame(gfx::Canvas* canvas, base::TimeTicks now);
  void PaintTrail(gfx::Canvas* canvas, base::TimeTicks now);
  void PaintOrb(gfx::Canvas* canvas, base::TimeTicks now);
  void PaintClickWaves(gfx::Canvas* canvas, base::TimeTicks now);

  State state_ = State::kIdle;
  bool cursor_visible_ = false;
  bool active_tab_locked_ = false;

  // Current cursor position in view coordinates.
  gfx::PointF cursor_pos_;

  // Move animation state.
  gfx::PointF move_start_;
  gfx::PointF move_end_;
  gfx::PointF move_control_;
  bool uses_curved_path_ = false;
  base::TimeTicks move_start_time_;
  base::TimeDelta move_duration_;
  AnimationCompleteCallback move_complete_callback_;
  base::circular_deque<TrailPoint> trail_;

  // Appear scale and arrival breath. Opacity runs on layer animations.
  std::optional<base::TimeTicks> show_time_;
  std::optional<base::TimeTicks> arrive_time_;

  // Focus brackets around the target element.
  std::optional<gfx::RectF> target_frame_;
  base::TimeTicks aim_start_time_;
  std::optional<base::TimeTicks> aim_fade_start_time_;

  base::TimeTicks click_start_time_;

  base::TimeDelta idle_dim_delay_;
  base::TimeDelta idle_hide_delay_;
  base::RepeatingTimer frame_timer_;
  base::OneShotTimer idle_timer_;

  base::WeakPtrFactory<DaoAgentCursorView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_AGENT_CURSOR_VIEW_H_
