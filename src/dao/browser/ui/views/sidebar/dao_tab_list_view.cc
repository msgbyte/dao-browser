// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_tab_list_view.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/sidebar/dao_new_tab_button.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_section_view.h"
#include "dao/browser/ui/views/sidebar/dao_tab_item_view.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/base/clipboard/clipboard_format_type.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/canvas_painter.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/paint_info.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-shared.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

BEGIN_METADATA(DaoTabListView)
END_METADATA

DaoTabListView::DaoTabListView(Browser* browser)
    : browser_(browser),
      tab_strip_model_(browser->tab_strip_model()) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets(), 0));

  tab_strip_model_->AddObserver(this);
  RebuildTabList();
}

DaoTabListView::~DaoTabListView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoTabListView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  RebuildTabList();
}

void DaoTabListView::TabChangedAt(content::WebContents* contents,
                                  int index,
                                  TabChangeType change_type) {
  // Update tab display (favicon, title, audio) without full rebuild.
  for (const auto& item : tab_items_) {
    if (item->model_index() == index) {
      item->UpdateTab(contents);
      return;
    }
  }
  // Fallback: full rebuild if item not found.
  RebuildTabList();
}

void DaoTabListView::RebuildTabList() {
  RemoveAllChildViews();
  tab_items_.clear();

  const int count = tab_strip_model_->count();
  const int active = tab_strip_model_->active_index();

  // Count pinned tabs
  int pinned_count = 0;
  for (int i = 0; i < count; ++i) {
    if (tab_strip_model_->IsTabPinned(i)) {
      pinned_count++;
    }
  }

  // Pinned section
  if (pinned_count > 0) {
    auto* pinned_section = AddChildView(
        std::make_unique<DaoSidebarSectionView>(u"Pinned"));
    for (int i = 0; i < count; ++i) {
      if (!tab_strip_model_->IsTabPinned(i)) {
        continue;
      }
      auto* contents = tab_strip_model_->GetWebContentsAt(i);
      auto* item = static_cast<DaoTabItemView*>(
          pinned_section->AddTabItem(std::make_unique<DaoTabItemView>(
              browser_, contents, i, i == active,
              base::BindRepeating(&DaoTabListView::OnTabClicked,
                                  base::Unretained(this), i),
              base::BindRepeating(&DaoTabListView::OnTabClosed,
                                  base::Unretained(this), i))));
      item->set_drag_controller(this);
      tab_items_.push_back(item);
    }

    // Separator between pinned and today
    auto sep_wrapper = std::make_unique<views::View>();
    sep_wrapper->SetPreferredSize(gfx::Size(0, 1));
    sep_wrapper->SetBackground(
        views::CreateSolidBackground(dao::kSeparatorColor));
    sep_wrapper->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::VH(8, 10)));
    AddChildView(std::move(sep_wrapper));
  }

  // Unpinned tabs section (includes new tab button as first item)
  auto* today_section = AddChildView(
      std::make_unique<DaoSidebarSectionView>(u""));

  // New tab button — inside the section, same level as tab items.
  new_tab_button_ = static_cast<DaoNewTabButton*>(
      today_section->AddTabItem(
          std::make_unique<DaoNewTabButton>(browser_)));
  if (new_tab_highlighted_) {
    new_tab_button_->SetHighlighted(true);
  }

  for (int i = count - 1; i >= 0; --i) {
    if (tab_strip_model_->IsTabPinned(i)) {
      continue;
    }
    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    auto* item = static_cast<DaoTabItemView*>(
        today_section->AddTabItem(std::make_unique<DaoTabItemView>(
            browser_, contents, i, i == active,
            base::BindRepeating(&DaoTabListView::OnTabClicked,
                                base::Unretained(this), i),
            base::BindRepeating(&DaoTabListView::OnTabClosed,
                                base::Unretained(this), i))));
    item->set_drag_controller(this);
    tab_items_.push_back(item);
  }

  InvalidateLayout();
}

void DaoTabListView::OnTabClicked(int index) {
  if (tab_strip_model_ && index >= 0 && index < tab_strip_model_->count()) {
    // Clicking the already-active tab does nothing.
    if (tab_strip_model_->active_index() == index) {
      return;
    }
    tab_strip_model_->ActivateTabAt(index);
  }
}

void DaoTabListView::OnTabClosed(int index) {
  if (tab_strip_model_ && index >= 0 && index < tab_strip_model_->count()) {
    tab_strip_model_->CloseWebContentsAt(
        index, TabCloseTypes::CLOSE_USER_GESTURE);
  }
}

// DragController implementation

void DaoTabListView::WriteDragDataForView(views::View* sender,
                                          const gfx::Point& press_pt,
                                          ui::OSExchangeData* data) {
  auto* tab_item = static_cast<DaoTabItemView*>(sender);
  drag_source_index_ = tab_item->model_index();
  data->SetString(u"dao-tab-drag");

  // macOS requires a non-zero drag image; without one the drag session
  // crashes in DragDropClientMac::StartDragAndDrop.
  gfx::Size size = sender->size();
  if (!size.IsEmpty()) {
    SkBitmap bitmap;
    {
      ui::CanvasPainter canvas_painter(&bitmap, size, 1.f,
                                       SK_ColorTRANSPARENT, false);
      sender->Paint(views::PaintInfo::CreateRootPaintInfo(
          canvas_painter.context(), size));
    }
    gfx::ImageSkia drag_image =
        gfx::ImageSkia::CreateFromBitmap(bitmap, 1.f);
    data->provider().SetDragImage(
        drag_image, gfx::Vector2d(press_pt.x(), press_pt.y()));
  }
}

int DaoTabListView::GetDragOperationsForView(views::View* sender,
                                             const gfx::Point& p) {
  return ui::DragDropTypes::DRAG_MOVE;
}

bool DaoTabListView::CanStartDragForView(views::View* sender,
                                         const gfx::Point& press_pt,
                                         const gfx::Point& p) {
  gfx::Vector2d delta = p - press_pt;
  return delta.Length() > 5;
}

// Drop target implementation

bool DaoTabListView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::STRING;
  return true;
}

bool DaoTabListView::CanDrop(const ui::OSExchangeData& data) {
  if (drag_source_index_ < 0) {
    return false;
  }
  auto text = data.GetString();
  return text.has_value() && *text == u"dao-tab-drag";
}

int DaoTabListView::OnDragUpdated(const ui::DropTargetEvent& event) {
  return ui::DragDropTypes::DRAG_MOVE;
}

views::View::DropCallback DaoTabListView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  int target = GetDropTargetModelIndex(event.y());
  int source = drag_source_index_;
  drag_source_index_ = -1;

  TabStripModel* model = tab_strip_model_;
  return base::BindOnce(
      [](TabStripModel* m, int from, int to,
         const ui::DropTargetEvent&,
         ui::mojom::DragOperation& op,
         std::unique_ptr<ui::LayerTreeOwner>) {
        if (m && from >= 0 && from < m->count() && to >= 0) {
          m->MoveWebContentsAt(from, std::min(to, m->count() - 1), false);
          op = ui::mojom::DragOperation::kMove;
        }
      },
      model, source, target);
}

void DaoTabListView::SetNewTabHighlighted(bool highlighted) {
  new_tab_highlighted_ = highlighted;
  if (new_tab_button_) {
    new_tab_button_->SetHighlighted(highlighted);
  }
}

int DaoTabListView::GetDropTargetModelIndex(int y_in_view) {
  for (const auto& tab_item : tab_items_) {
    gfx::Point pt(0, tab_item->height() / 2);
    views::View::ConvertPointToTarget(tab_item.get(), this, &pt);
    if (y_in_view < pt.y()) {
      return tab_item->model_index();
    }
  }
  return tab_items_.empty() ? 0 : tab_items_.back()->model_index();
}

}  // namespace dao
