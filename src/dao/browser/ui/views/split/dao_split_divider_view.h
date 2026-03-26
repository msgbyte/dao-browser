// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_DIVIDER_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_DIVIDER_VIEW_H_

#include "base/functional/callback.h"
#include "dao/browser/ui/views/split/dao_split_node.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/views/view.h"

namespace dao {

class DaoSplitView;

// Draggable divider bar between two sibling panes.  Adjusts the parent
// branch node's ratio_ when the user drags.
class DaoSplitDividerView : public views::View {
  METADATA_HEADER(DaoSplitDividerView, views::View)

 public:
  using RatioChangedCallback =
      base::RepeatingCallback<void(DaoSplitBranchNode*, float)>;

  DaoSplitDividerView(DaoSplitBranchNode* branch_node,
                       DaoSplitView* split_view);
  DaoSplitDividerView(const DaoSplitDividerView&) = delete;
  DaoSplitDividerView& operator=(const DaoSplitDividerView&) = delete;
  ~DaoSplitDividerView() override;

  DaoSplitBranchNode* branch_node() const { return branch_node_; }

  // views::View:
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  ui::Cursor GetCursor(const ui::MouseEvent& event) override;

 private:
  raw_ptr<DaoSplitBranchNode> branch_node_;
  raw_ptr<DaoSplitView> split_view_;
  bool is_dragging_ = false;
  bool is_hovered_ = false;
  gfx::Point drag_start_;
  float drag_start_ratio_ = 0.5f;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SPLIT_DAO_SPLIT_DIVIDER_VIEW_H_
