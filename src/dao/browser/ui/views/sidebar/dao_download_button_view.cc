// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_download_button_view.h"

#include <algorithm>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/download/download_prefs.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/download/public/common/download_item.h"
#include "ui/display/screen.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/download_manager.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/sidebar/dao_download_flyout_view.h"
#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

constexpr base::TimeDelta kExpandDuration = base::Milliseconds(150);

// Custom targeter delegate for DaoDownloadButtonView: when the view is
// collapsed (not expanded), restrict the hittable area to the icon button
// so that only hovering/clicking on the button triggers interaction.
class DownloadButtonTargeterDelegate : public views::ViewTargeterDelegate {
 public:
  DownloadButtonTargeterDelegate(DaoDownloadButtonView* owner,
                                  views::View* button_row)
      : owner_(owner), button_row_(button_row) {}

  bool DoesIntersectRect(const views::View* target,
                         const gfx::Rect& rect) const override {
    if (owner_->is_expanded_for_hit_test()) {
      return target->GetLocalBounds().Intersects(rect);
    }
    if (!button_row_ || button_row_->size().IsEmpty()) {
      return target->GetLocalBounds().Intersects(rect);
    }
    gfx::Point origin;
    views::View::ConvertPointToTarget(button_row_, target, &origin);
    gfx::Rect btn(origin, button_row_->size());
    return btn.Intersects(rect);
  }

 private:
  raw_ptr<DaoDownloadButtonView> owner_;
  raw_ptr<views::View> button_row_;
};
constexpr SkColor kProgressBarBg = SkColorSetARGB(40, 255, 255, 255);
constexpr SkColor kProgressBarFill = SkColorSetRGB(100, 180, 255);
// Runs on a background thread. Scans the download directory and returns
// the most recent files sorted by modification time, with their system icons.
std::vector<FileIconEntry> ScanDownloadDirectory(
    base::FilePath download_dir,
    int max_items,
    int icon_size) {
  struct FileEntry {
    base::FilePath path;
    base::Time modified_time;
  };
  std::vector<FileEntry> entries;

  base::FileEnumerator enumerator(download_dir, /*recursive=*/false,
                                  base::FileEnumerator::FILES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    // Skip hidden files (starting with '.')
    if (path.BaseName().value()[0] == '.') {
      continue;
    }
    base::FileEnumerator::FileInfo info = enumerator.GetInfo();
    entries.push_back({path, info.GetLastModifiedTime()});
  }

  // Sort newest first so we pick the N most recent files.
  std::sort(entries.begin(), entries.end(),
            [](const FileEntry& a, const FileEntry& b) {
              return a.modified_time > b.modified_time;
            });

  std::vector<FileIconEntry> result;
  int count = std::min(static_cast<int>(entries.size()), max_items);
  for (int i = 0; i < count; ++i) {
    FileIconEntry entry;
    entry.path = entries[i].path;
    entry.icon = GetFileIcon(entries[i].path, icon_size);
    result.push_back(std::move(entry));
  }
  // Reverse so oldest is at top, newest at bottom (closest to active
  // downloads and the download button).
  std::reverse(result.begin(), result.end());
  return result;
}

}  // namespace

BEGIN_METADATA(DaoDownloadButtonView)
END_METADATA

DaoDownloadButtonView::DaoDownloadButtonView(Browser* browser)
    : browser_(browser),
      expand_animation_(kExpandDuration, 60, this) {
  auto* box_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(4, 6, 6, 6)));
  box_layout->set_between_child_spacing(4);
  // kStretch so file list / active download containers use full width,
  // but button_row_ is constrained to 32x32 via SetMaximumSize below.
  box_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  SetNotifyEnterExitOnChild(true);

  // File list container (completed downloads, oldest at top, newest at bottom).
  file_list_container_ = AddChildView(std::make_unique<views::View>());
  file_list_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  file_list_container_->SetVisible(false);

  // Active download container (shown below completed files, closest to the
  // download button at the bottom).
  active_download_container_ = AddChildView(std::make_unique<views::View>());
  active_download_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  active_download_container_->SetVisible(false);

  // Pre-create active download item rows.
  for (int i = 0; i < kMaxFileItems; ++i) {
    ActiveDownloadViews av;

    auto row = std::make_unique<views::View>();
    auto* row_layout =
        row->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::TLBR(0, 10, 0, 6), 6));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    row->SetPreferredSize(gfx::Size(0, kActiveItemHeight));

    // Download icon.
    auto icon = std::make_unique<views::ImageView>();
    icon->SetPreferredSize(gfx::Size(kFileIconSize, kFileIconSize));
    icon->SetImage(gfx::CreateVectorIcon(vector_icons::kFileDownloadIcon,
                                          kFileIconSize, kProgressBarFill));
    av.icon = row->AddChildView(std::move(icon));

    // Middle section: name + progress bar stacked vertically.
    auto mid = std::make_unique<views::View>();
    auto* mid_layout =
        mid->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    mid_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto name = std::make_unique<views::Label>();
    name->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 11,
                                     gfx::Font::Weight::NORMAL));
    name->SetEnabledColor(kTextSecondary);
    name->SetSubpixelRenderingEnabled(false);
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name->SetElideBehavior(gfx::ELIDE_MIDDLE);
    av.name_label = mid->AddChildView(std::move(name));

    // Progress bar: a container with a fill child.
    auto bar = std::make_unique<views::View>();
    bar->SetPreferredSize(gfx::Size(0, kProgressBarHeight));
    bar->SetBackground(views::CreateSolidBackground(kProgressBarBg));
    auto fill = std::make_unique<views::View>();
    fill->SetBackground(views::CreateSolidBackground(kProgressBarFill));
    av.progress_fill = bar->AddChildView(std::move(fill));
    av.progress_bar = mid->AddChildView(std::move(bar));

    auto* mid_ptr = row->AddChildView(std::move(mid));
    row_layout->SetFlexForView(mid_ptr, 1);

    // Speed label.
    auto speed = std::make_unique<views::Label>();
    speed->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 10,
                                      gfx::Font::Weight::NORMAL));
    speed->SetEnabledColor(kTextMuted);
    speed->SetSubpixelRenderingEnabled(false);
    av.speed_label = row->AddChildView(std::move(speed));

    // Cancel button (× icon).
    auto cancel = std::make_unique<views::ImageView>();
    cancel->SetPreferredSize(gfx::Size(16, 16));
    cancel->SetImage(gfx::CreateVectorIcon(vector_icons::kCloseIcon, 12,
                                            kTextMuted));
    cancel->SetTooltipText(u"Cancel");
    av.cancel_button = row->AddChildView(std::move(cancel));

    row->SetVisible(false);
    av.row = active_download_container_->AddChildView(std::move(row));
    active_items_.push_back(av);
  }

  // Pre-create file item rows
  for (int i = 0; i < kMaxFileItems; ++i) {
    auto row = std::make_unique<views::View>();
    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::TLBR(0, 10, 0, 10), 8));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    row->SetPreferredSize(gfx::Size(0, kFileItemHeight));

    auto icon = std::make_unique<views::ImageView>();
    icon->SetPreferredSize(gfx::Size(kFileIconSize, kFileIconSize));
    icon->SetImage(gfx::CreateVectorIcon(
        vector_icons::kDescriptionIcon, kFileIconSize, kTextMuted));
    auto* icon_ptr = row->AddChildView(std::move(icon));

    auto name = std::make_unique<views::Label>();
    name->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 11,
                                     gfx::Font::Weight::NORMAL));
    name->SetEnabledColor(kTextSecondary);
    name->SetSubpixelRenderingEnabled(false);
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    name->SetElideBehavior(gfx::ELIDE_TAIL);
    auto* name_ptr = row->AddChildView(std::move(name));
    row_layout->SetFlexForView(name_ptr, 1);

    row->SetVisible(false);

    FileItemViews item;
    item.row = file_list_container_->AddChildView(std::move(row));
    item.icon = icon_ptr;
    item.name_label = name_ptr;
    file_items_.push_back(item);
  }

  // Wrapper keeps button_row_ left-aligned at 32x32 while the wrapper
  // itself stretches to full width (needed for correct mouse hit testing).
  auto* button_wrapper = AddChildView(std::make_unique<views::View>());
  auto* wrapper_layout =
      button_wrapper->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  wrapper_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Icon button (always visible, 32x32 rounded rect)
  button_row_ = button_wrapper->AddChildView(std::make_unique<views::View>());
  auto* btn_layout =
      button_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets(), 0));
  btn_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  btn_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  button_row_->SetPreferredSize(gfx::Size(kIconButtonSize, kIconButtonSize));
  button_row_->SetBackground(views::CreateRoundedRectBackground(
      SK_ColorTRANSPARENT, kIconButtonRadius));

  auto download_icon = std::make_unique<views::ImageView>();
  download_icon->SetPreferredSize(gfx::Size(16, 16));
  download_icon->SetImage(gfx::CreateVectorIcon(
      vector_icons::kFileDownloadIcon, 16, kTextSecondary));
  button_row_->AddChildView(std::move(download_icon));

  // Install custom event targeter: when collapsed, only the 32x32 icon
  // area is hittable; when expanded, the full view accepts mouse events.
  auto delegate = std::make_unique<DownloadButtonTargeterDelegate>(
      this, button_row_.get());
  SetEventTargeter(
      std::make_unique<views::ViewTargeter>(std::move(delegate)));
}

DaoDownloadButtonView::~DaoDownloadButtonView() {
  notifier_.reset();
}

void DaoDownloadButtonView::AddedToWidget() {
  // Initialize the download notifier once the widget (and thus the Profile)
  // is available.
  if (!notifier_ && browser_) {
    auto* profile = browser_->profile();
    if (profile) {
      auto* download_manager = profile->GetDownloadManager();
      if (download_manager) {
        notifier_ = std::make_unique<download::AllDownloadItemNotifier>(
            download_manager, this);
      }
    }
  }
}

void DaoDownloadButtonView::OnMouseEntered(const ui::MouseEvent& event) {
  is_hovered_ = true;
  UpdateButtonBackground();
  SetExpanded(true);
}

void DaoDownloadButtonView::OnMouseExited(const ui::MouseEvent& event) {
  is_hovered_ = false;
  UpdateButtonBackground();
  // Don't collapse while a drag is in progress — the source view must stay
  // alive until the system drag session ends (OnDragDone).
  if (!is_dragging_) {
    SetExpanded(false);
  }
}

bool DaoDownloadButtonView::OnMousePressed(const ui::MouseEvent& event) {
  gfx::Point point = event.location();

  // Check if press is on a cancel button in active downloads.
  int cancel_idx = HitTestCancelButton(point);
  if (cancel_idx >= 0 && active_items_[cancel_idx].item) {
    OnCancelClicked(active_items_[cancel_idx].item);
    return true;
  }

  // Check if press is on button row
  if (button_row_->bounds().Contains(point)) {
    drag_file_index_ = -1;
    return true;  // Consume; release will handle click.
  }

  // Check if press is on a file item — start tracking for drag or click.
  int file_index = HitTestFileItem(point);
  if (file_index >= 0) {
    drag_file_index_ = file_index;
    drag_press_pt_ = point;
    return true;  // Consume; drag or release will follow.
  }

  return false;
}

bool DaoDownloadButtonView::OnMouseDragged(const ui::MouseEvent& event) {
  if (drag_file_index_ < 0 || is_dragging_) {
    return false;
  }

  gfx::Vector2d delta = event.location() - drag_press_pt_;
  if (delta.Length() <= kDragThreshold) {
    return true;  // Not yet past threshold — keep tracking.
  }

  // Past threshold — initiate a system file drag.
  is_dragging_ = true;

  auto data = std::make_unique<ui::OSExchangeData>();
  data->SetFilename(recent_file_paths_[drag_file_index_]);

  // macOS requires a non-zero drag image. Use the file's system icon.
  gfx::ImageSkia drag_image =
      file_items_[drag_file_index_].icon->GetImage();
  if (!drag_image.isNull()) {
    data->provider().SetDragImage(drag_image,
                                  gfx::Vector2d(drag_image.width() / 2,
                                                drag_image.height() / 2));
  } else {
    // Fallback: use a generic icon so the drag doesn't crash.
    gfx::ImageSkia fallback = gfx::CreateVectorIcon(
        vector_icons::kFileDownloadIcon, 32, kTextSecondary);
    data->provider().SetDragImage(fallback, gfx::Vector2d(16, 16));
  }

  // RunShellDrag is asynchronous on macOS. OnDragDone() is called when
  // the drag session finishes.
  GetWidget()->RunShellDrag(this, std::move(data), drag_press_pt_,
                            ui::DragDropTypes::DRAG_COPY,
                            ui::mojom::DragEventSource::kMouse);
  return true;
}

void DaoDownloadButtonView::OnMouseReleased(const ui::MouseEvent& event) {
  if (is_dragging_) {
    return;
  }

  gfx::Point point = event.location();

  // Click on button row → open downloads folder.
  if (button_row_->bounds().Contains(point)) {
    OnButtonClicked();
    drag_file_index_ = -1;
    return;
  }

  // Click on a file item (no drag occurred) → open the file.
  if (drag_file_index_ >= 0) {
    OnFileItemClicked(drag_file_index_);
    drag_file_index_ = -1;
  }
}

void DaoDownloadButtonView::OnDragDone() {
  is_dragging_ = false;
  drag_file_index_ = -1;
  // Now that the drag ended, collapse if the mouse has left.
  if (!is_hovered_) {
    SetExpanded(false);
  }
}

void DaoDownloadButtonView::OnPaint(gfx::Canvas* canvas) {
  View::OnPaint(canvas);

  bool file_list_visible = file_list_container_->GetVisible();
  bool active_visible = active_download_container_->GetVisible();
  if (!file_list_visible && !active_visible) {
    return;
  }

  // Draw gradient background over the visible portion of the list area.
  gfx::Rect combined_area;
  if (active_visible) {
    combined_area = active_download_container_->bounds();
  }
  if (file_list_visible) {
    if (combined_area.IsEmpty()) {
      combined_area = file_list_container_->bounds();
    } else {
      combined_area.Union(file_list_container_->bounds());
    }
  }
  if (combined_area.IsEmpty()) {
    return;
  }

  // Clip to animated height for file list.
  int visible_bottom = combined_area.bottom();
  int visible_top = visible_bottom - current_file_list_height_;
  if (active_visible) {
    visible_top = std::min(visible_top,
                           active_download_container_->bounds().y());
  }
  gfx::Rect visible_area(combined_area.x(), visible_top,
                          combined_area.width(),
                          visible_bottom - visible_top);
  if (visible_area.IsEmpty()) {
    return;
  }

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setShader(gfx::CreateGradientShader(
      gfx::Point(0, visible_area.y()),
      gfx::Point(0, visible_area.bottom()),
      SK_ColorTRANSPARENT,
      kSidebarBackground));
  canvas->DrawRoundRect(gfx::RectF(visible_area), kIconButtonRadius, flags);
}

void DaoDownloadButtonView::UpdateButtonBackground() {
  // Default: semi-transparent white (alpha 35). Hover: brighter (alpha 60).
  constexpr SkColor kHoverBg = SkColorSetARGB(60, 255, 255, 255);
  if (is_hovered_) {
    button_row_->SetBackground(
        views::CreateRoundedRectBackground(kHoverBg, kIconButtonRadius));
  } else {
    button_row_->SetBackground(
        views::CreateRoundedRectBackground(SK_ColorTRANSPARENT, kIconButtonRadius));
  }
  button_row_->SchedulePaint();
}

void DaoDownloadButtonView::RefreshFileList() {
  DownloadPrefs* prefs = DownloadPrefs::FromBrowserContext(browser_->profile());
  base::FilePath download_dir = prefs->DownloadPath();

  // Post file I/O to a background thread, then update UI on the main thread.
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ScanDownloadDirectory, download_dir, kMaxFileItems,
                     kFileIconSize),
      base::BindOnce(&DaoDownloadButtonView::OnFileListReady,
                     weak_factory_.GetWeakPtr()));
}

void DaoDownloadButtonView::OnFileListReady(
    std::vector<FileIconEntry> entries) {
  recent_file_paths_.clear();

  for (int i = 0; i < kMaxFileItems; ++i) {
    if (i < static_cast<int>(entries.size())) {
      recent_file_paths_.push_back(entries[i].path);
      file_items_[i].name_label->SetText(
          entries[i].path.BaseName().LossyDisplayName());
      if (!entries[i].icon.isNull()) {
        file_items_[i].icon->SetImage(entries[i].icon);
      }
      file_items_[i].row->SetVisible(true);
    } else {
      file_items_[i].row->SetVisible(false);
    }
  }

  if (recent_file_paths_.empty() && GetActiveItemCount() == 0) {
    if (!is_expanded_) {
      file_list_container_->SetVisible(false);
      PreferredSizeChanged();
      SchedulePaint();
    }
    return;
  }

  if (!is_expanded_) {
    file_list_container_->SetVisible(false);
    PreferredSizeChanged();
    SchedulePaint();
    return;
  }

  // Start at zero height so the first layout pass doesn't flash full size.
  current_file_list_height_ = 0;
  file_list_container_->SetPreferredSize(gfx::Size(0, 0));
  file_list_container_->SetVisible(true);
  StartExpandAnimation();
}

void DaoDownloadButtonView::OnButtonClicked() {
  DownloadPrefs* prefs = DownloadPrefs::FromBrowserContext(browser_->profile());
  base::FilePath download_path = prefs->DownloadPath();
  platform_util::OpenItem(browser_->profile(), download_path,
                          platform_util::OPEN_FOLDER,
                          platform_util::OpenOperationCallback());
}

void DaoDownloadButtonView::OnFileItemClicked(int index) {
  if (index >= 0 && index < static_cast<int>(recent_file_paths_.size())) {
    platform_util::OpenItem(browser_->profile(), recent_file_paths_[index],
                            platform_util::OPEN_FILE,
                            platform_util::OpenOperationCallback());
  }
}

void DaoDownloadButtonView::OnCancelClicked(download::DownloadItem* item) {
  if (item) {
    item->Cancel(/*user_cancel=*/true);
  }
}

void DaoDownloadButtonView::SetExpanded(bool expanded) {
  if (is_expanded_ == expanded) {
    return;
  }
  is_expanded_ = expanded;

  if (expanded) {
    // Show active downloads immediately if any.
    UpdateActiveDownloadUI();
    RefreshFileList();
    // File list will be shown asynchronously in OnFileListReady
  } else {
    // Start collapse animation if there's something to collapse.
    if (file_list_container_->GetVisible() && current_file_list_height_ > 0) {
      StartCollapseAnimation();
    } else {
      file_list_container_->SetVisible(false);
      current_file_list_height_ = 0;
      PreferredSizeChanged();
      SchedulePaint();
    }
    // Keep active downloads visible while items are in progress.
    bool has_active = GetActiveItemCount() > 0;
    active_download_container_->SetVisible(has_active);
    PreferredSizeChanged();
  }
}

int DaoDownloadButtonView::HitTestFileItem(const gfx::Point& point) const {
  if (!is_expanded_ || !file_list_container_->GetVisible()) {
    return -1;
  }
  gfx::Point file_point = point;
  ConvertPointToTarget(this, file_list_container_, &file_point);
  for (int i = 0; i < static_cast<int>(file_items_.size()); ++i) {
    if (file_items_[i].row->GetVisible() &&
        file_items_[i].row->bounds().Contains(file_point)) {
      return i;
    }
  }
  return -1;
}

int DaoDownloadButtonView::HitTestActiveItem(const gfx::Point& point) const {
  if (!is_expanded_ || !active_download_container_->GetVisible()) {
    return -1;
  }
  gfx::Point ap = point;
  ConvertPointToTarget(this, active_download_container_, &ap);
  for (int i = 0; i < static_cast<int>(active_items_.size()); ++i) {
    if (active_items_[i].row->GetVisible() &&
        active_items_[i].row->bounds().Contains(ap)) {
      return i;
    }
  }
  return -1;
}

int DaoDownloadButtonView::HitTestCancelButton(
    const gfx::Point& point) const {
  int ai = HitTestActiveItem(point);
  if (ai < 0) {
    return -1;
  }
  // Convert point to the active item row's coordinates and check the cancel
  // button bounds.
  gfx::Point rp = point;
  ConvertPointToTarget(this, active_items_[ai].row.get(), &rp);
  if (active_items_[ai].cancel_button &&
      active_items_[ai].cancel_button->bounds().Contains(rp)) {
    return ai;
  }
  return -1;
}

int DaoDownloadButtonView::GetTargetFileListHeight() const {
  int file_count = static_cast<int>(recent_file_paths_.size());
  int file_height = std::min(file_count, kMaxFileItems) * kFileItemHeight;
  int active_height = GetActiveItemCount() * kActiveItemHeight;
  return file_height + active_height;
}

void DaoDownloadButtonView::StartExpandAnimation() {
  is_animating_expand_ = true;
  anim_start_height_ = current_file_list_height_;
  anim_target_height_ = GetTargetFileListHeight();
  expand_animation_.Stop();
  expand_animation_.Start();
}

void DaoDownloadButtonView::StartCollapseAnimation() {
  is_animating_expand_ = false;
  anim_start_height_ = current_file_list_height_;
  anim_target_height_ = 0;
  expand_animation_.Stop();
  expand_animation_.Start();
}

void DaoDownloadButtonView::AnimationProgressed(
    const gfx::Animation* animation) {
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_OUT,
                                         animation->GetCurrentValue());
  current_file_list_height_ =
      anim_start_height_ +
      static_cast<int>((anim_target_height_ - anim_start_height_) * t);

  // Update the container's preferred size to the animated height.
  int active_height = GetActiveItemCount() * kActiveItemHeight;
  int file_anim_height = std::max(0, current_file_list_height_ - active_height);
  file_list_container_->SetPreferredSize(gfx::Size(0, file_anim_height));
  PreferredSizeChanged();
  SchedulePaint();
}

void DaoDownloadButtonView::AnimationEnded(const gfx::Animation* animation) {
  current_file_list_height_ = anim_target_height_;

  int active_height = GetActiveItemCount() * kActiveItemHeight;
  int file_target_height = std::max(0, anim_target_height_ - active_height);
  file_list_container_->SetPreferredSize(gfx::Size(0, file_target_height));

  if (!is_animating_expand_) {
    // Collapse finished — hide the file list, but keep active downloads
    // visible while items are still in progress.
    file_list_container_->SetVisible(false);
    active_download_container_->SetVisible(GetActiveItemCount() > 0);
  }

  PreferredSizeChanged();
  SchedulePaint();
}

// ---- Download observer callbacks ----

void DaoDownloadButtonView::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!item || item->GetState() != download::DownloadItem::IN_PROGRESS) {
    return;
  }

  // Add to the first available active slot.
  for (auto& av : active_items_) {
    if (!av.item) {
      av.item = item;
      av.name_label->SetText(
          base::UTF8ToUTF16(item->GetFileNameToReportUser().BaseName().value()));
      av.speed_label->SetText(u"0 B/s");
      av.progress_fill->SetBounds(0, 0, 0, kProgressBarHeight);
      av.row->SetVisible(true);
      break;
    }
  }

  // Trigger the flyout arc animation.
  TriggerFlyoutAnimation();

  // Auto-expand to show the active download.
  UpdateActiveDownloadUI();
  if (!is_expanded_) {
    // Show active container briefly even without hover.
    active_download_container_->SetVisible(GetActiveItemCount() > 0);
    PreferredSizeChanged();
    SchedulePaint();
  }
}

void DaoDownloadButtonView::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  if (!item) {
    return;
  }

  auto state = item->GetState();

  // If completed or cancelled, move out of active list.
  if (state == download::DownloadItem::COMPLETE ||
      state == download::DownloadItem::CANCELLED ||
      state == download::DownloadItem::INTERRUPTED) {
    RemoveActiveDownload(item);
    // Refresh the completed file list if the download finished successfully.
    if (state == download::DownloadItem::COMPLETE && is_expanded_) {
      RefreshFileList();
    }
    return;
  }

  // Update the active item's progress and speed.
  for (auto& av : active_items_) {
    if (av.item == item) {
      int percent = item->PercentComplete();
      if (percent >= 0 && av.progress_bar) {
        int bar_width = av.progress_bar->width();
        int fill_width = bar_width * percent / 100;
        av.progress_fill->SetBounds(0, 0, fill_width, kProgressBarHeight);
      }
      av.speed_label->SetText(FormatSpeed(item->CurrentSpeed()));
      av.name_label->SetText(base::UTF8ToUTF16(
          item->GetFileNameToReportUser().BaseName().value()));
      break;
    }
  }
}

void DaoDownloadButtonView::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  RemoveActiveDownload(item);
}

void DaoDownloadButtonView::TriggerFlyoutAnimation() {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  auto* flyout = browser_view->dao_download_flyout();
  if (!flyout || flyout->is_animating()) {
    return;
  }

  // Ensure flyout covers the entire BrowserView (may not have been laid out).
  flyout->SetBounds(0, 0, browser_view->width(), browser_view->height());

  // Start point: use current cursor position (close to where the user clicked
  // the download link).  If the cursor is outside the content area, fall back
  // to the content center.
  auto* container = browser_view->contents_container();
  gfx::Point cursor_screen =
      display::Screen::GetScreen()->GetCursorScreenPoint();
  gfx::Point start = cursor_screen;
  views::View::ConvertPointFromScreen(flyout, &start);

  // Verify the start point is within the content area; otherwise use center.
  gfx::Rect content_in_flyout = container->GetLocalBounds();
  gfx::Point content_origin;
  views::View::ConvertPointToTarget(container, flyout, &content_origin);
  content_in_flyout.set_origin(content_origin);
  if (!content_in_flyout.Contains(start)) {
    start = content_in_flyout.CenterPoint();
  }

  // End point: download button center, in flyout-local coordinates.
  gfx::Point end = button_row_->GetLocalBounds().CenterPoint();
  views::View::ConvertPointToTarget(button_row_, flyout, &end);

  flyout->StartAnimation(start, end, base::BindOnce(
      &DaoDownloadButtonView::UpdateActiveDownloadUI,
      weak_factory_.GetWeakPtr()));
}

void DaoDownloadButtonView::UpdateActiveDownloadUI() {
  int count = GetActiveItemCount();
  active_download_container_->SetVisible(count > 0 && is_expanded_);
  PreferredSizeChanged();
  SchedulePaint();
}

void DaoDownloadButtonView::RemoveActiveDownload(
    download::DownloadItem* item) {
  for (auto& av : active_items_) {
    if (av.item == item) {
      av.item = nullptr;
      av.row->SetVisible(false);
      break;
    }
  }
  UpdateActiveDownloadUI();
}

// static
std::u16string DaoDownloadButtonView::FormatSpeed(int64_t bytes_per_sec) {
  if (bytes_per_sec <= 0) {
    return u"0 B/s";
  }
  if (bytes_per_sec < 1024) {
    return base::UTF8ToUTF16(std::to_string(bytes_per_sec) + " B/s");
  }
  char buf[32];
  if (bytes_per_sec < 1024 * 1024) {
    double kb = bytes_per_sec / 1024.0;
    snprintf(buf, sizeof(buf), "%.1f KB/s", kb);
  } else {
    double mb = bytes_per_sec / (1024.0 * 1024.0);
    snprintf(buf, sizeof(buf), "%.1f MB/s", mb);
  }
  return base::UTF8ToUTF16(std::string(buf));
}

int DaoDownloadButtonView::GetActiveItemCount() const {
  int count = 0;
  for (const auto& av : active_items_) {
    if (av.item) {
      ++count;
    }
  }
  return count;
}

}  // namespace dao
