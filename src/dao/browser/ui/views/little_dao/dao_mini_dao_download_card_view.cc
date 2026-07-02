// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_mini_dao_download_card_view.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/download/public/common/download_item.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "url/gurl.h"

namespace dao {

namespace {

constexpr int kCardWidth = 268;
constexpr int kCardCornerRadius = 12;
constexpr int kCardPadding = 10;
constexpr int kMaxVisibleRows = 2;
constexpr int kRowHeight = 42;
constexpr int kProgressHeight = 4;
constexpr int kCancelButtonSize = 22;
constexpr int kCancelIconSize = 14;

SkColor GlassFill() {
  return IsDarkMode() ? SkColorSetARGB(166, 58, 64, 70)
                      : SkColorSetARGB(154, 255, 255, 255);
}

SkColor GlassBorder() {
  return IsDarkMode() ? SkColorSetARGB(56, 255, 255, 255)
                      : SkColorSetARGB(120, 255, 255, 255);
}

void PrepareLabel(views::Label* label, SkColor color) {
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetEnabledColor(color);
  label->SetSkipSubpixelRenderingOpacityCheck(true);
}

class MiniDaoDownloadProgressView : public views::View {
  METADATA_HEADER(MiniDaoDownloadProgressView, views::View)

 public:
  explicit MiniDaoDownloadProgressView(int percent)
      : percent_(std::clamp(percent, 0, 100)) {
    SetPreferredSize(gfx::Size(0, kProgressHeight));
  }

  void SetPercent(int percent) {
    const int clamped_percent = std::clamp(percent, 0, 100);
    if (percent_ == clamped_percent) {
      return;
    }
    percent_ = clamped_percent;
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::RectF track(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(SeparatorColor());
    canvas->DrawRoundRect(track, kProgressHeight / 2.0f, flags);

    const float fill_width = track.width() * percent_ / 100.0f;
    if (fill_width <= 0.0f) {
      return;
    }
    flags.setColor(SpaceActive());
    canvas->DrawRoundRect(
        gfx::RectF(track.x(), track.y(), fill_width, track.height()),
        kProgressHeight / 2.0f, flags);
  }

 private:
  int percent_;
};

BEGIN_METADATA(MiniDaoDownloadProgressView)
END_METADATA

class MiniDaoDownloadGlassBackground : public views::Background {
 public:
  explicit MiniDaoDownloadGlassBackground(int radius) : radius_(radius) {
    SetColor(SK_ColorWHITE);
  }

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    gfx::RectF bounds(view->GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(GlassFill());
    canvas->DrawRoundRect(bounds, radius_, flags);

    bounds.Inset(0.5f);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1.0f);
    flags.setColor(GlassBorder());
    canvas->DrawRoundRect(bounds, radius_ - 0.5f, flags);
  }

 private:
  int radius_;
};

class MiniDaoDownloadCancelButton : public views::Button {
  METADATA_HEADER(MiniDaoDownloadCancelButton, views::Button)

 public:
  explicit MiniDaoDownloadCancelButton(PressedCallback callback)
      : Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(kCancelButtonSize, kCancelButtonSize));
    SetAccessibleName(l10n_util::GetStringUTF16(
        IDS_DAO_MINI_DAO_DOWNLOAD_CARD_CANCEL));
    SetTooltipText(l10n_util::GetStringUTF16(
        IDS_DAO_MINI_DAO_DOWNLOAD_CARD_CANCEL));
    SetInstallFocusRingOnFocus(false);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(ControlCenterHoverBg(),
                                                     kCancelButtonSize / 2));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const float icon_offset = (width() - kCancelIconSize) / 2.0f;
    DrawLucideIcon(canvas, LucideIcon::kX,
                   gfx::RectF(icon_offset, icon_offset, kCancelIconSize,
                              kCancelIconSize),
                   ControlCenterIconMuted());
  }
};

BEGIN_METADATA(MiniDaoDownloadCancelButton)
END_METADATA

class MiniDaoDownloadRowView : public views::View {
  METADATA_HEADER(MiniDaoDownloadRowView, views::View)

 public:
  MiniDaoDownloadRowView(int id,
                         const std::u16string& name,
                         const std::string& speed,
                         int percent,
                         views::Button::PressedCallback cancel_callback)
      : download_id_(id) {
    SetPreferredSize(gfx::Size(0, kRowHeight));
    auto* row_layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto content = std::make_unique<views::View>();
    auto* content_layout =
        content->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), 5));
    content_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);

    auto top_line = std::make_unique<views::View>();
    auto* top_layout =
        top_line->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    top_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);

    auto name_label = std::make_unique<views::Label>(name);
    PrepareLabel(name_label.get(), TextPrimary());
    name_label->SetElideBehavior(gfx::ELIDE_TAIL);
    name_label_ = top_line->AddChildView(std::move(name_label));
    top_layout->SetFlexForView(name_label_, 1);

    auto speed_label = std::make_unique<views::Label>(base::UTF8ToUTF16(speed));
    PrepareLabel(speed_label.get(), ControlCenterSecondaryTextColor());
    speed_label_ = top_line->AddChildView(std::move(speed_label));
    content->AddChildView(std::move(top_line));

    progress_view_ = content->AddChildView(
        std::make_unique<MiniDaoDownloadProgressView>(percent));

    views::View* content_view = AddChildView(std::move(content));
    row_layout->SetFlexForView(content_view, 1);
    AddChildView(std::make_unique<MiniDaoDownloadCancelButton>(
        std::move(cancel_callback)));
  }

  int download_id() const { return download_id_; }

  void Update(const std::u16string& name,
              const std::string& speed,
              int percent) {
    if (name_label_->GetText() != name) {
      name_label_->SetText(name);
    }

    const std::u16string speed_text = base::UTF8ToUTF16(speed);
    if (speed_label_->GetText() != speed_text) {
      speed_label_->SetText(speed_text);
    }

    progress_view_->SetPercent(percent);
  }

 private:
  int download_id_;
  raw_ptr<views::Label> name_label_ = nullptr;
  raw_ptr<views::Label> speed_label_ = nullptr;
  raw_ptr<MiniDaoDownloadProgressView> progress_view_ = nullptr;
};

BEGIN_METADATA(MiniDaoDownloadRowView)
END_METADATA

}  // namespace

BEGIN_METADATA(DaoMiniDaoDownloadCardView)
END_METADATA

DaoMiniDaoDownloadCardView::DaoMiniDaoDownloadCardView(Browser* browser)
    : browser_(browser) {
  SetVisible(false);

  card_ = AddChildView(std::make_unique<views::View>());
  card_->SetPaintToLayer();
  card_->layer()->SetFillsBoundsOpaquely(false);
  card_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kCardCornerRadius));
  card_->layer()->SetIsFastRoundedCorner(true);
  card_->layer()->SetBackgroundBlur(30);
  card_->SetBackground(
      std::make_unique<MiniDaoDownloadGlassBackground>(kCardCornerRadius));

  auto* card_layout =
      card_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(kCardPadding),
          8));
  card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  rows_container_ = card_->AddChildView(std::make_unique<views::View>());
  auto* rows_layout =
      rows_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  rows_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  overflow_label_ = card_->AddChildView(std::make_unique<views::Label>());
  PrepareLabel(overflow_label_, ControlCenterSecondaryTextColor());
  overflow_label_->SetVisible(false);

  if (browser_ && browser_->profile()) {
    if (auto* manager = browser_->profile()->GetDownloadManager()) {
      download_notifier_ =
          std::make_unique<download::AllDownloadItemNotifier>(manager, this);
    }
  }
  Refresh();
}

DaoMiniDaoDownloadCardView::~DaoMiniDaoDownloadCardView() = default;

bool DaoMiniDaoDownloadCardView::HasActiveDownloadsForTesting() const {
  return !active_download_ids_.empty();
}

void DaoMiniDaoDownloadCardView::CancelDownloadForTesting(int id) {
  CancelDownload(id);
}

void DaoMiniDaoDownloadCardView::OnDownloadCreated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  Refresh();
}

void DaoMiniDaoDownloadCardView::OnDownloadUpdated(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  Refresh();
}

void DaoMiniDaoDownloadCardView::OnDownloadRemoved(
    content::DownloadManager* manager,
    download::DownloadItem* item) {
  Refresh();
}

gfx::Size DaoMiniDaoDownloadCardView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (!card_) {
    return gfx::Size(kCardWidth, 0);
  }
  return gfx::Size(kCardWidth, card_->GetPreferredSize().height());
}

void DaoMiniDaoDownloadCardView::Layout(PassKey) {
  if (!card_) {
    return;
  }
  card_->SetBounds(0, 0, width(), height());
}

std::vector<DaoMiniDaoDownloadCardView::ActiveDownload>
DaoMiniDaoDownloadCardView::BuildActiveDownloads() const {
  std::vector<ActiveDownload> downloads;
  if (!browser_ || !browser_->profile()) {
    return downloads;
  }

  auto* manager = browser_->profile()->GetDownloadManager();
  if (!manager) {
    return downloads;
  }

  content::DownloadManager::DownloadVector items;
  manager->GetAllDownloads(&items);
  for (download::DownloadItem* item : items) {
    if (!item || item->GetState() != download::DownloadItem::IN_PROGRESS ||
        !IsDownloadFromThisWindow(item)) {
      continue;
    }

    ActiveDownload row;
    row.id = static_cast<int>(item->GetId());
    row.name = item->GetFileNameToReportUser().BaseName().LossyDisplayName();
    row.percent = item->PercentComplete();
    row.speed = FormatSpeed(item->CurrentSpeed());
    downloads.push_back(std::move(row));
  }
  return downloads;
}

bool DaoMiniDaoDownloadCardView::IsDownloadFromThisWindow(
    download::DownloadItem* item) const {
  if (!browser_ || !browser_->tab_strip_model()) {
    return false;
  }

  content::WebContents* source =
      content::DownloadItemUtils::GetOriginalWebContents(item);
  if (!source) {
    source = content::DownloadItemUtils::GetWebContents(item);
  }
  if (source) {
    return browser_->tab_strip_model()->GetIndexOfWebContents(source) !=
           TabStripModel::kNoTab;
  }

  content::WebContents* active = GetActiveWebContents();
  return active && item->GetTabUrl().is_valid() &&
         item->GetTabUrl() == active->GetVisibleURL();
}

content::WebContents* DaoMiniDaoDownloadCardView::GetActiveWebContents()
    const {
  if (!browser_ || !browser_->tab_strip_model()) {
    return nullptr;
  }
  return browser_->tab_strip_model()->GetActiveWebContents();
}

void DaoMiniDaoDownloadCardView::Refresh() {
  const std::vector<ActiveDownload> downloads = BuildActiveDownloads();
  const bool rows_match = VisibleRowsMatch(downloads);
  const bool was_visible = GetVisible();
  const bool overflow_was_visible = overflow_label_->GetVisible();

  active_download_ids_.clear();
  active_download_ids_.reserve(downloads.size());
  for (const ActiveDownload& download : downloads) {
    active_download_ids_.push_back(download.id);
  }

  if (rows_match) {
    UpdateRows(downloads);
  } else {
    RebuildRows(downloads);
  }
  UpdateOverflow(downloads);

  const bool should_be_visible = !downloads.empty();
  SetVisible(should_be_visible);
  const bool layout_changed =
      !rows_match || was_visible != should_be_visible ||
      overflow_was_visible != overflow_label_->GetVisible();
  if (layout_changed && parent()) {
    parent()->InvalidateLayout();
  }
  if (layout_changed) {
    InvalidateLayout();
  }
  SchedulePaint();
}

bool DaoMiniDaoDownloadCardView::VisibleRowsMatch(
    const std::vector<ActiveDownload>& downloads) const {
  const int visible_count =
      std::min(static_cast<int>(downloads.size()), kMaxVisibleRows);
  if (visible_download_ids_.size() != static_cast<size_t>(visible_count)) {
    return false;
  }

  for (int i = 0; i < visible_count; ++i) {
    if (visible_download_ids_[i] != downloads[i].id) {
      return false;
    }
  }
  return true;
}

void DaoMiniDaoDownloadCardView::UpdateRows(
    const std::vector<ActiveDownload>& downloads) {
  const int visible_count =
      std::min(static_cast<int>(downloads.size()), kMaxVisibleRows);
  for (int i = 0; i < visible_count; ++i) {
    auto* row =
        static_cast<MiniDaoDownloadRowView*>(rows_container_->children()[i]);
    DCHECK(row);
    DCHECK_EQ(downloads[i].id, row->download_id());
    row->Update(downloads[i].name, downloads[i].speed, downloads[i].percent);
  }
}

void DaoMiniDaoDownloadCardView::RebuildRows(
    const std::vector<ActiveDownload>& downloads) {
  rows_container_->RemoveAllChildViews();
  visible_download_ids_.clear();

  const int visible_count =
      std::min(static_cast<int>(downloads.size()), kMaxVisibleRows);
  auto* rows_layout =
      static_cast<views::BoxLayout*>(rows_container_->GetLayoutManager());

  for (int i = 0; i < visible_count; ++i) {
    const ActiveDownload& download = downloads[i];
    auto row = std::make_unique<MiniDaoDownloadRowView>(
        download.id, download.name, download.speed, download.percent,
        base::BindRepeating(&DaoMiniDaoDownloadCardView::CancelDownload,
                            base::Unretained(this), download.id));

    views::View* row_view = rows_container_->AddChildView(std::move(row));
    rows_layout->SetFlexForView(row_view, 1);
    visible_download_ids_.push_back(download.id);
  }
}

void DaoMiniDaoDownloadCardView::UpdateOverflow(
    const std::vector<ActiveDownload>& downloads) {
  const int overflow_count =
      static_cast<int>(downloads.size()) - kMaxVisibleRows;
  if (overflow_count > 0) {
    overflow_label_->SetText(l10n_util::GetStringFUTF16(
        IDS_DAO_MINI_DAO_DOWNLOAD_CARD_OVERFLOW,
        base::NumberToString16(overflow_count)));
    overflow_label_->SetVisible(true);
  } else {
    overflow_label_->SetVisible(false);
  }
}

void DaoMiniDaoDownloadCardView::CancelDownload(int id) {
  if (!browser_ || !browser_->profile() || id < 0) {
    return;
  }

  auto* manager = browser_->profile()->GetDownloadManager();
  if (!manager) {
    return;
  }

  download::DownloadItem* item =
      manager->GetDownload(static_cast<uint32_t>(id));
  if (item && IsDownloadFromThisWindow(item)) {
    item->Cancel(/*user_cancel=*/true);
  }
}

// static
std::string DaoMiniDaoDownloadCardView::FormatSpeed(
    int64_t bytes_per_sec) {
  if (bytes_per_sec <= 0) {
    return "0 B/s";
  }
  char buf[32];
  if (bytes_per_sec < 1024) {
    std::snprintf(buf, sizeof(buf), "%lld B/s",
                  static_cast<long long>(bytes_per_sec));
  } else if (bytes_per_sec < 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f KB/s", bytes_per_sec / 1024.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f MB/s",
                  bytes_per_sec / (1024.0 * 1024.0));
  }
  return std::string(buf);
}

}  // namespace dao
