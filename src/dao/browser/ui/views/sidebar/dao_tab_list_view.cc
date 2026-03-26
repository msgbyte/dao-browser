// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_tab_list_view.h"

#include <algorithm>
#include <map>
#include <set>

#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
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
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {

std::u16string BuildSplitGroupTitle(
    const std::vector<content::WebContents*>& contents) {
  std::u16string title;
  for (size_t i = 0; i < contents.size(); ++i) {
    std::u16string item_title =
        contents[i] && !contents[i]->GetTitle().empty()
            ? contents[i]->GetTitle()
            : u"Tab";
    if (!title.empty()) {
      title += u"  |  ";
    }
    title += item_title;
  }
  return title.empty() ? u"Split view" : title;
}

std::u16string GetSplitPreviewTitle(content::WebContents* contents) {
  if (!contents || contents->GetTitle().empty()) {
    return u"Tab";
  }
  return contents->GetTitle();
}

std::unique_ptr<views::View> CreateSplitPreviewChip(const std::u16string& title,
                                                    bool is_active) {
  auto chip = std::make_unique<views::View>();
  auto* layout = chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(0, 8), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  chip->SetPreferredSize(gfx::Size(0, 28));
  chip->SetBackground(views::CreateRoundedRectBackground(
      is_active ? dao::kActiveTabBackground : dao::kAddressBarBackground, 10));

  auto* label = chip->AddChildView(std::make_unique<views::Label>(title));
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetSubpixelRenderingEnabled(false);
  label->SetEnabledColor(is_active ? dao::kTextPrimary : dao::kTextSecondary);
  label->SetFontList(gfx::FontList({"sans-serif"}, gfx::Font::FontStyle::NORMAL,
                                   12, gfx::Font::Weight::MEDIUM));
  layout->SetFlexForView(label, 1);
  return chip;
}

class DaoSplitGroupItemView : public views::Button {
  METADATA_HEADER(DaoSplitGroupItemView, views::Button)

 public:
  DaoSplitGroupItemView(const std::vector<content::WebContents*>& contents,
                        bool is_active,
                        base::RepeatingClosure on_click)
      : Button(std::move(on_click)) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(0, 10), 6));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto* previews = AddChildView(std::make_unique<views::View>());
    auto* previews_layout =
        previews->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    previews_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    layout->SetFlexForView(previews, 1);

    const size_t visible_count = std::min<size_t>(contents.size(), 2);
    for (size_t i = 0; i < visible_count; ++i) {
      auto chip = CreateSplitPreviewChip(GetSplitPreviewTitle(contents[i]),
                                         is_active);
      previews_layout->SetFlexForView(previews->AddChildView(std::move(chip)), 1);
    }

    if (contents.size() > visible_count) {
      previews->AddChildView(CreateSplitPreviewChip(
          u"+" + base::NumberToString16(static_cast<int>(contents.size() -
                                                         visible_count)),
          is_active));
    }

    if (is_active) {
      SetBackground(views::CreateRoundedRectBackground(
          dao::kActiveTabBackground, 12));
    }

    views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
    views::InkDrop::Get(this)->SetBaseColor(dao::kInkDropBase);
    views::InkDrop::Get(this)->SetVisibleOpacity(dao::kInkDropOpacity);
    views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(), 12);
    SetInstallFocusRingOnFocus(false);
    SetPreferredSize(gfx::Size(0, 40));
    SetAccessibleName(BuildSplitGroupTitle(contents));
  }
};

BEGIN_METADATA(DaoSplitGroupItemView)
END_METADATA

}  // namespace

class DaoTabDropIndicatorView : public views::View {
  METADATA_HEADER(DaoTabDropIndicatorView, views::View)

 public:
  DaoTabDropIndicatorView() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }

  void SetActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    SchedulePaint();
  }

  void SetIndicatorY(int y) {
    if (indicator_y_ == y) return;
    indicator_y_ = y;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    if (!active_ || indicator_y_ < 0) return;

    constexpr int kLineInset = 12;
    constexpr int kLineHeight = 2;
    constexpr int kDotRadius = 3;
    SkColor line_color = SkColorSetA(dao::kSpaceActive, 200);

    cc::PaintFlags flags;
    flags.setColor(line_color);
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);

    int line_y = indicator_y_ - kLineHeight / 2;
    canvas->DrawRect(
        gfx::Rect(kLineInset, line_y,
                   width() - 2 * kLineInset, kLineHeight),
        flags);

    canvas->DrawCircle(gfx::Point(kLineInset, indicator_y_),
                       kDotRadius, flags);
    canvas->DrawCircle(gfx::Point(width() - kLineInset, indicator_y_),
                       kDotRadius, flags);
  }

 private:
  bool active_ = false;
  int indicator_y_ = -1;
};

BEGIN_METADATA(DaoTabDropIndicatorView)
END_METADATA

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

void DaoTabListView::RefreshForSplitStateChange() {
  RebuildTabList();
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

  struct SplitGroupEntry {
    std::vector<content::WebContents*> contents;
    std::set<int> indices;
    int representative_index = TabStripModel::kNoTab;
    bool is_active = false;
  };

  std::vector<SplitGroupEntry> split_groups;
  std::map<int, size_t> split_group_by_index;
  if (auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_)) {
    if (auto* split_view = browser_view->dao_split_view()) {
      for (const auto& summary : split_view->GetSplitGroupSummaries()) {
        SplitGroupEntry entry;
        entry.contents = summary.contents;
        entry.is_active = summary.is_active;
        if (summary.representative) {
          entry.representative_index =
              tab_strip_model_->GetIndexOfWebContents(summary.representative);
        }

        for (content::WebContents* contents : summary.contents) {
          int index = tab_strip_model_->GetIndexOfWebContents(contents);
          if (index == TabStripModel::kNoTab || tab_strip_model_->IsTabPinned(index)) {
            continue;
          }
          entry.indices.insert(index);
        }

        if (entry.indices.size() < 2) {
          continue;
        }

        if (entry.representative_index == TabStripModel::kNoTab) {
          entry.representative_index = *entry.indices.rbegin();
        }

        size_t group_index = split_groups.size();
        for (int index : entry.indices) {
          split_group_by_index[index] = group_index;
        }
        split_groups.push_back(std::move(entry));
      }
    }
  }

  std::set<size_t> rendered_split_groups;

  for (int i = count - 1; i >= 0; --i) {
    if (tab_strip_model_->IsTabPinned(i)) {
      continue;
    }
    auto split_group_it = split_group_by_index.find(i);
    if (split_group_it != split_group_by_index.end()) {
      size_t split_group_index = split_group_it->second;
      if (rendered_split_groups.contains(split_group_index)) {
        continue;
      }
      rendered_split_groups.insert(split_group_index);
      const SplitGroupEntry& group = split_groups[split_group_index];
      today_section->AddTabItem(std::make_unique<DaoSplitGroupItemView>(
          group.contents, group.is_active,
          base::BindRepeating(&DaoTabListView::OnTabClicked,
                              base::Unretained(this),
                              group.representative_index)));
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

  drop_indicator_ = AddChildView(std::make_unique<DaoTabDropIndicatorView>());
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
  data->SetString(u"dao-tab-drag:" +
                   base::NumberToString16(tab_item->model_index()));

  // Notify split view that a tab drag has started.
  if (tab_drag_callback_)
    tab_drag_callback_.Run(true);

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
  return text.has_value() && text->starts_with(u"dao-tab-drag");
}

void DaoTabListView::OnDragEntered(const ui::DropTargetEvent& event) {
  auto* indicator = static_cast<DaoTabDropIndicatorView*>(drop_indicator_.get());
  indicator->SetActive(true);
}

int DaoTabListView::OnDragUpdated(const ui::DropTargetEvent& event) {
  auto target = ComputeDropTarget(event.y());
  auto* indicator = static_cast<DaoTabDropIndicatorView*>(drop_indicator_.get());
  indicator->SetIndicatorY(target.indicator_y);
  return ui::DragDropTypes::DRAG_MOVE;
}

void DaoTabListView::OnDragExited() {
  auto* indicator = static_cast<DaoTabDropIndicatorView*>(drop_indicator_.get());
  indicator->SetActive(false);
  indicator->SetIndicatorY(-1);
}

views::View::DropCallback DaoTabListView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  auto target = ComputeDropTarget(event.y());
  int source = drag_source_index_;
  drag_source_index_ = -1;

  auto* indicator = static_cast<DaoTabDropIndicatorView*>(drop_indicator_.get());
  indicator->SetActive(false);
  indicator->SetIndicatorY(-1);

  // Notify split view that the tab drag has ended (dropped back on tab list).
  if (tab_drag_callback_)
    tab_drag_callback_.Run(false);

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
      model, source, target.model_index);
}

void DaoTabListView::SetNewTabHighlighted(bool highlighted) {
  new_tab_highlighted_ = highlighted;
  if (new_tab_button_) {
    new_tab_button_->SetHighlighted(highlighted);
  }
}

void DaoTabListView::Layout(PassKey) {
  LayoutSuperclass<views::View>(this);
  if (drop_indicator_) {
    drop_indicator_->SetBoundsRect(gfx::Rect(0, 0, width(), height()));
  }
}

DaoTabListView::DropTarget DaoTabListView::ComputeDropTarget(int y_in_view) {
  for (const auto& tab_item : tab_items_) {
    gfx::Point mid(0, tab_item->height() / 2);
    views::View::ConvertPointToTarget(tab_item.get(), this, &mid);
    if (y_in_view < mid.y()) {
      int idx = tab_item->model_index();
      int target = idx;
      // MoveWebContentsAt uses remove-then-insert semantics, so the target
      // index must account for the shift caused by removing the source tab.
      // Unpinned tabs are displayed in reverse model order, so visually
      // "above" means higher model index; pinned tabs are in normal order.
      if (drag_source_index_ >= 0) {
        bool unpinned = !tab_strip_model_->IsTabPinned(idx);
        if (unpinned && drag_source_index_ > idx) {
          target = idx + 1;
        } else if (!unpinned && drag_source_index_ < idx) {
          target = idx - 1;
        }
      }
      gfx::Point top(0, 0);
      views::View::ConvertPointToTarget(tab_item.get(), this, &top);
      return {target, top.y()};
    }
  }
  if (tab_items_.empty()) {
    return {0, -1};
  }
  auto* last = tab_items_.back().get();
  int idx = last->model_index();
  gfx::Point bottom(0, last->height());
  views::View::ConvertPointToTarget(last, this, &bottom);
  return {idx, bottom.y()};
}

}  // namespace dao
