// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_FLYOUT_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_FLYOUT_VIEW_H_

#include "base/functional/callback.h"
#include "ui/gfx/animation/animation_delegate.h"
#include "ui/gfx/animation/linear_animation.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/view.h"

namespace views {
class ImageView;
}  // namespace views

namespace dao {

// Overlay that animates a file icon along a quadratic Bezier curve from a
// start point (e.g., top-right of content area) to the download button in
// the sidebar.  Covers the entire BrowserView, is click-through, and hides
// itself when the animation completes.
class DaoDownloadFlyoutView : public views::View,
                               public gfx::AnimationDelegate {
  METADATA_HEADER(DaoDownloadFlyoutView, views::View)

 public:
  DaoDownloadFlyoutView();
  DaoDownloadFlyoutView(const DaoDownloadFlyoutView&) = delete;
  DaoDownloadFlyoutView& operator=(const DaoDownloadFlyoutView&) = delete;
  ~DaoDownloadFlyoutView() override;

  // Starts the arc animation from |start| to |end| (both in BrowserView
  // coordinates).  |on_complete| is called when the animation finishes.
  void StartAnimation(const gfx::Point& start,
                      const gfx::Point& end,
                      base::OnceClosure on_complete);

  bool is_animating() const { return animation_.is_animating(); }

  // gfx::AnimationDelegate:
  void AnimationProgressed(const gfx::Animation* animation) override;
  void AnimationEnded(const gfx::Animation* animation) override;

 private:
  static constexpr int kIconSize = 28;
  static constexpr base::TimeDelta kDuration = base::Milliseconds(600);

  // Evaluates the quadratic Bezier at parameter t ∈ [0, 1].
  gfx::PointF BezierAt(double t) const;

  raw_ptr<views::ImageView> icon_view_ = nullptr;
  gfx::LinearAnimation animation_;

  gfx::PointF start_;
  gfx::PointF end_;
  gfx::PointF control_;  // Bezier control point

  base::OnceClosure on_complete_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_FLYOUT_VIEW_H_
