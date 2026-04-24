// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_download_flyout_view.h"

#include "components/vector_icons/vector_icons.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/controls/image_view.h"

namespace dao {

BEGIN_METADATA(DaoDownloadFlyoutView)
END_METADATA

DaoDownloadFlyoutView::DaoDownloadFlyoutView()
    : animation_(kDuration, 60, this) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetCanProcessEventsWithinSubtree(false);
  SetVisible(false);

  auto icon = std::make_unique<views::ImageView>();
  icon->SetPreferredSize(gfx::Size(kIconSize, kIconSize));
  icon->SetImage(gfx::CreateVectorIcon(vector_icons::kFileDownloadIcon,
                                        kIconSize, TextPrimary()));
  icon->SetPaintToLayer();
  icon->layer()->SetFillsBoundsOpaquely(false);
  icon_view_ = AddChildView(std::move(icon));
}

DaoDownloadFlyoutView::~DaoDownloadFlyoutView() = default;

void DaoDownloadFlyoutView::StartAnimation(const gfx::Point& start,
                                            const gfx::Point& end,
                                            base::OnceClosure on_complete) {
  start_ = gfx::PointF(start);
  end_ = gfx::PointF(end);
  on_complete_ = std::move(on_complete);

  // Control point: placed above and between start and end, creating a
  // natural parabolic arc — the icon rises slightly then falls toward
  // the download button in the bottom-left sidebar.
  //
  //            * 控制点 (上方)
  //           / \
  //          /   \
  //   起点 /     \ 终点 (下载按钮)
  //
  float mid_x = (start_.x() + end_.x()) * 0.5f;
  float top_y = std::min(start_.y(), end_.y());
  control_ = gfx::PointF(mid_x, top_y - 80.0f);

  // Position the icon at the start.
  icon_view_->SetBounds(static_cast<int>(start_.x()) - kIconSize / 2,
                        static_cast<int>(start_.y()) - kIconSize / 2,
                        kIconSize, kIconSize);

  SetVisible(true);
  icon_view_->layer()->SetOpacity(1.0f);
  animation_.Stop();
  animation_.Start();
}

gfx::PointF DaoDownloadFlyoutView::BezierAt(double t) const {
  // Quadratic Bezier: P = (1-t)²·P0 + 2(1-t)t·P1 + t²·P2
  float ft = static_cast<float>(t);
  float inv = 1.0f - ft;
  float x = inv * inv * start_.x() + 2.0f * inv * ft * control_.x() +
            ft * ft * end_.x();
  float y = inv * inv * start_.y() + 2.0f * inv * ft * control_.y() +
            ft * ft * end_.y();
  return gfx::PointF(x, y);
}

void DaoDownloadFlyoutView::AnimationProgressed(
    const gfx::Animation* animation) {
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                         animation->GetCurrentValue());
  gfx::PointF pos = BezierAt(t);
  icon_view_->SetBounds(static_cast<int>(pos.x()) - kIconSize / 2,
                        static_cast<int>(pos.y()) - kIconSize / 2,
                        kIconSize, kIconSize);

  // Fade out in the last 15% of the animation.
  float opacity = 1.0f;
  if (t > 0.85) {
    opacity = static_cast<float>((1.0 - t) / 0.15);
  }
  icon_view_->layer()->SetOpacity(opacity);
}

void DaoDownloadFlyoutView::AnimationEnded(const gfx::Animation* animation) {
  SetVisible(false);
  if (on_complete_) {
    std::move(on_complete_).Run();
  }
}

}  // namespace dao
