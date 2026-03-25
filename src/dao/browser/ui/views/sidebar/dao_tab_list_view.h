// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_LIST_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_LIST_VIEW_H_

#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/views/drag_controller.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace dao {

class DaoNewTabButton;
class DaoTabItemView;

class DaoTabListView : public views::View,
                       public TabStripModelObserver,
                       public views::DragController {
  METADATA_HEADER(DaoTabListView, views::View)

 public:
  explicit DaoTabListView(Browser* browser);
  DaoTabListView(const DaoTabListView&) = delete;
  DaoTabListView& operator=(const DaoTabListView&) = delete;
  ~DaoTabListView() override;

  void set_show_omnibox_callback(base::RepeatingClosure callback) {
    show_omnibox_callback_ = std::move(callback);
  }

  void SetNewTabHighlighted(bool highlighted);

  const std::vector<raw_ptr<DaoTabItemView>>& tab_items() const {
    return tab_items_;
  }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void TabChangedAt(content::WebContents* contents,
                    int index,
                    TabChangeType change_type) override;

  // views::DragController:
  void WriteDragDataForView(views::View* sender,
                            const gfx::Point& press_pt,
                            ui::OSExchangeData* data) override;
  int GetDragOperationsForView(views::View* sender,
                               const gfx::Point& p) override;
  bool CanStartDragForView(views::View* sender,
                           const gfx::Point& press_pt,
                           const gfx::Point& p) override;

  // views::View overrides:
  void Layout(PassKey) override;

  // Drop target (views::View):
  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override;
  bool CanDrop(const ui::OSExchangeData& data) override;
  void OnDragEntered(const ui::DropTargetEvent& event) override;
  int OnDragUpdated(const ui::DropTargetEvent& event) override;
  void OnDragExited() override;
  DropCallback GetDropCallback(const ui::DropTargetEvent& event) override;

 private:
  struct DropTarget {
    int model_index = -1;
    int indicator_y = -1;
  };

  void RebuildTabList();
  void OnTabClicked(int index);
  void OnTabClosed(int index);
  DropTarget ComputeDropTarget(int y_in_view);

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
  std::vector<raw_ptr<DaoTabItemView>> tab_items_;
  raw_ptr<DaoNewTabButton> new_tab_button_ = nullptr;
  raw_ptr<views::View> drop_indicator_ = nullptr;
  int drag_source_index_ = -1;
  bool new_tab_highlighted_ = false;
  base::RepeatingClosure show_omnibox_callback_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_LIST_VIEW_H_
