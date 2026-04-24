// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_popup.h"

#include "third_party/blink/public/common/input/web_input_event.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/dao_control_center_extensions_section.h"
#include "dao/browser/ui/views/dao_control_center_more_menu.h"
#include "dao/browser/ui/views/dao_control_center_qr_view.h"
#include "dao/browser/ui/views/dao_control_center_utility_section.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/views/background.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {

void PaintCardShadow(gfx::Canvas* canvas,
                     const gfx::Rect& card_bounds,
                     int corner_radius) {
  // Two-layer shadow using Chromium's DrawLooper: ambient + key light.
  gfx::ShadowValues shadows;
  // Ambient shadow — large, soft
  shadows.emplace_back(gfx::Vector2d(0, 0), 40, PopupShadowOuter());
  // Key shadow — tighter, darker, slight downward offset
  shadows.emplace_back(gfx::Vector2d(0, 4), 16, PopupShadowInner());

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(SK_ColorTRANSPARENT);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setLooper(gfx::CreateShadowDrawLooper(shadows));
  canvas->DrawRoundRect(gfx::RectF(card_bounds), corner_radius, flags);
}

}  // namespace

// Background that paints the semi-transparent fill on the card itself.
class ControlCenterCardBackground : public views::Background {
 public:
  ControlCenterCardBackground(SkColor color, int radius)
      : color_(color), radius_(radius) {}
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    gfx::Rect bounds = view->GetLocalBounds();
    cc::PaintFlags flags;
    flags.setColor(color_);
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    canvas->DrawRoundRect(gfx::RectF(bounds), radius_, flags);
  }

 private:
  SkColor color_;
  int radius_;
};

BEGIN_METADATA(DaoControlCenterPopup)
END_METADATA

DaoControlCenterPopup::DaoControlCenterPopup(Browser* browser)
    : browser_(browser) {
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  SetVisible(false);

  // Card container — frosted glass: backdrop blur + semi-transparent bg.
  card_ = AddChildView(std::make_unique<views::View>());
  card_->SetPaintToLayer();
  card_->layer()->SetFillsBoundsOpaquely(false);
  card_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kCardCornerRadius));
  card_->layer()->SetIsFastRoundedCorner(true);
  card_->layer()->SetBackgroundBlur(30);

  auto* card_layout =
      card_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets(kCardPadding), 0));
  card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Extensions grid section
  extensions_section_ = card_->AddChildView(
      std::make_unique<DaoControlCenterExtensionsSection>(browser_));

  // Separator
  auto separator = std::make_unique<views::View>();
  separator->SetPreferredSize(gfx::Size(0, 1));
  separator_ = card_->AddChildView(std::move(separator));

  // Utility buttons row
  utility_section_ = card_->AddChildView(
      std::make_unique<DaoControlCenterUtilitySection>(this));

  // QR view (hidden by default, replaces main content)
  qr_view_ = card_->AddChildView(
      std::make_unique<DaoControlCenterQrView>(this));
  qr_view_->SetVisible(false);

  // More menu (hidden by default, replaces main content)
  more_menu_ = card_->AddChildView(
      std::make_unique<DaoControlCenterMoreMenu>(this));
  more_menu_->SetVisible(false);

  // Close popup when tab changes
  browser_->tab_strip_model()->AddObserver(this);

  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}

void DaoControlCenterPopup::ApplyTheme() {
  if (card_) {
    card_->SetBackground(std::make_unique<ControlCenterCardBackground>(
        PopupBackground(), kCardCornerRadius));
  }
  if (separator_) {
    separator_->SetBackground(
        views::CreateSolidBackground(SeparatorColor()));
  }
}

void DaoControlCenterPopup::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaint();
}

DaoControlCenterPopup::~DaoControlCenterPopup() {
  if (browser_ && browser_->tab_strip_model()) {
    browser_->tab_strip_model()->RemoveObserver(this);
  }
}

void DaoControlCenterPopup::ShowAt(const gfx::Point& anchor_bottom_right) {
  anchor_ = anchor_bottom_right;
  ShowMainPanel();

  // Reorder to topmost child so hit-testing finds us first.
  if (parent()) {
    parent()->ReorderChildView(this, parent()->children().size());
  }

  SetVisible(true);

  // Stack all ancestor layers above siblings in compositor order.
  for (ui::Layer* l = layer(); l && l->parent(); l = l->parent()) {
    l->parent()->StackAtTop(l);
  }

  // Block web content's native NSView from stealing events.
  auto* web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (web_contents) {
    BlockWebContentNativeEvents(web_contents);
  }

  if (extensions_section_) {
    extensions_section_->Refresh();
  }

  content::WebContentsObserver::Observe(web_contents);
}

void DaoControlCenterPopup::Hide() {
  SetVisible(false);
  content::WebContentsObserver::Observe(nullptr);

  // Restore web content event processing.
  auto* web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (web_contents) {
    UnblockWebContentNativeEvents(web_contents);
  }
}

void DaoControlCenterPopup::ShowQrView() {
  if (extensions_section_) extensions_section_->SetVisible(false);
  if (utility_section_) utility_section_->SetVisible(false);
  if (more_menu_) more_menu_->SetVisible(false);
  if (card_->children().size() > 1) {
    card_->children()[1]->SetVisible(false);
  }
  if (qr_view_) {
    qr_view_->SetVisible(true);
    qr_view_->GenerateQrCode();
  }
  InvalidateLayout();
  SchedulePaint();
}

void DaoControlCenterPopup::ShowMoreMenu() {
  if (extensions_section_) extensions_section_->SetVisible(false);
  if (utility_section_) utility_section_->SetVisible(false);
  if (qr_view_) qr_view_->SetVisible(false);
  if (card_->children().size() > 1) {
    card_->children()[1]->SetVisible(false);
  }
  if (more_menu_) more_menu_->SetVisible(true);
  InvalidateLayout();
  SchedulePaint();
}

void DaoControlCenterPopup::ShowMainPanel() {
  if (extensions_section_) extensions_section_->SetVisible(true);
  if (utility_section_) utility_section_->SetVisible(true);
  if (qr_view_) qr_view_->SetVisible(false);
  if (more_menu_) more_menu_->SetVisible(false);
  if (card_->children().size() > 1) {
    card_->children()[1]->SetVisible(true);
  }
  InvalidateLayout();
  SchedulePaint();
}

void DaoControlCenterPopup::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    Hide();
  }
}

void DaoControlCenterPopup::DidGetUserInteraction(
    const blink::WebInputEvent& event) {
  // User clicked/touched/scrolled the web content — close the popup.
  Hide();
}

void DaoControlCenterPopup::Layout(PassKey) {
  gfx::Size card_pref = card_->GetPreferredSize();
  int card_width = kCardWidth;
  int card_height = card_pref.height();

  // Right-align the card so its right edge aligns with the anchor point's x.
  constexpr int kMargin = 16;
  int card_x = anchor_.x() - card_width;
  int card_y = anchor_.y();

  if (card_x < kMargin) card_x = kMargin;
  if (card_y + card_height > height() - kMargin) {
    card_height = std::max(100, height() - card_y - kMargin);
  }

  card_->SetBounds(card_x, card_y, card_width, card_height);
}

void DaoControlCenterPopup::OnPaintBackground(gfx::Canvas* canvas) {
  // Paint the drop shadow on the popup's own layer (not clipped by card's
  // rounded corner layer). The card bounds are in our coordinate space.
  if (card_ && card_->GetVisible()) {
    PaintCardShadow(canvas, card_->bounds(), kCardCornerRadius);
  }
}

bool DaoControlCenterPopup::OnMousePressed(const ui::MouseEvent& event) {
  gfx::Point point = event.location();
  if (card_ && !card_->bounds().Contains(point)) {
    Hide();
    return true;
  }
  // Click is inside the card — don't consume it so child views (buttons)
  // receive the event.
  return false;
}

}  // namespace dao
