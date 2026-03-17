// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"

#include <algorithm>

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/sidebar/dao_download_button_view.h"
#include "dao/browser/ui/views/sidebar/dao_favorites_view.h"
#include "dao/browser/ui/views/sidebar/dao_new_tab_button.h"
#include "dao/browser/ui/views/sidebar/dao_tab_item_view.h"
#include "dao/browser/ui/views/sidebar/dao_tab_list_view.h"
#include "net/base/filename_util.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-shared.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/page_transition_types.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_tree_owner.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/background.h"
#include "ui/views/style/typography.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

// Transparent overlay that paints on its own layer above all sidebar content.
class DaoDropOverlayView : public views::View {
  METADATA_HEADER(DaoDropOverlayView, views::View)

 public:
  DaoDropOverlayView() {
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

BEGIN_METADATA(DaoDropOverlayView)
END_METADATA

BEGIN_METADATA(DaoSidebarView)
END_METADATA

DaoSidebarView::DaoSidebarView(Browser* browser)
    : browser_(browser),
      collapse_animation_(base::Milliseconds(50), 60, this) {
  SetPaintToLayer();
  layer()->SetMasksToBounds(true);

  // Inner container always keeps full width; outer view clips it
  inner_container_ = AddChildView(std::make_unique<views::View>());
  auto* layout = inner_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::TLBR(0, 4, 0, 4), 16));
  inner_container_->SetBackground(
      views::CreateSolidBackground(dao::kSidebarBackground));

  // Header row: traffic-light spacer + toggle sidebar button
  auto header_row = std::make_unique<views::View>();
  auto* header_layout = header_row->SetLayoutManager(
      std::make_unique<views::FlexLayout>());
  header_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  header_layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  // Left inset clears macOS traffic lights (~70px)
  header_layout->SetInteriorMargin(gfx::Insets::TLBR(0, 70, 0, 0));
  header_row->SetPreferredSize(gfx::Size(0, 36));

  // Flexible spacer pushes toggle button to the right
  auto spacer = std::make_unique<views::View>();
  spacer->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded));
  header_row->AddChildView(std::move(spacer));

  // Toggle sidebar button
  auto toggle_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoSidebarView::ToggleCollapsed,
                          base::Unretained(this)),
      u"\u2630");  // ☰ hamburger icon
  toggle_btn->SetEnabledTextColors(dao::kTextSecondary);
  toggle_btn->SetTextSubpixelRenderingEnabled(false);
  toggle_btn->SetLabelStyle(views::style::STYLE_BODY_1);
  toggle_btn->SetPreferredSize(gfx::Size(32, 32));
  toggle_btn->SetInstallFocusRingOnFocus(false);
  toggle_btn->SetTooltipText(u"Toggle Sidebar (\u2318S)");
  toggle_button_ = header_row->AddChildView(std::move(toggle_btn));

  inner_container_->AddChildView(std::move(header_row));

  // Favorites row
  favorites_ = inner_container_->AddChildView(
      std::make_unique<DaoFavoritesView>(browser));

  // Tab list (pinned section + new-tab button + today section)
  tab_list_view_ = inner_container_->AddChildView(
      std::make_unique<DaoTabListView>(browser));
  layout->SetFlexForView(tab_list_view_, 1);

  // Wire active-tab click to show floating omnibox
  tab_list_view_->set_show_omnibox_callback(
      base::BindRepeating(&DaoSidebarView::ShowOmniboxPopup,
                          base::Unretained(this)));

  // Download button at bottom
  download_button_ = inner_container_->AddChildView(
      std::make_unique<DaoDownloadButtonView>(browser));

  // Resize handle on the right edge
  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));

  // Drop overlay — has its own layer so it paints above all sidebar content.
  drop_overlay_ = AddChildView(std::make_unique<DaoDropOverlayView>());
}

DaoSidebarView::~DaoSidebarView() = default;

gfx::Size DaoSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(current_width_, 0);
}

void DaoSidebarView::Layout(PassKey) {
  if (inner_container_) {
    // During collapse/expand animation, keep inner at user_width_ so content
    // is clipped smoothly. During resize, use current_width_ so background
    // fills the entire sidebar and content reflows in real time.
    int container_width =
        (collapsed_ || collapse_animation_.is_animating()) ? user_width_
                                                           : current_width_;
    inner_container_->SetBoundsRect(
        gfx::Rect(0, 0, container_width, height()));
  }
  if (resize_area_) {
    resize_area_->SetVisible(!collapsed_);
    resize_area_->SetBoundsRect(
        gfx::Rect(width() - kResizeAreaWidth, 0, kResizeAreaWidth, height()));
  }
  if (drop_overlay_) {
    drop_overlay_->SetBoundsRect(gfx::Rect(0, 0, width(), height()));
  }
}

// --- Resize ---------------------------------------------------------------

void DaoSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (collapsed_) {
    return;
  }
  if (!is_resizing_) {
    is_resizing_ = true;
    resize_start_width_ = current_width_;
  }
  int new_width = resize_start_width_ + resize_amount;
  new_width = std::clamp(new_width, kMinWidth, kMaxWidth);
  current_width_ = new_width;
  if (done_resizing) {
    is_resizing_ = false;
    user_width_ = new_width;
    target_width_ = new_width;
  }
  PreferredSizeChanged();
}

// --- Collapse / expand ---------------------------------------------------

void DaoSidebarView::ToggleCollapsed() {
  auto_expanded_ = false;
  collapsed_ = !collapsed_;
  start_width_ = current_width_;
  target_width_ = collapsed_ ? kCollapsedWidth : user_width_;
  collapse_animation_.Stop();
  collapse_animation_.Start();
}

void DaoSidebarView::AnimationProgressed(const gfx::Animation* animation) {
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                         animation->GetCurrentValue());
  current_width_ =
      start_width_ + static_cast<int>((target_width_ - start_width_) * t);
  PreferredSizeChanged();
}

void DaoSidebarView::AnimationEnded(const gfx::Animation* animation) {
  current_width_ = target_width_;
  PreferredSizeChanged();
}

// --- Keyboard shortcut (Cmd+\) -------------------------------------------

void DaoSidebarView::AddedToWidget() {
  View::AddedToWidget();
  if (GetFocusManager()) {
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_OEM_5, ui::EF_COMMAND_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_S, ui::EF_COMMAND_DOWN),
        ui::AcceleratorManager::kNormalPriority, this);
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_D, ui::EF_COMMAND_DOWN),
        ui::AcceleratorManager::kHighPriority, this);
  }
}

void DaoSidebarView::RemovedFromWidget() {
  if (GetFocusManager()) {
    GetFocusManager()->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_OEM_5, ui::EF_COMMAND_DOWN), this);
    GetFocusManager()->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_S, ui::EF_COMMAND_DOWN), this);
    GetFocusManager()->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_D, ui::EF_COMMAND_DOWN), this);
  }
  View::RemovedFromWidget();
}

bool DaoSidebarView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_D) {
    if (chrome::CanDuplicateTab(browser_)) {
      chrome::DuplicateTab(browser_);
    }
    return true;
  }
  // Cmd+\ or Cmd+S: toggle sidebar
  ToggleCollapsed();
  return true;
}

// --- Edge hover auto-expand ----------------------------------------------

void DaoSidebarView::OnMouseEntered(const ui::MouseEvent& event) {
  if (collapsed_ && !collapse_animation_.is_animating()) {
    auto_expanded_ = true;
    collapsed_ = false;
    start_width_ = current_width_;
    target_width_ = user_width_;
    collapse_animation_.Stop();
    collapse_animation_.Start();
  }
}

void DaoSidebarView::OnMouseExited(const ui::MouseEvent& event) {
  if (auto_expanded_) {
    auto_expanded_ = false;
    collapsed_ = true;
    start_width_ = current_width_;
    target_width_ = kCollapsedWidth;
    collapse_animation_.Stop();
    collapse_animation_.Start();
  }
}

// --- New tab button highlight -----------------------------------------------

void DaoSidebarView::SetNewTabHighlighted(bool highlighted) {
  if (tab_list_view_) {
    tab_list_view_->SetNewTabHighlighted(highlighted);
  }
}

// --- Command bar delegation -------------------------------------------------

void DaoSidebarView::ShowOmniboxPopup() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->Show();
  }
}

void DaoSidebarView::HideOmniboxPopup() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->Hide();
  }
}

// --- File / URL drop target -----------------------------------------------

bool DaoSidebarView::GetDropFormats(
    int* formats,
    std::set<ui::ClipboardFormatType>* format_types) {
  *formats = ui::OSExchangeData::URL | ui::OSExchangeData::FILE_NAME;
  return true;
}

bool DaoSidebarView::CanDrop(const ui::OSExchangeData& data) {
  // Reject internal tab drags — those carry a "dao-tab-drag" string marker.
  std::optional<std::u16string> text = data.GetString();
  if (text.has_value() && *text == u"dao-tab-drag") {
    return false;
  }
  return data.HasURL(ui::FilenameToURLPolicy::CONVERT_FILENAMES) ||
         data.HasFile();
}

void DaoSidebarView::OnDragEntered(const ui::DropTargetEvent& event) {
  is_drop_target_active_ = true;
  drop_target_index_ = -1;
  auto* overlay = static_cast<DaoDropOverlayView*>(drop_overlay_.get());
  overlay->SetActive(true);
  overlay->SetIndicatorY(-1);
  // Auto-expand sidebar if collapsed so the user sees the drop zone.
  if (collapsed_ && !collapse_animation_.is_animating()) {
    drop_auto_expanded_ = true;
    collapsed_ = false;
    start_width_ = current_width_;
    target_width_ = user_width_;
    collapse_animation_.Stop();
    collapse_animation_.Start();
  }
}

int DaoSidebarView::OnDragUpdated(const ui::DropTargetEvent& event) {
  auto* overlay = static_cast<DaoDropOverlayView*>(drop_overlay_.get());
  // Compute drop indicator position by checking tab items.
  const auto& items = tab_list_view_->tab_items();
  int indicator_y = -1;
  if (items.empty()) {
    drop_target_index_ = -1;
  } else {
    // Convert event y from sidebar coords to tab_list_view coords.
    gfx::Point pt_in_tab_list(event.x(), event.y());
    views::View::ConvertPointToTarget(this, tab_list_view_, &pt_in_tab_list);

    TabStripModel* model = browser_->tab_strip_model();
    bool found = false;
    for (const auto& tab_item : items) {
      gfx::Point mid(0, tab_item->height() / 2);
      views::View::ConvertPointToTarget(tab_item.get(), tab_list_view_, &mid);
      if (pt_in_tab_list.y() < mid.y()) {
        int idx = tab_item->model_index();
        // Today section is displayed in reverse model order:
        // visually "above" a tab means "after" it in the model.
        if (!model->IsTabPinned(idx)) {
          drop_target_index_ = idx + 1;
        } else {
          drop_target_index_ = idx;
        }
        gfx::Point top(0, 0);
        views::View::ConvertPointToTarget(tab_item.get(), this, &top);
        indicator_y = top.y();
        found = true;
        break;
      }
    }
    if (!found) {
      auto* last = items.back().get();
      int idx = last->model_index();
      // Bottom of reversed today section = lowest model index.
      if (!model->IsTabPinned(idx)) {
        drop_target_index_ = idx;
      } else {
        drop_target_index_ = idx + 1;
      }
      gfx::Point bottom(0, last->height());
      views::View::ConvertPointToTarget(last, this, &bottom);
      indicator_y = bottom.y();
    }
  }
  overlay->SetIndicatorY(indicator_y);
  return ui::DragDropTypes::DRAG_COPY;
}

void DaoSidebarView::OnDragExited() {
  is_drop_target_active_ = false;
  drop_target_index_ = -1;
  auto* overlay = static_cast<DaoDropOverlayView*>(drop_overlay_.get());
  overlay->SetActive(false);
  overlay->SetIndicatorY(-1);
  // Collapse back if we auto-expanded for the drag.
  if (drop_auto_expanded_) {
    drop_auto_expanded_ = false;
    collapsed_ = true;
    start_width_ = current_width_;
    target_width_ = kCollapsedWidth;
    collapse_animation_.Stop();
    collapse_animation_.Start();
  }
}

views::View::DropCallback DaoSidebarView::GetDropCallback(
    const ui::DropTargetEvent& event) {
  // Successful drop — clear highlight and keep sidebar open.
  is_drop_target_active_ = false;
  drop_auto_expanded_ = false;
  int insert_index = drop_target_index_;
  drop_target_index_ = -1;
  auto* overlay = static_cast<DaoDropOverlayView*>(drop_overlay_.get());
  overlay->SetActive(false);
  overlay->SetIndicatorY(-1);

  Browser* browser = browser_;
  return base::BindOnce(
      [](Browser* browser, int insert_index,
         const ui::DropTargetEvent& event,
         ui::mojom::DragOperation& output_drag_op,
         std::unique_ptr<ui::LayerTreeOwner> drag_image_layer_owner) {
        const ui::OSExchangeData& data = event.data();
        std::vector<GURL> urls;

        // Try GetURLs which handles both URLs and file conversions.
        auto maybe_urls =
            data.GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES);
        if (maybe_urls.has_value()) {
          for (const auto& u : *maybe_urls) {
            if (u.is_valid()) {
              urls.push_back(u);
            }
          }
        }

        // Also pick up files via GetFilenames for multi-file drops.
        auto maybe_files = data.GetFilenames();
        if (maybe_files.has_value()) {
          for (const auto& file : *maybe_files) {
            GURL file_url = net::FilePathToFileURL(file.path);
            if (file_url.is_valid() &&
                std::find(urls.begin(), urls.end(), file_url) == urls.end()) {
              urls.push_back(file_url);
            }
          }
        }

        for (size_t i = 0; i < urls.size(); ++i) {
          NavigateParams params(browser, urls[i], ui::PAGE_TRANSITION_TYPED);
          params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
          if (insert_index >= 0) {
            params.tabstrip_index = insert_index + static_cast<int>(i);
          }
          Navigate(&params);
        }

        output_drag_op = urls.empty() ? ui::mojom::DragOperation::kNone
                                      : ui::mojom::DragOperation::kCopy;
      },
      browser, insert_index);
}

void DaoSidebarView::OnPaint(gfx::Canvas* canvas) {
  views::View::OnPaint(canvas);
}

}  // namespace dao
