// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/little_dao/dao_little_dao_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {
constexpr int kLeftPadding = 80;      // Space for traffic lights (~76px)
constexpr int kRightPadding = 0;
constexpr int kVerticalPadding = 8;   // Top/bottom padding inside 48px header
constexpr int kButtonCornerRadius = 8;
constexpr int kDisplayCornerRadius = 8;   // Rounded rectangle
constexpr int kElementSpacing = 8;
}  // namespace

BEGIN_METADATA(DaoLittleDaoView)
END_METADATA

DaoLittleDaoView::DaoLittleDaoView(Browser* browser)
    : browser_(browser), tab_strip_model_(browser->tab_strip_model()) {
  SetBackground(views::CreateSolidBackground(SidebarBackground()));

  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetMainAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kStretch);
  layout->SetInteriorMargin(gfx::Insets::TLBR(
      kVerticalPadding, kLeftPadding, kVerticalPadding, kRightPadding));
  layout->SetDefault(views::kMarginsKey,
                     gfx::Insets::TLBR(0, 0, 0, kElementSpacing));

  // URL display button — pill-shaped, shows hostname, click opens command bar
  url_display_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoLittleDaoView::ShowCommandBar,
                          base::Unretained(this)),
      u""));
  url_display_->SetEnabledTextColors(TextSecondary());
  url_display_->SetBackground(views::CreateRoundedRectBackground(
      SuggestionHover(), kDisplayCornerRadius));
  url_display_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, 12, 0, 12)));
  url_display_->SetPreferredSize(gfx::Size(0, 0));
  url_display_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  url_display_->SetAccessibleName(u"Address");
  url_display_->SetFocusBehavior(views::View::FocusBehavior::NEVER);
  url_display_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kUnbounded));
  // Install rounded-rect highlight path and enable InkDrop hover effect
  views::InstallRoundRectHighlightPathGenerator(
      url_display_, gfx::Insets(), kDisplayCornerRadius);
  views::InkDrop::Get(url_display_)->SetMode(
      views::InkDropHost::InkDropMode::ON_NO_GESTURE_HANDLER);
  views::InkDrop::Get(url_display_)->SetBaseColor(SK_ColorBLACK);
  views::InkDrop::Get(url_display_)->SetVisibleOpacity(0.04f);

  // "Open in Dao ⌘O" button
  open_button_ = AddChildView(std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoLittleDaoView::OpenInDao,
                          base::Unretained(this)),
      u"Open in Dao"));
  open_button_->SetEnabledTextColors(TextPrimary());
  open_button_->SetBackground(views::CreateRoundedRectBackground(
      SuggestionHover(), kButtonCornerRadius));
  open_button_->SetBorder(
      views::CreateEmptyBorder(gfx::Insets::TLBR(0, 14, 0, 10)));
  open_button_->SetAccessibleName(u"Open in Dao");
  // Hover effect for open button
  views::InstallRoundRectHighlightPathGenerator(
      open_button_, gfx::Insets(), kButtonCornerRadius);
  views::InkDrop::Get(open_button_)->SetMode(
      views::InkDropHost::InkDropMode::ON_NO_GESTURE_HANDLER);
  views::InkDrop::Get(open_button_)->SetBaseColor(SK_ColorBLACK);
  views::InkDrop::Get(open_button_)->SetVisibleOpacity(0.04f);

  // Shortcut hint label — added as child of button, manually positioned
  // in Layout() since LabelButton's internal layout ignores extra children.
  shortcut_label_ =
      open_button_->AddChildView(std::make_unique<views::Label>(u"\u2318+O"));
  shortcut_label_->SetEnabledColor(TextMuted());

  // Expand button preferred size to include room for the shortcut label,
  // and force the same height as the URL display for visual alignment.
  gfx::Size btn_pref = open_button_->GetPreferredSize();
  gfx::Size sc_pref = shortcut_label_->GetPreferredSize();
  open_button_->SetPreferredSize(
      gfx::Size(btn_pref.width() + sc_pref.width() + 6, 0));

  // Register Cmd+O accelerator
  AddAccelerator(ui::Accelerator(ui::VKEY_O, ui::EF_COMMAND_DOWN));

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

void DaoLittleDaoView::AddedToWidget() {}

gfx::Size DaoLittleDaoView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kBarHeight);
}

void DaoLittleDaoView::Layout(PassKey) {
  // Run the FlexLayout on this view's children first.
  if (GetLayoutManager()) {
    GetLayoutManager()->Layout(this);
  }

  // Reposition traffic lights on every layout pass so macOS cannot reset them.
  if (GetWidget()) {
    constexpr int kTrafficLightX = 13;
    constexpr int kTrafficLightY = 6;
    dao::SetTrafficLightsPosition(GetWidget()->GetNativeWindow(),
                                   kTrafficLightX, kTrafficLightY);
  }

  // Manually position the shortcut label inside open_button_ since
  // LabelButton's internal layout only handles its own label+image.
  if (shortcut_label_ && open_button_ && open_button_->width() > 0) {
    gfx::Size pref = shortcut_label_->GetPreferredSize();
    int x = open_button_->width() - pref.width() - 10;
    int y = (open_button_->height() - pref.height()) / 2;
    shortcut_label_->SetBoundsRect(gfx::Rect(x, y, pref.width(), pref.height()));
  }
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
  SetBackground(views::CreateSolidBackground(SidebarBackground()));

  SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(0, 0, 1, 0), SeparatorColor()));

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

void DaoLittleDaoView::OpenInDao() {
  DaoLittleDaoController::TransferToMainBrowser(browser_);
}

bool DaoLittleDaoView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_O &&
      accelerator.IsCmdDown()) {
    OpenInDao();
    return true;
  }
  return false;
}

}  // namespace dao
