// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_address_bar_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_button.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/font_list.h"
#include "ui/compositor/layer.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/style/typography.h"
#include "ui/views/widget/widget.h"
#include "ui/compositor/scoped_layer_animation_settings.h"

namespace dao {

namespace {
constexpr SkColor kHostColor = SkColorSetRGB(60, 60, 60);
constexpr SkColor kPathColor = SkColorSetRGB(124, 124, 124);
constexpr int kUrlPillRadius = 8;
constexpr SkColor kUrlHoverBg = SkColorSetARGB(15, 0, 0, 0);
constexpr int kFontSize = 12;
}  // namespace

BEGIN_METADATA(DaoAddressBarView)
END_METADATA

DaoAddressBarView::DaoAddressBarView(Browser* browser)
    : browser_(browser), tab_strip_model_(browser->tab_strip_model()) {
  SetBackground(views::CreateSolidBackground(SK_ColorWHITE));  // default
  SetNotifyEnterExitOnChild(true);

  // Top rounded corners (bottom corners are on the contents container)
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(
      dao::kContentCornerRadius, dao::kContentCornerRadius, 0, 0));
  layer()->SetIsFastRoundedCorner(true);

  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetMainAxisAlignment(views::LayoutAlignment::kStart);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

  gfx::FontList font({"system-ui"}, gfx::Font::NORMAL, kFontSize,
                      gfx::Font::Weight::NORMAL);

  // Traffic light spacer + toggle button (visible only when collapsed).
  // The spacer clears the macOS traffic lights (~70px), then the toggle
  // button sits right next to them.
  auto traffic_light_spacer = std::make_unique<views::View>();
  traffic_light_spacer->SetPreferredSize(gfx::Size(70, kBarHeight));
  traffic_light_spacer->SetVisible(false);
  traffic_light_spacer_ = AddChildView(std::move(traffic_light_spacer));

  auto toggle_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoAddressBarView::OnToggleButtonPressed,
                          base::Unretained(this)),
      u"\u2630");  // ☰ hamburger icon
  toggle_btn->SetEnabledTextColors(kHostColor);
  toggle_btn->SetTextSubpixelRenderingEnabled(false);
  toggle_btn->SetLabelStyle(views::style::STYLE_BODY_3_EMPHASIS);
  toggle_btn->SetPreferredSize(gfx::Size(kBarHeight, kBarHeight));
  toggle_btn->SetInstallFocusRingOnFocus(false);
  toggle_btn->SetTooltipText(u"Toggle Sidebar (\u2318S)");
  toggle_btn->SetAccessibleName(u"Toggle Sidebar");
  toggle_btn->SetVisible(false);
  toggle_btn->SetPaintToLayer();
  toggle_btn->layer()->SetFillsBoundsOpaquely(false);
  toggle_btn->layer()->SetOpacity(0.0f);
  sidebar_toggle_button_ = AddChildView(std::move(toggle_btn));

  // Left spacer to balance the right-side button for URL centering
  auto left_spacer = std::make_unique<views::View>();
  left_spacer->SetPreferredSize(gfx::Size(28, 1));
  left_spacer_ = AddChildView(std::move(left_spacer));

  // Left flex spacer — pushes URL pill toward center
  auto left_flex = std::make_unique<views::View>();
  left_flex->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithWeight(1));
  AddChildView(std::move(left_flex));

  // URL pill: wraps host + path labels, sized to content, centered by spacers.
  url_container_ = AddChildView(std::make_unique<views::View>());
  auto* url_layout =
      url_container_->SetLayoutManager(std::make_unique<views::FlexLayout>());
  url_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  url_layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);
  url_layout->SetInteriorMargin(gfx::Insets::VH(2, 8));
  url_container_->SetCanProcessEventsWithinSubtree(false);
  url_container_->SetPaintToLayer();
  url_container_->layer()->SetFillsBoundsOpaquely(false);
  url_container_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(kUrlPillRadius));
  url_container_->layer()->SetIsFastRoundedCorner(true);

  host_label_ = url_container_->AddChildView(std::make_unique<views::Label>());
  host_label_->SetFontList(font);
  host_label_->SetEnabledColor(kHostColor);
  host_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  host_label_->SetSubpixelRenderingEnabled(false);
  host_label_->SetCanProcessEventsWithinSubtree(false);

  path_label_ = url_container_->AddChildView(std::make_unique<views::Label>());
  path_label_->SetFontList(font);
  path_label_->SetEnabledColor(kPathColor);
  path_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  path_label_->SetSubpixelRenderingEnabled(false);
  path_label_->SetCanProcessEventsWithinSubtree(false);
  path_label_->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToMinimum,
                               views::MaximumFlexSizeRule::kPreferred));

  // Right flex spacer — pushes CC button to far right
  auto right_flex = std::make_unique<views::View>();
  right_flex->SetProperty(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kScaleToZero,
                               views::MaximumFlexSizeRule::kUnbounded)
          .WithWeight(1));
  AddChildView(std::move(right_flex));

  // Control center button (fixed at right edge)
  control_center_button_ = AddChildView(
      std::make_unique<DaoControlCenterButton>(browser));

  tab_strip_model_->AddObserver(this);
  ObserveActiveWebContents();
  UpdateURL();
  UpdateBackgroundColor();
}

DaoAddressBarView::~DaoAddressBarView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoAddressBarView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  ObserveActiveWebContents();
  UpdateURL();
  UpdateBackgroundColor();
}

void DaoAddressBarView::TabChangedAt(content::WebContents* contents,
                                     int index,
                                     TabChangeType change_type) {
  // Only update if the changed tab is the active one
  if (tab_strip_model_ && index == tab_strip_model_->active_index()) {
    UpdateURL();
    UpdateBackgroundColor();
  }
}

void DaoAddressBarView::UpdateURL() {
  if (!tab_strip_model_) {
    host_label_->SetText(u"");
    path_label_->SetText(u"");
    return;
  }

  auto* web_contents = tab_strip_model_->GetActiveWebContents();
  if (!web_contents) {
    host_label_->SetText(u"");
    path_label_->SetText(u"");
    return;
  }

  GURL url = web_contents->GetVisibleURL();
  if (!url.is_valid()) {
    host_label_->SetText(u"");
    path_label_->SetText(u"");
    return;
  }

  // For non-standard schemes (about:blank, dao://, etc.), show full URL
  // in the host label with no path split.
  if (!url.SchemeIsHTTPOrHTTPS()) {
    host_label_->SetText(base::UTF8ToUTF16(url.spec()));
    path_label_->SetText(u"");
    return;
  }

  // Host part
  std::string host = url.host();
  host_label_->SetText(base::UTF8ToUTF16(host));

  // Path + query part
  std::string path_and_query = url.path();
  if (url.has_query()) {
    path_and_query += "?" + url.query();
  }
  if (path_and_query == "/") {
    path_and_query.clear();
  }
  if (!path_and_query.empty()) {
    path_label_->SetText(base::UTF8ToUTF16(" " + path_and_query));
  } else {
    path_label_->SetText(u"");
  }
}

bool DaoAddressBarView::OnMousePressed(const ui::MouseEvent& event) {
  // If the click lands on the control center button, let it handle it
  if (control_center_button_) {
    gfx::Point pt = event.location();
    views::View::ConvertPointToTarget(this, control_center_button_, &pt);
    if (control_center_button_->HitTestPoint(pt)) {
      return false;  // Let the button handle the click
    }
  }
  // If the click lands on the sidebar toggle button, let it handle it
  if (sidebar_toggle_button_ && sidebar_toggle_button_->GetVisible()) {
    gfx::Point pt = event.location();
    views::View::ConvertPointToTarget(this, sidebar_toggle_button_, &pt);
    if (sidebar_toggle_button_->HitTestPoint(pt)) {
      return false;  // Let the button handle the click
    }
  }
  // Click elsewhere on address bar opens the command bar
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->Show();
  }
  return true;
}

void DaoAddressBarView::OnMouseMoved(const ui::MouseEvent& event) {
  bool over_url = url_container_ &&
                  url_container_->GetMirroredBounds().Contains(event.location());
  UpdateUrlContainerHover(over_url);
}

void DaoAddressBarView::OnMouseExited(const ui::MouseEvent& event) {
  UpdateUrlContainerHover(false);
}

void DaoAddressBarView::UpdateUrlContainerHover(bool hovered) {
  if (url_hovered_ == hovered) {
    return;
  }
  url_hovered_ = hovered;
  if (url_container_) {
    if (hovered) {
      url_container_->SetBackground(
          views::CreateRoundedRectBackground(kUrlHoverBg, kUrlPillRadius));
    } else {
      url_container_->SetBackground(nullptr);
    }
  }
}

views::View* DaoAddressBarView::control_center_button() const {
  return control_center_button_;
}

gfx::Size DaoAddressBarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(0, kBarHeight);
}

void DaoAddressBarView::OnBackgroundColorChanged() {
  UpdateBackgroundColor();
}

void DaoAddressBarView::UpdateBackgroundColor() {
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

  // Adaptive bottom separator: 0.1 opacity white on dark, 0.1 opacity black on light
  int r = SkColorGetR(bg_color);
  int g = SkColorGetG(bg_color);
  int b = SkColorGetB(bg_color);
  double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
  SkColor separator_color = luminance < 128
      ? SkColorSetARGB(25, 255, 255, 255)   // dark bg → white 0.1
      : SkColorSetARGB(25, 0, 0, 0);        // light bg → black 0.1
  SetBorder(views::CreateSolidSidedBorder(
      gfx::Insets::TLBR(0, 0, 1, 0), separator_color));

  UpdateToggleButtonColor();
  SchedulePaint();
}

void DaoAddressBarView::ObserveActiveWebContents() {
  if (!tab_strip_model_) {
    return;
  }
  auto* web_contents = tab_strip_model_->GetActiveWebContents();
  if (web_contents != content::WebContentsObserver::web_contents()) {
    content::WebContentsObserver::Observe(web_contents);
  }
}

void DaoAddressBarView::SetSidebarCollapsed(bool collapsed) {
  if (sidebar_collapsed_ == collapsed) {
    return;
  }
  sidebar_collapsed_ = collapsed;

  if (traffic_light_spacer_) {
    // In fullscreen there are no traffic lights, so never show the spacer.
    bool fullscreen = GetWidget() && GetWidget()->IsFullscreen();
    traffic_light_spacer_->SetVisible(collapsed && !fullscreen);
  }
  if (left_spacer_) {
    left_spacer_->SetVisible(!collapsed);
  }

  if (sidebar_toggle_button_) {
    if (collapsed) {
      // Show button and fade in smoothly.
      // The button slides from right to left naturally as the address bar
      // repositions during sidebar collapse animation.
      sidebar_toggle_button_->SetVisible(true);
      UpdateToggleButtonColor();
      sidebar_toggle_button_->layer()->SetOpacity(0.0f);
      {
        ui::ScopedLayerAnimationSettings settings(
            sidebar_toggle_button_->layer()->GetAnimator());
        settings.SetTransitionDuration(base::Milliseconds(200));
        settings.SetTweenType(gfx::Tween::EASE_OUT);
        sidebar_toggle_button_->layer()->SetOpacity(1.0f);
      }
    } else {
      // Hide immediately when expanding (sidebar covers the area anyway)
      sidebar_toggle_button_->layer()->GetAnimator()->StopAnimating();
      sidebar_toggle_button_->layer()->SetOpacity(0.0f);
      sidebar_toggle_button_->SetVisible(false);
    }
  }
}

void DaoAddressBarView::UpdateToggleButtonColor() {
  if (!sidebar_toggle_button_ || !sidebar_toggle_button_->GetVisible()) {
    return;
  }
  // Adapt icon color to match the address bar background luminance
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
  int r = SkColorGetR(bg_color);
  int g = SkColorGetG(bg_color);
  int b = SkColorGetB(bg_color);
  double luminance = 0.299 * r + 0.587 * g + 0.114 * b;
  SkColor icon_color = luminance < 128
      ? SkColorSetARGB(230, 255, 255, 255)  // dark bg → white icon
      : SkColorSetARGB(200, 0, 0, 0);       // light bg → dark icon
  sidebar_toggle_button_->SetEnabledTextColors(icon_color);
}

void DaoAddressBarView::OnToggleButtonPressed() {
  if (toggle_callback_) {
    toggle_callback_.Run();
    return;
  }
  // Fallback: directly toggle sidebar via BrowserView
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_sidebar()) {
    browser_view->dao_sidebar()->ToggleCollapsed();
  }
}

}  // namespace dao
