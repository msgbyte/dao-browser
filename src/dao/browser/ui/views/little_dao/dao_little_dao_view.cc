// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_little_dao_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

namespace {
constexpr int kLeftPadding = 80;      // Space for traffic lights (~76px)
constexpr int kRightPadding = 16;
constexpr int kVerticalPadding = 8;   // Top/bottom padding inside 48px header
constexpr int kDisplayHeight = 28;
constexpr int kButtonCornerRadius = 8;
constexpr int kDisplayCornerRadius = 14;  // Pill shape (half of height)
constexpr int kElementSpacing = 8;
constexpr SkColor kButtonBackground = SkColorSetARGB(20, 255, 255, 255);
constexpr SkColor kDisplayBackground = SkColorSetARGB(20, 255, 255, 255);
}  // namespace

BEGIN_METADATA(DaoLittleDaoView)
END_METADATA

DaoLittleDaoView::DaoLittleDaoView(Browser* browser)
    : browser_(browser), tab_strip_model_(browser->tab_strip_model()) {
  SetBackground(views::CreateSolidBackground(SK_ColorWHITE));

  // Top rounded corners
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(
      dao::kContentCornerRadius, dao::kContentCornerRadius, 0, 0));
  layer()->SetIsFastRoundedCorner(true);

  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetMainAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  layout->SetInteriorMargin(gfx::Insets::TLBR(
      kVerticalPadding, kLeftPadding, kVerticalPadding, kRightPadding));
  layout->SetDefault(views::kMarginsKey,
                     gfx::Insets::TLBR(0, 0, 0, kElementSpacing));

  // URL display button — pill-shaped, shows hostname, click opens command bar
  url_display_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoLittleDaoView::ShowCommandBar,
                          base::Unretained(this)),
      u""));
  url_display_->SetEnabledTextColors(SkColorSetRGB(60, 60, 60));
  url_display_->SetBackground(views::CreateRoundedRectBackground(
      kDisplayBackground, kDisplayCornerRadius));
  url_display_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, 12, 0, 12)));
  url_display_->SetPreferredSize(gfx::Size(0, kDisplayHeight));
  url_display_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_display_->SetAccessibleName(u"Address");
  url_display_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  url_display_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded));
  // Install pill-shaped highlight for hover/focus ink effects
  views::InstallRoundRectHighlightPathGenerator(
      url_display_, gfx::Insets(), kDisplayCornerRadius);

  // "Open in Dao" button — subtle rounded style
  open_button_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(
          [](Browser* b) {
            DaoLittleDaoController::TransferToMainBrowser(b);
          },
          browser_),
      u"Open in Dao"));
  open_button_->SetEnabledTextColors(kTextPrimary);
  open_button_->SetBackground(views::CreateRoundedRectBackground(
      kButtonBackground, kButtonCornerRadius));
  open_button_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(5, 14, 5, 14)));
  open_button_->SetAccessibleName(u"Open in Dao");

  tab_strip_model_->AddObserver(this);
  ObserveActiveWebContents();
  UpdateURLDisplay();
  UpdateBackgroundColor();
}

DaoLittleDaoView::~DaoLittleDaoView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

gfx::Rect DaoLittleDaoView::open_in_dao_button_bounds() const {
  if (!open_button_)
    return gfx::Rect();
  // Convert button bounds to parent (BrowserView) coordinates.
  gfx::Rect btn_bounds = open_button_->bounds();
  gfx::Point origin = btn_bounds.origin();
  views::View::ConvertPointToTarget(this, parent(), &origin);
  btn_bounds.set_origin(origin);
  return btn_bounds;
}

gfx::Rect DaoLittleDaoView::url_display_bounds() const {
  if (!url_display_)
    return gfx::Rect();
  gfx::Rect display_bounds = url_display_->bounds();
  gfx::Point origin = display_bounds.origin();
  views::View::ConvertPointToTarget(this, parent(), &origin);
  display_bounds.set_origin(origin);
  return display_bounds;
}

void DaoLittleDaoView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ObserveActiveWebContents();
  UpdateURLDisplay();
  UpdateBackgroundColor();
}

void DaoLittleDaoView::TabChangedAt(content::WebContents* contents,
                                     int index,
                                     TabChangeType change_type) {
  if (tab_strip_model_ && index == tab_strip_model_->active_index()) {
    UpdateURLDisplay();
    UpdateBackgroundColor();
  }
}

void DaoLittleDaoView::OnBackgroundColorChanged() {
  UpdateBackgroundColor();
}

gfx::Size DaoLittleDaoView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kBarHeight);
}

void DaoLittleDaoView::UpdateURLDisplay() {
  if (!tab_strip_model_ || !url_display_)
    return;

  auto* web_contents = tab_strip_model_->GetActiveWebContents();
  if (!web_contents) {
    url_display_->SetText(u"");
    return;
  }

  GURL url = web_contents->GetVisibleURL();
  if (!url.is_valid() || url.IsAboutBlank()) {
    url_display_->SetText(u"");
    return;
  }

  // Show just the hostname for a cleaner look
  std::string host = url.host();
  if (host.empty()) {
    url_display_->SetText(base::UTF8ToUTF16(url.spec()));
  } else {
    url_display_->SetText(base::UTF8ToUTF16(host));
  }
}

void DaoLittleDaoView::UpdateBackgroundColor() {
  SkColor bg_color = SK_ColorWHITE;
  if (tab_strip_model_) {
    auto* web_contents = tab_strip_model_->GetActiveWebContents();
    if (web_contents) {
      auto* rwhv = web_contents->GetRenderWidgetHostView();
      if (rwhv) {
        auto color = rwhv->GetBackgroundColor();
        if (color.has_value()) {
          bg_color = color.value();
        }
      }
    }
  }
  SetBackground(views::CreateSolidBackground(bg_color));

  // Adaptive text colors based on background luminance
  int r = SkColorGetR(bg_color);
  int g = SkColorGetG(bg_color);
  int b = SkColorGetB(bg_color);
  double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
  bool is_dark = luminance < 128;

  if (url_display_) {
    url_display_->SetEnabledTextColors(
        is_dark ? SkColorSetRGB(220, 220, 220) : SkColorSetRGB(60, 60, 60));
    url_display_->SetBackground(views::CreateRoundedRectBackground(
        is_dark ? SkColorSetARGB(20, 255, 255, 255)
                : SkColorSetARGB(15, 0, 0, 0),
        kDisplayCornerRadius));
  }

  if (open_button_) {
    open_button_->SetEnabledTextColors(
        is_dark ? kTextPrimary : SkColorSetRGB(40, 40, 40));
    open_button_->SetBackground(views::CreateRoundedRectBackground(
        is_dark ? kButtonBackground : SkColorSetARGB(15, 0, 0, 0),
        kButtonCornerRadius));
  }

  // Adaptive bottom separator
  SkColor separator_color = is_dark
      ? SkColorSetARGB(25, 255, 255, 255)
      : SkColorSetARGB(25, 0, 0, 0);
  SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(0, 0, 1, 0), separator_color));

  SchedulePaint();
}

void DaoLittleDaoView::ObserveActiveWebContents() {
  if (!tab_strip_model_)
    return;
  auto* web_contents = tab_strip_model_->GetActiveWebContents();
  if (web_contents != content::WebContentsObserver::web_contents()) {
    content::WebContentsObserver::Observe(web_contents);
  }
}

void DaoLittleDaoView::ShowCommandBar() {
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view)
    return;
  auto* command_bar = browser_view->dao_command_bar();
  if (command_bar) {
    command_bar->Show();
  }
}

}  // namespace dao
