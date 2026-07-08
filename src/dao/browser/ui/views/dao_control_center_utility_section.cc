// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_utility_section.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
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
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "third_party/skia/include/core/SkMatrix.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/utils/SkParsePath.h"
#include "ui/views/background.h"
#include "ui/views/bubble/bubble_border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace dao {

namespace {

constexpr int kUtilButtonSize = 48;
constexpr int kUtilIconSize = 18;
constexpr int kUtilCornerRadius = 10;
constexpr int kUtilFontSize = 10;

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

// A utility button with a Lucide icon and a text label.
class UtilityButton : public views::Button {
 public:
  UtilityButton(const std::u16string& label_text,
                LucideIcon icon,
                views::Button::PressedCallback callback)
      : Button(std::move(callback)), icon_(icon) {
    auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(6, 0), 2));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    layout->set_main_axis_alignment(
        views::BoxLayout::MainAxisAlignment::kCenter);

    // Spacer for the icon area (painted in PaintButtonContents).
    auto icon_spacer = std::make_unique<views::View>();
    icon_spacer->SetPreferredSize(gfx::Size(kUtilIconSize, kUtilIconSize));
    icon_spacer->SetCanProcessEventsWithinSubtree(false);
    AddChildView(icon_spacer.release());

    label_ = AddChildView(std::make_unique<views::Label>(label_text));
    label_->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL,
                                       kUtilFontSize,
                                       gfx::Font::Weight::NORMAL));
    label_->SetEnabledColor(ControlCenterLabelColor());
    label_->SetCanProcessEventsWithinSubtree(false);

    SetPreferredSize(gfx::Size(kUtilButtonSize, kUtilButtonSize));
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(label_text);
  }

  void SetSelected(bool selected) {
    if (selected_ == selected) {
      return;
    }
    selected_ = selected;
    RefreshBackground();
    SchedulePaint();
  }

  void SetVisualEnabled(bool enabled) {
    SetEnabled(enabled);
    label_->SetEnabled(true);
    label_->SetEnabledColor(ControlCenterLabelColor());
    RefreshBackground();
    SchedulePaint();
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    if (GetEnabled()) {
      hovered_ = true;
      RefreshBackground();
      SchedulePaint();
    }
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    hovered_ = false;
    RefreshBackground();
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    // Draw the icon centered in the spacer area (first child).
    if (!children().empty()) {
      gfx::Rect icon_bounds = children().front()->bounds();
      const gfx::RectF icon_rect(icon_bounds.x(), icon_bounds.y(),
                                 kUtilIconSize, kUtilIconSize);
      const SkColor icon_color =
          selected_ && GetEnabled() ? ControlCenterIconDefault()
                                    : ControlCenterIconMuted();
      if (selected_ && icon_ == LucideIcon::kMoon) {
        DrawFilledMoonIcon(canvas, icon_rect, icon_color);
      } else {
        DrawLucideIcon(canvas, icon_, icon_rect, icon_color);
      }
    }
  }

 private:
  void RefreshBackground() {
    if (!GetEnabled()) {
      SetBackground(nullptr);
      return;
    }
    if (hovered_) {
      SetBackground(views::CreateRoundedRectBackground(
          SuggestionHover(), kUtilCornerRadius));
      return;
    }
    if (selected_) {
      SetBackground(views::CreateRoundedRectBackground(
          ControlCenterActiveBg(), kUtilCornerRadius));
      return;
    }
    SetBackground(nullptr);
  }

  LucideIcon icon_;
  raw_ptr<views::Label> label_ = nullptr;
  bool selected_ = false;
  bool hovered_ = false;
};

}  // namespace

BEGIN_METADATA(DaoControlCenterUtilitySection)
END_METADATA

DaoControlCenterUtilitySection::DaoControlCenterUtilitySection(
    DaoControlCenterPopup* popup)
    : popup_(popup) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(8, 0), 0));
  layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"QR Code", LucideIcon::kQrCode,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnQrClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MINI_DAO),
          LucideIcon::kExternalLink,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnMiniDaoClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"Security", LucideIcon::kShieldCheck,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnLockClicked,
              base::Unretained(this)))
          .release()));

  {
    auto button = std::make_unique<UtilityButton>(
        l10n_util::GetStringUTF16(IDS_DAO_FORCE_DARK_MODE_SHORT_LABEL),
        LucideIcon::kMoon,
        base::BindRepeating(
            &DaoControlCenterUtilitySection::OnForceDarkModeClicked,
            base::Unretained(this)));
    button->SetAccessibleName(
        l10n_util::GetStringUTF16(IDS_DAO_FORCE_DARK_MODE_LABEL));
    force_dark_mode_button_ = button.get();
    AddChildView(static_cast<views::View*>(button.release()));
  }

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"More", LucideIcon::kEllipsis,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnMoreClicked,
              base::Unretained(this)))
          .release()));

  RefreshForceDarkModeState();
}

DaoControlCenterUtilitySection::~DaoControlCenterUtilitySection() = default;

void DaoControlCenterUtilitySection::Refresh() {
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

  Browser* browser = popup_->browser();
  popup_->Hide();

  Browser* little_dao_browser =
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
  if (!popup_ || !popup_->browser() || !IsForceDarkModeAvailable()) {
    RefreshForceDarkModeState();
    return;
  }

  Profile* profile = popup_->browser()->profile();
  SetForceDarkModeUserEnabled(profile, !IsForceDarkModeUserEnabled(profile));
  RefreshForceDarkModeState();
}

void DaoControlCenterUtilitySection::RefreshForceDarkModeState() {
  if (!force_dark_mode_button_) {
    return;
  }

  auto* button = static_cast<UtilityButton*>(force_dark_mode_button_.get());
  Profile* profile =
      popup_ && popup_->browser() ? popup_->browser()->profile() : nullptr;
  const bool available = IsForceDarkModeAvailable();
  button->SetSelected(IsForceDarkModeUserEnabled(profile));
  button->SetVisualEnabled(available);
  button->SetTooltipText(l10n_util::GetStringUTF16(
      available ? IDS_DAO_FORCE_DARK_MODE_TOOLTIP
                : IDS_DAO_FORCE_DARK_MODE_DISABLED_TOOLTIP));
}

void DaoControlCenterUtilitySection::OnLockClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }
  Browser* browser = popup_->browser();
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
      /*anchor=*/nullptr, browser->window()->GetNativeWindow(), web_contents,
      virtual_url);
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
