// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_utility_section.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_specification.h"
#include "chrome/browser/ui/views/page_info/page_info_bubble_view.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "dao/browser/ui/views/little_dao/dao_little_dao_controller.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Wide status tile: round icon badge followed by a title and status line.
constexpr int kStatusTileHeight = 52;
constexpr int kStatusTileInset = 10;
constexpr int kStatusBadgeSize = 32;
constexpr int kStatusIconSize = 16;
constexpr int kStatusTextGap = 10;
constexpr int kStatusTextSpacing = 1;

// Compact action tile: icon above a short label.
constexpr int kActionTileHeight = 56;
constexpr int kActionTileTopInset = 10;
constexpr int kActionTileInsetH = 4;
constexpr int kActionIconSize = 18;
constexpr int kActionLabelGap = 6;

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

std::unique_ptr<views::View> CreateTileRow() {
  auto row = std::make_unique<views::View>();
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
      DaoControlCenterPopup::kTileSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  // Tiles report a zero preferred width, so equal flex splits the row evenly.
  layout->SetDefaultFlex(1);
  return row;
}

// Base tile: paints the rounded tile surface with a hover tint on top.
class TileButton : public views::Button {
  METADATA_HEADER(TileButton, views::Button)

 public:
  explicit TileButton(views::Button::PressedCallback callback)
      : Button(std::move(callback)) {
    SetInstallFocusRingOnFocus(false);
  }

  // Re-applies theme colors to the tile's labels.
  virtual void ApplyTheme() {}

  void SetVisualEnabled(bool enabled) {
    visually_enabled_ = enabled;
    if (!enabled) {
      SetBackground(nullptr);
    }
    SchedulePaint();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    if (visually_enabled_) {
      SetBackground(views::CreateRoundedRectBackground(
          SuggestionHover(), DaoControlCenterPopup::kTileCornerRadius));
      SchedulePaint();
    }
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void OnPaintBackground(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(ControlCenterTileBackground());
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()),
                          DaoControlCenterPopup::kTileCornerRadius, flags);
    // The hover tint, when set as the background, paints over the surface.
    Button::OnPaintBackground(canvas);
  }

 protected:
  bool visually_enabled() const { return visually_enabled_; }

 private:
  bool visually_enabled_ = true;
};

BEGIN_METADATA(TileButton)
END_METADATA

// A wide tile with a round icon badge, a title and a status line. When
// active, the badge switches to the accent color.
class StatusTile : public TileButton {
  METADATA_HEADER(StatusTile, TileButton)

 public:
  StatusTile(const std::u16string& title,
             LucideIcon icon,
             views::Button::PressedCallback callback)
      : TileButton(std::move(callback)), icon_(icon) {
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

  void SetIcon(LucideIcon icon) {
    if (icon_ == icon) {
      return;
    }
    icon_ = icon;
    SchedulePaint();
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

// A compact tile with an icon above a short label.
class ActionTile : public TileButton {
  METADATA_HEADER(ActionTile, TileButton)

 public:
  ActionTile(const std::u16string& label_text,
             LucideIcon icon,
             views::Button::PressedCallback callback)
      : TileButton(std::move(callback)), icon_(icon) {
    label_ = AddChildView(CreateTileLabel(label_text, kLabelFontSize,
                                          gfx::Font::Weight::NORMAL));
    label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);

    SetPreferredSize(gfx::Size(0, kActionTileHeight));
    SetAccessibleName(label_text);
    ApplyTheme();
  }

  void ApplyTheme() override {
    label_->SetEnabledColor(ControlCenterTileLabelColor());
  }

  void Layout(PassKey) override {
    label_->SetBounds(kActionTileInsetH,
                      kActionTileTopInset + kActionIconSize + kActionLabelGap,
                      std::max(0, width() - 2 * kActionTileInsetH),
                      label_->font_list().GetHeight());
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    const gfx::RectF icon_rect((width() - kActionIconSize) / 2.0f,
                               kActionTileTopInset, kActionIconSize,
                               kActionIconSize);
    DrawLucideIcon(canvas, icon_, icon_rect, ControlCenterTileIconColor());
  }

 private:
  LucideIcon icon_;
  raw_ptr<views::Label> label_ = nullptr;
};

BEGIN_METADATA(ActionTile)
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

  // Wide status tiles: dark mode and security.
  views::View* status_row = AddChildView(CreateTileRow());
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
  security_button_ = status_row->AddChildView(std::make_unique<StatusTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_SECURITY),
      LucideIcon::kShieldCheck,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnLockClicked,
                          base::Unretained(this))));

  // Compact action tiles: QR code, Mini Dao, more.
  views::View* action_row = AddChildView(CreateTileRow());
  action_row->AddChildView(std::make_unique<ActionTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_QR_CODE),
      LucideIcon::kQrCode,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnQrClicked,
                          base::Unretained(this))));
  action_row->AddChildView(std::make_unique<ActionTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MINI_DAO),
      LucideIcon::kExternalLink,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnMiniDaoClicked,
                          base::Unretained(this))));
  action_row->AddChildView(std::make_unique<ActionTile>(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MORE),
      LucideIcon::kEllipsis,
      base::BindRepeating(&DaoControlCenterUtilitySection::OnMoreClicked,
                          base::Unretained(this))));

  Refresh();
}

DaoControlCenterUtilitySection::~DaoControlCenterUtilitySection() = default;

void DaoControlCenterUtilitySection::Refresh() {
  for (views::View* row : children()) {
    for (views::View* child : row->children()) {
      if (auto* tile = views::AsViewClass<TileButton>(child)) {
        tile->ApplyTheme();
      }
    }
  }
  RefreshForceDarkModeState();
  RefreshSecurityState();
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
      on ? IDS_DAO_FORCE_DARK_MODE_STATUS_ON
         : IDS_DAO_FORCE_DARK_MODE_STATUS_OFF));
  tile->GetViewAccessibility().SetCheckedState(
      on ? ax::mojom::CheckedState::kTrue : ax::mojom::CheckedState::kFalse);
  tile->SetTooltipText(l10n_util::GetStringUTF16(
      available ? IDS_DAO_FORCE_DARK_MODE_TOOLTIP
                : IDS_DAO_FORCE_DARK_MODE_DISABLED_TOOLTIP));
}

void DaoControlCenterUtilitySection::RefreshSecurityState() {
  if (!security_button_) {
    return;
  }

  auto* tile = static_cast<StatusTile*>(security_button_.get());
  content::WebContents* web_contents =
      popup_ && popup_->browser()
          ? popup_->browser()->tab_strip_model()->GetActiveWebContents()
          : nullptr;
  const security_state::SecurityLevel level =
      web_contents ? chrome_security_state::GetSecurityLevel(web_contents)
                   : security_state::NONE;

  int status_id = IDS_DAO_CONTROL_CENTER_SECURITY_SITE_INFO;
  LucideIcon icon = LucideIcon::kShieldCheck;
  switch (level) {
    case security_state::SECURE:
      status_id = IDS_DAO_CONTROL_CENTER_SECURITY_SECURE;
      break;
    case security_state::WARNING:
    case security_state::DANGEROUS:
      status_id = IDS_DAO_CONTROL_CENTER_SECURITY_NOT_SECURE;
      icon = LucideIcon::kShieldOff;
      break;
    default:
      break;
  }
  tile->SetIcon(icon);
  tile->SetStatus(l10n_util::GetStringUTF16(status_id));
}

void DaoControlCenterUtilitySection::OnLockClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }
  BrowserWindowInterface* browser = popup_->browser();
  auto* web_contents = browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }

  // The default ShowPageInfoDialog() path tries to anchor the bubble to the
  // location bar / app menu button. Both live inside Chrome's top toolbar,
  // which Dao Browser pushes off-screen (y = -toolbar_height) when the
  // sidebar is active, so they fail IsDrawn() and Chrome falls back to
  // GetPageInfoAnchorRect(), which places the bubble at the top-left of the
  // BrowserView (offset 40px). That's the "appears on the left side" the
  // user reports.
  //
  // The Page Info bubble logically belongs to the Security button the user
  // just clicked. Build the spec ourselves so we can pin the anchor rect to
  // that button's screen rect, exactly like the Share button does for the
  // macOS native share picker.
  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (!browser_view) {
    return;
  }

  // Resolve the virtual URL the same way ShowPageInfoDialog() does so the
  // bubble shows the right page info content.
  content::NavigationEntry* entry =
      web_contents->GetController().GetVisibleEntry();
  if (!entry) {
    return;
  }
  const GURL virtual_url = entry->GetVirtualURL();

  // The Page Info bubble should appear pinned to the upper-right of the
  // browser window — that is where the control center popup is anchored
  // from, and the popup itself is closed below before the bubble shows. We
  // build a 1px tall rect that hugs the top edge of the BrowserView at its
  // right side: BubbleBorder::TOP_RIGHT then puts the bubble directly below
  // it with the bubble's right edge flush with the browser's right edge.
  //
  // Capture the anchor BEFORE hiding the popup so the BrowserView bounds we
  // read are stable (Hide() may trigger relayout/teardown).
  gfx::Rect browser_bounds = browser_view->GetBoundsInScreen();
  gfx::Rect anchor_rect(browser_bounds.right(), browser_bounds.y(), 0, 0);

  // Hide the control center now; once the bubble shows it owns focus and
  // the popup would only get in the way visually.
  popup_->Hide();

  PageInfoBubbleSpecification::Builder builder(
      views::BubbleAnchor(), browser->GetWindow()->GetNativeWindow(),
      web_contents, virtual_url);
  builder.AddAnchorRect(anchor_rect)
      .AddInitializedCallback(base::DoNothing())
      .AddPageInfoClosingCallback(
          base::BindOnce([](views::Widget::ClosedReason, bool) {}));

  views::BubbleDialogDelegateView* const bubble =
      PageInfoBubbleView::CreatePageInfoBubble(builder.Build());
  // TOP_RIGHT means: the bubble's TOP-RIGHT corner is the anchor point, so
  // the bubble drops down-and-to-the-left from the browser window's
  // upper-right corner — exactly where the user clicked from.
  bubble->SetArrow(views::BubbleBorder::TOP_RIGHT);
  bubble->GetWidget()->Show();
}

void DaoControlCenterUtilitySection::OnMoreClicked() {
  if (popup_) {
    popup_->ShowMoreMenu();
  }
}

}  // namespace dao
