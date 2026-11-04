// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_utility_section.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/performance/dao_high_performance_mode.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_control_center_tile.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"

namespace dao {

namespace {

// Wide status tile: round icon badge followed by a title and status line.
constexpr int kStatusTileHeight = 52;
constexpr int kStatusTileInset = 10;
constexpr int kStatusBadgeSize = 32;
constexpr int kStatusIconSize = 16;
constexpr int kStatusTextGap = 10;
constexpr int kStatusTextSpacing = 1;

constexpr int kTitleFontSize = 12;
constexpr int kLabelFontSize = 11;

void DrawFilledMoonIcon(gfx::Canvas* canvas,
                        const gfx::RectF& rect,
                        SkColor color) {
  const float scale = rect.width() / 24.0f;
  SkPath path;
  SkParsePath::FromSVGString(
      "M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46."
      "402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803."
      "401",
      &path);
  SkMatrix matrix;
  matrix.setScale(scale, scale);
  matrix.postTranslate(rect.x(), rect.y());
  path = path.makeTransform(matrix);

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(color);
  canvas->DrawPath(path, flags);
}

std::unique_ptr<views::Label> CreateTileLabel(const std::u16string& text,
                                              int font_size,
                                              gfx::Font::Weight weight) {
  auto label = std::make_unique<views::Label>(text);
  label->SetFontList(
      gfx::FontList({"system-ui"}, gfx::Font::NORMAL, font_size, weight));
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  label->SetSkipSubpixelRenderingOpacityCheck(true);
  label->SetCanProcessEventsWithinSubtree(false);
  return label;
}

// A wide tile with a round icon badge, a title and a status line. When
// active, the badge switches to the accent color.
class StatusTile : public DaoControlCenterTileButton {
  METADATA_HEADER(StatusTile, DaoControlCenterTileButton)

 public:
  StatusTile(const std::u16string& title,
             LucideIcon icon,
             views::Button::PressedCallback callback)
      : DaoControlCenterTileButton(std::move(callback)), icon_(icon) {
    title_ = AddChildView(
        CreateTileLabel(title, kTitleFontSize, gfx::Font::Weight::SEMIBOLD));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    status_ = AddChildView(CreateTileLabel(std::u16string(), kLabelFontSize,
                                           gfx::Font::Weight::NORMAL));
    status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);

    SetPreferredSize(gfx::Size(0, kStatusTileHeight));
    SetAccessibleName(title);
    ApplyTheme();
  }

  void SetStatus(const std::u16string& status) { status_->SetText(status); }

  void SetActive(bool active) {
    if (active_ == active) {
      return;
    }
    active_ = active;
    SchedulePaint();
  }

  void ApplyTheme() override {
    title_->SetEnabledColor(ControlCenterTileTitleColor());
    status_->SetEnabledColor(ControlCenterTileSubtitleColor());
  }

  void Layout(PassKey) override {
    const int text_x = kStatusTileInset + kStatusBadgeSize + kStatusTextGap;
    const int text_width = std::max(0, width() - text_x - kStatusTileInset);
    const int title_height = title_->font_list().GetHeight();
    const int status_height = status_->font_list().GetHeight();
    int y =
        (height() - title_height - kStatusTextSpacing - status_height) / 2;
    title_->SetBounds(text_x, y, text_width, title_height);
    y += title_height + kStatusTextSpacing;
    status_->SetBounds(text_x, y, text_width, status_height);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const bool active = active_ && visually_enabled();
    const gfx::RectF badge(kStatusTileInset,
                           (height() - kStatusBadgeSize) / 2.0f,
                           kStatusBadgeSize, kStatusBadgeSize);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(active ? ControlCenterTileActiveIconBackground()
                          : ControlCenterTileIconBackground());
    canvas->DrawCircle(badge.CenterPoint(), kStatusBadgeSize / 2.0f, flags);

    gfx::RectF icon_rect = badge;
    icon_rect.Inset((kStatusBadgeSize - kStatusIconSize) / 2.0f);
    const SkColor icon_color =
        active ? SK_ColorWHITE : ControlCenterTileIconColor();
    if (active && icon_ == LucideIcon::kMoon) {
      DrawFilledMoonIcon(canvas, icon_rect, icon_color);
    } else {
      DrawLucideIcon(canvas, icon_, icon_rect, icon_color);
    }
  }

 private:
  LucideIcon icon_;
  raw_ptr<views::Label> title_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  bool active_ = false;
};

BEGIN_METADATA(StatusTile)
END_METADATA

}  // namespace

BEGIN_METADATA(DaoControlCenterUtilitySection)
END_METADATA

DaoControlCenterUtilitySection::DaoControlCenterUtilitySection(
    DaoControlCenterPopup* popup)
    : popup_(popup) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(),
      DaoControlCenterPopup::kTileSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Wide status tiles: dark mode and high performance mode.
  views::View* status_row = AddChildView(CreateControlCenterTileRow());
  {
    auto tile = std::make_unique<StatusTile>(
        l10n_util::GetStringUTF16(IDS_DAO_FORCE_DARK_MODE_TITLE),
        LucideIcon::kMoon,
        base::BindRepeating(
            &DaoControlCenterUtilitySection::OnForceDarkModeClicked,
            base::Unretained(this)));
    tile->SetAccessibleName(
        l10n_util::GetStringUTF16(IDS_DAO_FORCE_DARK_MODE_LABEL));
    tile->GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
    force_dark_mode_button_ = status_row->AddChildView(std::move(tile));
  }
  {
    auto tile = std::make_unique<StatusTile>(
        l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE),
        LucideIcon::kZap,
        base::BindRepeating(
            &DaoControlCenterUtilitySection::OnHighPerformanceModeClicked,
            base::Unretained(this)));
    tile->GetViewAccessibility().SetRole(ax::mojom::Role::kToggleButton);
    tile->SetTooltipText(
        l10n_util::GetStringUTF16(IDS_DAO_HIGH_PERFORMANCE_MODE));
    high_performance_button_ = status_row->AddChildView(std::move(tile));
  }

  // Compact action tiles: QR code, Mini Dao, more.
  views::View* action_row = AddChildView(CreateControlCenterTileRow());
  action_row->AddChildView(std::make_unique<DaoControlCenterActionTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_QR_CODE),
      LucideIcon::kQrCode,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnQrClicked,
                          base::Unretained(this))));
  action_row->AddChildView(std::make_unique<DaoControlCenterActionTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MINI_DAO),
      LucideIcon::kExternalLink,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnMiniDaoClicked,
                          base::Unretained(this))));
  action_row->AddChildView(std::make_unique<DaoControlCenterActionTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MORE),
      LucideIcon::kEllipsis,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnMoreClicked,
                          base::Unretained(this))));

  Refresh();
  popup_->browser()->tab_strip_model()->AddObserver(this);
  ObserveActiveWebContents();
}

DaoControlCenterUtilitySection::~DaoControlCenterUtilitySection() = default;

void DaoControlCenterUtilitySection::Refresh() {
  for (views::View* row : children()) {
    for (views::View* child : row->children()) {
      if (auto* tile = views::AsViewClass<DaoControlCenterTileButton>(child)) {
        tile->ApplyTheme();
      }
    }
  }
  RefreshForceDarkModeState();
}

void DaoControlCenterUtilitySection::OnQrClicked() {
  if (popup_) {
    popup_->ShowQrView();
  }
}

void DaoControlCenterUtilitySection::OnMiniDaoClicked() {
  if (!popup_) {
    return;
  }

  BrowserWindowInterface* browser = popup_->browser();
  popup_->Hide();

  BrowserWindowInterface* little_dao_browser =
      DaoLittleDaoController::ExtractActiveTabToLittleDao(browser);
  if (little_dao_browser || !browser) {
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view || !browser_view->dao_toast()) {
    return;
  }

  browser_view->dao_toast()->ShowToast(l10n_util::GetStringUTF16(
      IDS_DAO_CONTROL_CENTER_MINI_DAO_FAILED_TOAST));
}

void DaoControlCenterUtilitySection::OnForceDarkModeClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }

  if (!IsForceDarkModeAvailable()) {
    RefreshForceDarkModeState();
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(popup_->browser());
    if (browser_view && browser_view->dao_toast()) {
      browser_view->dao_toast()->ShowToast(l10n_util::GetStringUTF16(
          IDS_DAO_FORCE_DARK_MODE_DISABLED_TOOLTIP));
    }
    return;
  }

  Profile* profile = popup_->browser()->GetProfile();
  SetForceDarkModeUserEnabled(profile, !IsForceDarkModeUserEnabled(profile));
  RefreshForceDarkModeState();
}

void DaoControlCenterUtilitySection::RefreshForceDarkModeState() {
  if (!force_dark_mode_button_) {
    return;
  }

  auto* tile = static_cast<StatusTile*>(force_dark_mode_button_.get());
  Profile* profile =
      popup_ && popup_->browser() ? popup_->browser()->GetProfile() : nullptr;
  const bool available = IsForceDarkModeAvailable();
  const bool enabled = IsForceDarkModeUserEnabled(profile);
  const bool on = available && enabled;
  tile->SetActive(enabled);
  tile->SetVisualEnabled(available);
  tile->SetStatus(l10n_util::GetStringUTF16(
      on ? IDS_DAO_CONTROL_CENTER_STATUS_ON
         : IDS_DAO_CONTROL_CENTER_STATUS_OFF));
  tile->GetViewAccessibility().SetCheckedState(
      on ? ax::mojom::CheckedState::kTrue : ax::mojom::CheckedState::kFalse);
  tile->SetTooltipText(l10n_util::GetStringUTF16(
      available ? IDS_DAO_FORCE_DARK_MODE_TOOLTIP
                : IDS_DAO_FORCE_DARK_MODE_DISABLED_TOOLTIP));
}

void DaoControlCenterUtilitySection::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    ObserveActiveWebContents();
  }
}

void DaoControlCenterUtilitySection::WebContentsDestroyed() {
  high_performance_subscription_ = {};
  Observe(nullptr);
  UpdateHighPerformanceMode(false);
}

void DaoControlCenterUtilitySection::OnHighPerformanceModeClicked() {
  auto* contents = popup_->browser()->tab_strip_model()->GetActiveWebContents();
  if (contents) {
    DaoHighPerformanceMode::SetEnabled(
        contents, !DaoHighPerformanceMode::IsEnabled(contents));
  }
}

void DaoControlCenterUtilitySection::ObserveActiveWebContents() {
  auto* contents = popup_->browser()->tab_strip_model()->GetActiveWebContents();
  high_performance_subscription_ = {};
  Observe(contents);
  if (contents) {
    high_performance_subscription_ = DaoHighPerformanceMode::AddObserver(
        contents, base::BindRepeating(
                      &DaoControlCenterUtilitySection::UpdateHighPerformanceMode,
                      base::Unretained(this)));
  }
  UpdateHighPerformanceMode(DaoHighPerformanceMode::IsEnabled(contents));
}

void DaoControlCenterUtilitySection::UpdateHighPerformanceMode(bool enabled) {
  auto* tile = static_cast<StatusTile*>(high_performance_button_.get());
  tile->SetEnabled(web_contents() != nullptr);
  tile->SetVisualEnabled(web_contents() != nullptr);
  tile->SetActive(enabled);
  tile->SetStatus(l10n_util::GetStringUTF16(
      enabled ? IDS_DAO_CONTROL_CENTER_STATUS_ON
              : IDS_DAO_CONTROL_CENTER_STATUS_OFF));
  tile->GetViewAccessibility().SetCheckedState(
      enabled ? ax::mojom::CheckedState::kTrue
              : ax::mojom::CheckedState::kFalse);
}

void DaoControlCenterUtilitySection::OnMoreClicked() {
  if (popup_) {
    popup_->ShowMoreMenu();
  }
}

}  // namespace dao
