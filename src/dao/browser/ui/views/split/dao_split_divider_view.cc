// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/split/dao_split_divider_view.h"

#include <algorithm>

#include "dao/browser/ui/views/dao_colors.h"
#include "ui/compositor/layer.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/canvas.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/accessibility/view_accessibility.h"

namespace dao {

BEGIN_METADATA(DaoSplitDividerView)
END_METADATA

DaoSplitDividerView::DaoSplitDividerView(DaoSplitBranchNode* branch_node,
                                           DaoSplitView* split_view)
    : branch_node_(branch_node), split_view_(split_view) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kSplitter);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
}

DaoSplitDividerView::~DaoSplitDividerView() = default;

void DaoSplitDividerView::OnPaint(gfx::Canvas* canvas) {
  SkColor color = is_hovered_ || is_dragging_ ? kDividerHoverColor
                                               : kDividerColor;
  canvas->FillRect(GetLocalBounds(), color);
}

bool DaoSplitDividerView::OnMousePressed(const ui::MouseEvent& event) {
  if (!branch_node_)
    return false;
  is_dragging_ = true;
  drag_start_ = event.root_location();
  drag_start_ratio_ = branch_node_->ratio();
  return true;
}

bool DaoSplitDividerView::OnMouseDragged(const ui::MouseEvent& event) {
  if (!is_dragging_ || !branch_node_ || !split_view_)
    return false;

  gfx::Vector2d delta = event.root_location() - drag_start_;
  const gfx::Rect& parent_bounds = branch_node_->bounds();

  float ratio_delta;
  if (branch_node_->direction() == SplitDirection::kHorizontal) {
    int available = parent_bounds.width() - kDividerWidth;
    if (available <= 0)
      return true;
    ratio_delta = static_cast<float>(delta.x()) / available;
  } else {
    int available = parent_bounds.height() - kDividerWidth;
    if (available <= 0)
      return true;
    ratio_delta = static_cast<float>(delta.y()) / available;
  }

  float new_ratio = std::clamp(drag_start_ratio_ + ratio_delta, 0.1f, 0.9f);

  // Enforce minimum pane size.
  if (branch_node_->direction() == SplitDirection::kHorizontal) {
    int available = parent_bounds.width() - kDividerWidth;
    int first_size = static_cast<int>(available * new_ratio);
    int second_size = available - first_size;
    if (first_size < kMinPaneSize || second_size < kMinPaneSize)
      return true;
  } else {
    int available = parent_bounds.height() - kDividerWidth;
    int first_size = static_cast<int>(available * new_ratio);
    int second_size = available - first_size;
    if (first_size < kMinPaneSize || second_size < kMinPaneSize)
      return true;
  }

  branch_node_->set_ratio(new_ratio);
  split_view_->OnDividerDragged();
  return true;
}

void DaoSplitDividerView::OnMouseReleased(const ui::MouseEvent& event) {
  if (is_dragging_) {
    is_dragging_ = false;
    if (split_view_) {
      split_view_->OnDividerDragFinished();
    }
  }
}

void DaoSplitDividerView::OnMouseEntered(const ui::MouseEvent& event) {
  is_hovered_ = true;
  SchedulePaint();
}

void DaoSplitDividerView::OnMouseExited(const ui::MouseEvent& event) {
  is_hovered_ = false;
  SchedulePaint();
}

ui::Cursor DaoSplitDividerView::GetCursor(const ui::MouseEvent& event) {
  if (!branch_node_)
    return ui::Cursor();
  return branch_node_->direction() == SplitDirection::kHorizontal
             ? ui::mojom::CursorType::kColumnResize
             : ui::mojom::CursorType::kRowResize;
}

}  // namespace dao
