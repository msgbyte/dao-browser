// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/frame/contents_web_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_tab_commands.h"
#include "ui/base/l10n/l10n_util.h"
#include "base/task/single_thread_task_runner.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/geometry/transform.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_corner_overlay_view.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"
#include "dao/browser/ui/webui/dao_sidebar_ui.h"
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
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"
#include "ui/views/widget/widget.h"

namespace dao {

namespace {

constexpr base::TimeDelta kCommandSInterceptToastDelay =
    base::Milliseconds(250);
constexpr base::TimeDelta kCommandSConfirmationDuration =
    base::Milliseconds(2500);

}  // namespace

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
    SkColor line_color = SkColorSetA(dao::SpaceActive(), 200);

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

// WebView subclass that rejects native drag-drop so events propagate
// up to DaoSidebarView (which handles file/URL drops in both modes).
class DaoNonDropWebView : public views::WebView {
  METADATA_HEADER(DaoNonDropWebView, views::WebView)

 public:
  using WebView::WebView;

  bool GetDropFormats(
      int* formats,
      std::set<ui::ClipboardFormatType>* format_types) override {
    return false;
  }
  bool CanDrop(const ui::OSExchangeData& data) override { return false; }
};

BEGIN_METADATA(DaoNonDropWebView)
END_METADATA

class DaoToggleButton : public views::Button {
  METADATA_HEADER(DaoToggleButton, views::Button)

 public:
  explicit DaoToggleButton(PressedCallback callback)
      : Button(std::move(callback)) {
    SetPreferredSize(gfx::Size(32, 32));
    SetInstallFocusRingOnFocus(false);
    SetTooltipText(l10n_util::GetStringUTF16(IDS_DAO_SIDEBAR_TOGGLE_TOOLTIP));
    SetAccessibleName(
        l10n_util::GetStringUTF16(IDS_DAO_SIDEBAR_TOGGLE_ACCESSIBLE_NAME));
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    constexpr float kHoverSize = 24.0f;
    constexpr float kHoverRadius = 6.0f;
    constexpr float kIconSize = 16.0f;
    if (hovered_) {
      float hx = (width() - kHoverSize) / 2.0f;
      float hy = (height() - kHoverSize) / 2.0f;
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(dao::ControlCenterHoverBg());
      canvas->DrawRoundRect(gfx::RectF(hx, hy, kHoverSize, kHoverSize),
                            kHoverRadius, flags);
    }
    float x = (width() - kIconSize) / 2.0f;
    float y = (height() - kIconSize) / 2.0f;
    DrawLucideIcon(canvas, LucideIcon::kPanelLeftClose,
                   gfx::RectF(x, y, kIconSize, kIconSize),
                   dao::TextSecondary());
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    hovered_ = true;
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    hovered_ = false;
    SchedulePaint();
  }

 private:
  bool hovered_ = false;
};

BEGIN_METADATA(DaoToggleButton)
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
  inner_container_->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::TLBR(0, 4, 0, 4), 2));

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
  auto toggle_btn = std::make_unique<DaoToggleButton>(
      base::BindRepeating(&DaoSidebarView::ToggleCollapsed,
                          base::Unretained(this)));
  toggle_button_ = header_row->AddChildView(std::move(toggle_btn));

  header_row_ = inner_container_->AddChildView(std::move(header_row));

  // WebUI sidebar content is loaded lazily in AddedToWidget().

  // Resize handle on the right edge
  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));

  // Drop overlay — has its own layer so it paints above all sidebar content.
  drop_overlay_ = AddChildView(std::make_unique<DaoDropOverlayView>());

  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}

void DaoSidebarView::ApplyTheme() {
  if (inner_container_) {
    inner_container_->SetBackground(
        views::CreateSolidBackground(dao::SidebarBackground()));
  }
}

void DaoSidebarView::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaintRecursive(this);
}

// static
void DaoSidebarView::SchedulePaintRecursive(views::View* view) {
  view->SchedulePaint();
  for (views::View* child : view->children()) {
    SchedulePaintRecursive(child);
  }
}

DaoSidebarView::~DaoSidebarView() {
  if (sidebar_web_view_ && sidebar_web_view_->GetWebContents()) {
    sidebar_web_view_->GetWebContents()->SetDelegate(nullptr);
  }
}

void DaoSidebarView::EnsureWebUILoaded() {
  if (webui_loaded_ || !sidebar_web_view_) {
    return;
  }
  webui_loaded_ = true;
  sidebar_web_view_->LoadInitialURL(
      GURL(std::string(content::kChromeUIScheme) + "://sidebar"));

  if (sidebar_web_view_->GetWebContents()) {
    sidebar_web_view_->GetWebContents()->SetDelegate(this);

    // Pass Browser* to the WebUI handler after load.
    content::WebUI* webui =
        sidebar_web_view_->GetWebContents()->GetWebUI();
    if (webui) {
      auto* controller = static_cast<DaoSidebarUI*>(webui->GetController());
      if (controller) {
        controller->SetBrowser(browser_);
      }
    }
  }
}

bool DaoSidebarView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

void DaoSidebarView::TrackCommandSShortcutSentToWebContents() {
  if (command_s_toggle_confirmation_pending_) {
    return;
  }

  command_s_intercept_toast_timer_.Start(
      FROM_HERE, kCommandSInterceptToastDelay,
      base::BindOnce(&DaoSidebarView::ShowCommandSShortcutInterceptedToast,
                     weak_factory_.GetWeakPtr()));
}

bool DaoSidebarView::MaybeHandleConfirmedCommandSShortcut() {
  if (!command_s_toggle_confirmation_pending_) {
    return false;
  }

  DidHandleCommandSShortcutInBrowser();
  ToggleCollapsed();
  return true;
}

void DaoSidebarView::DidHandleCommandSShortcutInBrowser() {
  ClearCommandSShortcutConfirmation();
}

void DaoSidebarView::ShowCommandSShortcutInterceptedToast() {
  command_s_toggle_confirmation_pending_ = true;

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_toast()) {
    browser_view->dao_toast()->ShowToast(
        l10n_util::GetStringUTF16(IDS_DAO_SIDEBAR_TOGGLE_RETRY_TOAST));
    browser_view->InvalidateLayout();
  }

  command_s_confirmation_timer_.Start(
      FROM_HERE, kCommandSConfirmationDuration,
      base::BindOnce(&DaoSidebarView::ClearCommandSShortcutConfirmation,
                     weak_factory_.GetWeakPtr()));
}

void DaoSidebarView::ClearCommandSShortcutConfirmation() {
  command_s_intercept_toast_timer_.Stop();
  command_s_confirmation_timer_.Stop();
  command_s_toggle_confirmation_pending_ = false;
}

bool DaoSidebarView::PreHandleGestureEvent(
    content::WebContents* source,
    const blink::WebGestureEvent& event) {
  if (blink::WebInputEvent::IsPinchGestureEventType(event.GetType())) {
    return true;
  }
  return false;
}

bool DaoSidebarView::CanDragEnter(
    content::WebContents* source,
    const content::DropData& data,
    blink::DragOperationsMask operations_allowed) {
  // Allow the renderer to handle drags. Actual file/URL drops are
  // intercepted in OpenURLFromTab() and redirected to new tabs.
  return true;
}

content::WebContents* DaoSidebarView::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  // When a file or URL is dropped on the sidebar WebView, the renderer
  // tries to navigate chrome://sidebar to that URL. Intercept and open
  // in a new browser tab instead.
  if (params.url.SchemeIsFile() || params.url.SchemeIsHTTPOrHTTPS()) {
    NavigateParams nav_params(browser_, params.url, ui::PAGE_TRANSITION_TYPED);
    nav_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    if (webui_drop_insert_index_ >= 0) {
      nav_params.tabstrip_index = webui_drop_insert_index_;
    }
    webui_drop_insert_index_ = -1;
    Navigate(&nav_params);
    return nullptr;
  }
  // Allow chrome:// navigations (unlikely but safe).
  return source;
}

content::WebContents* DaoSidebarView::sidebar_web_contents() const {
  return sidebar_web_view_ ? sidebar_web_view_->GetWebContents() : nullptr;
}

Browser* GetBrowserForSidebarWebContents(content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  Profile* profile =
      Profile::FromBrowserContext(web_contents->GetBrowserContext());
  for (Browser* b : chrome::FindAllBrowsersWithProfile(profile)) {
    BrowserView* bv = BrowserView::GetBrowserViewForBrowser(b);
    if (bv && bv->dao_sidebar() &&
        bv->dao_sidebar()->sidebar_web_contents() == web_contents) {
      return b;
    }
  }
  return nullptr;
}

void DaoSidebarView::StartFileDrag(const base::FilePath& path) {
  // Post to the message loop so chrome.send() returns first.
  // RunShellDrag blocks (enters a nested run loop on macOS) and would
  // otherwise freeze the WebUI if called synchronously from a message handler.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoSidebarView::DoStartFileDrag,
                     weak_factory_.GetWeakPtr(), path));
}

void DaoSidebarView::DoStartFileDrag(const base::FilePath& path) {
  auto* widget = GetWidget();
  if (!widget) return;

  auto data = std::make_unique<ui::OSExchangeData>();
  data->SetFilename(path);

  // Use the file's system icon as the drag image.
  gfx::ImageSkia drag_image = GetFileIcon(path, 32);
  if (!drag_image.isNull()) {
    data->provider().SetDragImage(drag_image,
                                  gfx::Vector2d(drag_image.width() / 2,
                                                drag_image.height() / 2));
  }

  views::View* source_view =
      sidebar_web_view_ ? static_cast<views::View*>(sidebar_web_view_.get())
                        : static_cast<views::View*>(this);
  widget->RunShellDrag(source_view, std::move(data), gfx::Point(),
                       ui::DragDropTypes::DRAG_COPY,
                       ui::mojom::DragEventSource::kMouse);
}

gfx::Rect DaoSidebarView::header_bounds_in_sidebar() const {
  // When collapsed, inner_container_ still holds user_width_ for animation
  // clipping, so header_row_->bounds() reports the full pre-collapse width.
  // Returning that would make NonClientHitTest mark the address bar area as
  // a window drag region and swallow clicks on the address-bar toggle.
  if (!header_row_ || !inner_container_ || collapsed_) {
    return gfx::Rect();
  }
  gfx::Rect r = header_row_->bounds();
  r.Offset(inner_container_->bounds().origin().OffsetFromOrigin());
  return r;
}

gfx::Rect DaoSidebarView::toggle_button_bounds_in_sidebar() const {
  if (!toggle_button_ || !toggle_button_->GetVisible() || !header_row_ ||
      !inner_container_) {
    return gfx::Rect();
  }
  gfx::Rect r = toggle_button_->bounds();
  r.Offset(header_row_->bounds().origin().OffsetFromOrigin());
  r.Offset(inner_container_->bounds().origin().OffsetFromOrigin());
  return r;
}

gfx::Size DaoSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  if (collapsed_ && GetWidget() && GetWidget()->IsFullscreen()) {
    return gfx::Size(0, 0);
  }
  return gfx::Size(current_width_, 0);
}

void DaoSidebarView::Layout(PassKey) {
  // Reposition traffic lights on every layout pass so macOS cannot reset them.
  if (GetWidget()) {
    constexpr int kTrafficLightX = 13;
    constexpr int kTrafficLightY = 6;
    dao::SetTrafficLightsPosition(GetWidget()->GetNativeWindow(),
                                   kTrafficLightX, kTrafficLightY);
  }

  // Dao: Hide traffic-light spacing in fullscreen (no window controls).
  if (header_row_) {
    bool fullscreen = GetWidget() && GetWidget()->IsFullscreen();
    auto* header_layout = static_cast<views::FlexLayout*>(
        header_row_->GetLayoutManager());
    int left_inset = fullscreen ? 0 : 70;
    header_layout->SetInteriorMargin(gfx::Insets::TLBR(0, left_inset, 0, 0));
  }
  if (toggle_button_) {
    toggle_button_->SetVisible(!collapsed_);
  }
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
  ClearCommandSShortcutConfirmation();
  auto_expanded_ = false;
  collapsed_ = !collapsed_;

  if (!collapsed_) {
    EnsureWebUILoaded();
  }

  int old_width = current_width_;
  int new_width = collapsed_ ? kCollapsedWidth : user_width_;
  current_width_ = new_width;
  target_width_ = new_width;

  // Stop any in-flight LinearAnimation (used only for resize).
  collapse_animation_.Stop();

  // Commit final layout immediately (one layout pass).
  PreferredSizeChanged();

  // Animate via compositor layers.
  AnimateLayerSlide(old_width, new_width);
}

void DaoSidebarView::AnimateLayerSlide(int old_width, int new_width) {
  int delta = old_width - new_width;
  if (delta == 0) {
    return;
  }

  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!bv) {
    return;
  }

  // Collect layers to the right of sidebar: address bar, corner overlay,
  // contents container. These all need to slide together.
  std::vector<ui::Layer*> slide_layers;
  if (bv->dao_address_bar() && bv->dao_address_bar()->layer()) {
    slide_layers.push_back(bv->dao_address_bar()->layer());
  }
  if (bv->dao_corner_overlay() && bv->dao_corner_overlay()->layer()) {
    slide_layers.push_back(bv->dao_corner_overlay()->layer());
  }
  if (bv->contents_container() && bv->contents_container()->layer()) {
    slide_layers.push_back(bv->contents_container()->layer());
  }

  // Snap all layers to old position (undo the layout jump).
  gfx::Transform start_transform;
  start_transform.Translate(delta, 0);
  for (auto* l : slide_layers) {
    l->SetTransform(start_transform);
  }
  if (layer()) {
    layer()->SetTransform(start_transform);
  }

  // Animate all layers to final position (identity transform).
  // GPU compositor drives this — zero per-frame relayout.
  constexpr auto kDuration = base::Milliseconds(150);
  {
    ui::ScopedLayerAnimationSettings settings(layer()->GetAnimator());
    settings.SetTransitionDuration(kDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    settings.AddObserver(this);
    layer()->SetTransform(gfx::Transform());
  }
  for (auto* l : slide_layers) {
    ui::ScopedLayerAnimationSettings settings(l->GetAnimator());
    settings.SetTransitionDuration(kDuration);
    settings.SetTweenType(gfx::Tween::EASE_OUT);
    l->SetTransform(gfx::Transform());
  }
}

void DaoSidebarView::OnImplicitAnimationsCompleted() {
  // Notify address bar about sidebar collapse state after animation.
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (bv && bv->dao_address_bar()) {
    bv->dao_address_bar()->SetSidebarCollapsed(collapsed_);
  }

  // After the layer slide animation, explicitly reset all transforms to
  // identity and force a synchronous re-layout.  On macOS the WebView's
  // native NSView frame is determined by NativeViewHost::Layout() which
  // reads ConvertRectToWidget() — if a residual layer transform exists
  // (even identity with float rounding) the NSView frame can become stale,
  // breaking hit-testing.  DeprecatedLayoutImmediately() forces
  // NativeViewHost to call [native_view_ setFrame:] synchronously.
  if (layer()) {
    layer()->SetTransform(gfx::Transform());
  }
  if (bv) {
    if (bv->dao_address_bar() && bv->dao_address_bar()->layer()) {
      bv->dao_address_bar()->layer()->SetTransform(gfx::Transform());
    }
    if (bv->dao_corner_overlay() && bv->dao_corner_overlay()->layer()) {
      bv->dao_corner_overlay()->layer()->SetTransform(gfx::Transform());
    }
    if (bv->contents_container() && bv->contents_container()->layer()) {
      bv->contents_container()->layer()->SetTransform(gfx::Transform());
    }
    bv->InvalidateLayout();
    bv->DeprecatedLayoutImmediately();
    if (bv->contents_container()) {
      bv->contents_container()->InvalidateLayout();
      bv->contents_container()->DeprecatedLayoutImmediately();
    }
    if (bv->contents_web_view()) {
      bv->contents_web_view()->InvalidateLayout();
      bv->contents_web_view()->DeprecatedLayoutImmediately();
    }
    if (bv->dao_split_view()) {
      bv->dao_split_view()->InvalidateLayout();
      bv->dao_split_view()->DeprecatedLayoutImmediately();
    }
  }
  if (sidebar_web_view_) {
    sidebar_web_view_->InvalidateLayout();
    sidebar_web_view_->DeprecatedLayoutImmediately();
  }
}

void DaoSidebarView::AnimationProgressed(const gfx::Animation* animation) {
  // Only used for live resize, not collapse/expand.
  double t = gfx::Tween::CalculateValue(gfx::Tween::EASE_IN_OUT,
                                         animation->GetCurrentValue());
  current_width_ =
      start_width_ + static_cast<int>((target_width_ - start_width_) * t);
  PreferredSizeChanged();
}

void DaoSidebarView::AnimationEnded(const gfx::Animation* animation) {
  // Only used for live resize, not collapse/expand.
  current_width_ = target_width_;
  PreferredSizeChanged();
}

// --- Keyboard shortcut (Cmd+\) -------------------------------------------

void DaoSidebarView::AddedToWidget() {
  View::AddedToWidget();

  // Create WebView lazily here (not in constructor) to avoid creating
  // WebContents too early during BrowserView construction.
  if (!sidebar_web_view_) {
    sidebar_web_view_ = inner_container_->AddChildView(
        std::make_unique<DaoNonDropWebView>(browser_->profile()));
    auto* layout = static_cast<views::BoxLayout*>(
        inner_container_->GetLayoutManager());
    layout->SetFlexForView(sidebar_web_view_, 1);
  }

  if (!collapsed_) {
    EnsureWebUILoaded();
  }

  // Wire toggle callback to address bar (deferred to here because address
  // bar is created after sidebar during BrowserView construction).
  BrowserView* bv = BrowserView::GetBrowserViewForBrowser(browser_);
  if (bv && bv->dao_address_bar()) {
    bv->dao_address_bar()->set_toggle_callback(
        base::BindRepeating(&DaoSidebarView::ToggleCollapsed,
                            base::Unretained(this)));
  }
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
    GetFocusManager()->RegisterAccelerator(
        ui::Accelerator(ui::VKEY_E, ui::EF_COMMAND_DOWN),
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
    GetFocusManager()->UnregisterAccelerator(
        ui::Accelerator(ui::VKEY_E, ui::EF_COMMAND_DOWN), this);
  }
  View::RemovedFromWidget();
}

bool DaoSidebarView::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  if (accelerator.key_code() == ui::VKEY_D) {
    DuplicateActiveTab(browser_);
    return true;
  }
  if (accelerator.key_code() == ui::VKEY_E) {
    // Cmd+E: toggle right agent sidebar
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->dao_agent_sidebar()) {
      bool expanded = browser_view->dao_agent_sidebar()->Toggle();
      browser_view->InvalidateLayout();
      if (browser_view->dao_address_bar()) {
        browser_view->dao_address_bar()->SetChatButtonHighlighted(expanded);
      }
    }
    return true;
  }
  if (accelerator.key_code() == ui::VKEY_S) {
    DidHandleCommandSShortcutInBrowser();
    ToggleCollapsed();
    return true;
  }

  // Cmd+\: toggle sidebar
  ToggleCollapsed();
  return true;
}

// --- Edge hover auto-expand ----------------------------------------------

void DaoSidebarView::OnMouseEntered(const ui::MouseEvent& event) {
  if (collapsed_ && !layer()->GetAnimator()->is_animating()) {
    auto_expanded_ = true;
    collapsed_ = false;
    int old_width = current_width_;
    current_width_ = user_width_;
    target_width_ = user_width_;
    collapse_animation_.Stop();
    PreferredSizeChanged();
    AnimateLayerSlide(old_width, user_width_);
  }
}

void DaoSidebarView::OnMouseExited(const ui::MouseEvent& event) {
  NotifySidebarPointerExited();

  if (auto_expanded_) {
    auto_expanded_ = false;
    collapsed_ = true;
    int old_width = current_width_;
    current_width_ = kCollapsedWidth;
    target_width_ = kCollapsedWidth;
    collapse_animation_.Stop();
    PreferredSizeChanged();
    AnimateLayerSlide(old_width, kCollapsedWidth);
  }
}

void DaoSidebarView::NotifySidebarPointerExited() {
  if (!sidebar_web_view_ || !sidebar_web_view_->GetWebContents()) {
    return;
  }
  content::WebUI* web_ui = sidebar_web_view_->GetWebContents()->GetWebUI();
  if (!web_ui) {
    return;
  }
  web_ui->CallJavascriptFunctionUnsafe("cr.webUIListenerCallback",
                                       base::Value("sidebarPointerExited"));
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

void DaoSidebarView::SetNewTabButtonHighlight(bool highlighted) {
  if (!sidebar_web_view_ || !sidebar_web_view_->GetWebContents()) {
    return;
  }
  content::WebUI* web_ui = sidebar_web_view_->GetWebContents()->GetWebUI();
  if (!web_ui) {
    return;
  }
  web_ui->CallJavascriptFunctionUnsafe("cr.webUIListenerCallback",
                                 base::Value("newTabButtonHighlight"),
                                 base::Value(highlighted));
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
  if (text.has_value() && text->starts_with(u"dao-tab-drag")) {
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
  if (collapsed_ && !layer()->GetAnimator()->is_animating()) {
    drop_auto_expanded_ = true;
    collapsed_ = false;
    int old_width = current_width_;
    current_width_ = user_width_;
    target_width_ = user_width_;
    collapse_animation_.Stop();
    PreferredSizeChanged();
    AnimateLayerSlide(old_width, user_width_);
  }
}

int DaoSidebarView::OnDragUpdated(const ui::DropTargetEvent& event) {
  // Drop position is computed by WebUI JS (setDropInsertIndex message).
  // C++ just accepts the drop; the insert index comes from webui_drop_insert_index_.
  drop_target_index_ = webui_drop_insert_index_;
  return ui::DragDropTypes::DRAG_COPY;
}

void DaoSidebarView::OnDragExited() {
  is_drop_target_active_ = false;
  drop_target_index_ = -1;
  auto* overlay = static_cast<DaoDropOverlayView*>(drop_overlay_.get());
  overlay->SetActive(false);
  overlay->SetIndicatorY(-1);
  if (drop_auto_expanded_) {
    drop_auto_expanded_ = false;
    collapsed_ = true;
    int old_width = current_width_;
    current_width_ = kCollapsedWidth;
    target_width_ = kCollapsedWidth;
    collapse_animation_.Stop();
    PreferredSizeChanged();
    AnimateLayerSlide(old_width, kCollapsedWidth);
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
        for (const auto& info :
             data.GetURLs(ui::FilenameToURLPolicy::CONVERT_FILENAMES)) {
          if (info.url.is_valid()) {
            urls.push_back(info.url);
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

}  // namespace dao
