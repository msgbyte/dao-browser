// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_address_bar_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_control_center_button.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/font_list.h"
#include "ui/compositor/layer.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/button.h"
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
constexpr int kNavButtonSize = 24;
constexpr int kNavIconSize = 14;
constexpr int kNavButtonRadius = 6;

}  // namespace

// A small icon button used for back/forward/stop/refresh/chat in the address bar.
class NavIconButton : public views::Button {
  METADATA_HEADER(NavIconButton, views::Button)

 public:
  NavIconButton(views::Button::PressedCallback callback,
                LucideIcon icon,
                const std::u16string& accessible_name)
      : Button(std::move(callback)), icon_(icon) {
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(accessible_name);
  }

  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override {
    return gfx::Size(kNavButtonSize, kNavButtonSize);
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    float icon_size = static_cast<float>(kNavIconSize);
    float ox = (width() - icon_size) / 2.0f;
    float oy = (height() - icon_size) / 2.0f;
    SkColor color = highlighted_ ? dao::kSpaceActive
                   : nav_enabled_ ? icon_color_
                   : disabled_color_;
    DrawLucideIcon(canvas, icon_,
                   gfx::RectF(ox, oy, icon_size, icon_size), color);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    if (!nav_enabled_) return;
    Button::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(20, 0, 0, 0), kNavButtonRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void SetIconColor(SkColor color) {
    icon_color_ = color;
    // Compute disabled color: same hue but much lower opacity
    disabled_color_ = SkColorSetA(color, SkColorGetA(color) / 3);
    SchedulePaint();
  }

  void SetNavEnabled(bool enabled) {
    nav_enabled_ = enabled;
    SetEnabled(enabled);
    if (!enabled) {
      SetBackground(nullptr);
    }
    SchedulePaint();
  }

  void SetIcon(LucideIcon icon) {
    icon_ = icon;
    SchedulePaint();
  }

  void SetHighlighted(bool highlighted) {
    highlighted_ = highlighted;
    SchedulePaint();
  }

  bool highlighted() const { return highlighted_; }
  bool nav_enabled() const { return nav_enabled_; }

 private:
  LucideIcon icon_;
  SkColor icon_color_ = SkColorSetARGB(180, 100, 100, 100);
  SkColor disabled_color_ = SkColorSetARGB(60, 100, 100, 100);
  bool nav_enabled_ = true;
  bool highlighted_ = false;
};

BEGIN_METADATA(NavIconButton)
END_METADATA

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
  left_spacer->SetPreferredSize(gfx::Size(4, 1));
  left_spacer_ = AddChildView(std::move(left_spacer));

  // Navigation buttons: back, forward, close
  back_button_ = AddChildView(std::make_unique<NavIconButton>(
      base::BindRepeating(&DaoAddressBarView::OnBackButtonPressed,
                          base::Unretained(this)),
      LucideIcon::kArrowLeft, u"Go Back"));

  forward_button_ = AddChildView(std::make_unique<NavIconButton>(
      base::BindRepeating(&DaoAddressBarView::OnForwardButtonPressed,
                          base::Unretained(this)),
      LucideIcon::kArrowRight, u"Go Forward"));

  // Stop/Refresh button: shows X (stop) while loading, RotateCw (refresh) when loaded
  stop_refresh_button_ = AddChildView(std::make_unique<NavIconButton>(
      base::BindRepeating(&DaoAddressBarView::OnStopRefreshButtonPressed,
                          base::Unretained(this)),
      LucideIcon::kRotateCw, u"Reload"));

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

  // Chat button (opens agent sidebar)
  chat_button_ = AddChildView(std::make_unique<NavIconButton>(
      base::BindRepeating(&DaoAddressBarView::OnChatButtonPressed,
                          base::Unretained(this)),
      LucideIcon::kMessageCircle, u"Toggle Chat"));

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
  // If the click lands on any nav button, let it handle it
  for (views::Button* btn : {back_button_.get(), forward_button_.get(),
                              stop_refresh_button_.get(), chat_button_.get()}) {
    if (btn) {
      gfx::Point pt = event.location();
      views::View::ConvertPointToTarget(this, btn, &pt);
      if (btn->HitTestPoint(pt)) {
        return false;
      }
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
  UpdateNavButtonColors();
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
  UpdateNavButtonEnabled();
  UpdateStopRefreshButton();
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

void DaoAddressBarView::OnBackButtonPressed() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents =
      tab_strip_model_->GetActiveWebContents();
  if (contents && contents->GetController().CanGoBack()) {
    contents->GetController().GoBack();
  }
}

void DaoAddressBarView::OnForwardButtonPressed() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents =
      tab_strip_model_->GetActiveWebContents();
  if (contents && contents->GetController().CanGoForward()) {
    contents->GetController().GoForward();
  }
}

void DaoAddressBarView::OnStopRefreshButtonPressed() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  if (!contents) {
    return;
  }
  if (is_loading_) {
    contents->Stop();
  } else {
    contents->GetController().Reload(content::ReloadType::NORMAL, true);
  }
}

void DaoAddressBarView::OnChatButtonPressed() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_agent_sidebar()) {
    bool expanded = browser_view->dao_agent_sidebar()->Toggle();
    if (chat_button_) {
      static_cast<NavIconButton*>(chat_button_.get())->SetHighlighted(expanded);
    }
  }
}

void DaoAddressBarView::UpdateNavButtonEnabled() {
  if (!tab_strip_model_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  bool can_back = contents && contents->GetController().CanGoBack();
  bool can_forward = contents && contents->GetController().CanGoForward();

  if (back_button_) {
    static_cast<NavIconButton*>(back_button_.get())->SetNavEnabled(can_back);
  }
  if (forward_button_) {
    static_cast<NavIconButton*>(forward_button_.get())->SetNavEnabled(can_forward);
  }
}

void DaoAddressBarView::UpdateStopRefreshButton() {
  if (!tab_strip_model_ || !stop_refresh_button_) {
    return;
  }
  content::WebContents* contents = tab_strip_model_->GetActiveWebContents();
  is_loading_ = contents && contents->IsLoading();

  auto* btn = static_cast<NavIconButton*>(stop_refresh_button_.get());
  if (is_loading_) {
    btn->SetIcon(LucideIcon::kX);
    btn->SetAccessibleName(u"Stop Loading");
  } else {
    btn->SetIcon(LucideIcon::kRotateCw);
    btn->SetAccessibleName(u"Reload");
  }
}

void DaoAddressBarView::DidStartLoading() {
  UpdateStopRefreshButton();
}

void DaoAddressBarView::DidStopLoading() {
  UpdateStopRefreshButton();
  UpdateNavButtonEnabled();
}

void DaoAddressBarView::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  UpdateNavButtonEnabled();
  UpdateStopRefreshButton();
}

void DaoAddressBarView::UpdateNavButtonColors() {
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
      ? SkColorSetARGB(180, 255, 255, 255)
      : SkColorSetARGB(160, 0, 0, 0);

  for (views::Button* btn : {back_button_.get(), forward_button_.get(),
                              stop_refresh_button_.get(), chat_button_.get()}) {
    if (btn) {
      static_cast<NavIconButton*>(btn)->SetIconColor(icon_color);
    }
  }
  if (control_center_button_) {
    control_center_button_->SetIconColor(icon_color);
  }
}

}  // namespace dao
