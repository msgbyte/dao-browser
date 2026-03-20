// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_utility_section.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/bubble_anchor_util.h"
#include "chrome/browser/ui/page_info/page_info_dialog.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

#if BUILDFLAG(IS_MAC)
#include "dao/browser/ui/views/dao_native_share_mac.h"
#endif

namespace dao {

namespace {

constexpr int kUtilButtonSize = 56;
constexpr int kUtilIconSize = 18;
constexpr int kUtilCornerRadius = 10;
constexpr int kUtilFontSize = 10;
constexpr SkColor kIconColor = SkColorSetRGB(55, 55, 60);

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

    auto* label =
        AddChildView(std::make_unique<views::Label>(label_text));
    label->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL,
                                      kUtilFontSize,
                                      gfx::Font::Weight::NORMAL));
    label->SetEnabledColor(SkColorSetRGB(100, 100, 100));
    label->SetCanProcessEventsWithinSubtree(false);

    SetPreferredSize(gfx::Size(kUtilButtonSize, kUtilButtonSize));
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(label_text);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(15, 0, 0, 0), kUtilCornerRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    // Draw the icon centered in the spacer area (first child).
    if (!children().empty()) {
      gfx::Rect icon_bounds = children().front()->bounds();
      DrawLucideIcon(canvas, icon_,
                     gfx::RectF(icon_bounds.x(), icon_bounds.y(),
                                kUtilIconSize, kUtilIconSize),
                     kIconColor);
    }
  }

 private:
  LucideIcon icon_;
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
          u"Share", LucideIcon::kShare,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnShareClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"QR Code", LucideIcon::kQrCode,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnQrClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"Security", LucideIcon::kShieldCheck,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnLockClicked,
              base::Unretained(this)))
          .release()));

  AddChildView(static_cast<views::View*>(
      std::make_unique<UtilityButton>(
          u"More", LucideIcon::kEllipsis,
          base::BindRepeating(
              &DaoControlCenterUtilitySection::OnMoreClicked,
              base::Unretained(this)))
          .release()));
}

DaoControlCenterUtilitySection::~DaoControlCenterUtilitySection() = default;

void DaoControlCenterUtilitySection::OnShareClicked() {
#if BUILDFLAG(IS_MAC)
  if (!popup_ || !popup_->browser()) {
    return;
  }
  auto* web_contents =
      popup_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  std::string url = web_contents->GetVisibleURL().spec();
  std::string title = web_contents->GetTitle().empty()
                          ? url
                          : base::UTF16ToUTF8(web_contents->GetTitle());

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(popup_->browser());
  if (!browser_view || !browser_view->GetWidget()) {
    return;
  }
  gfx::NativeView native_view = browser_view->GetWidget()->GetNativeView();
  gfx::Rect anchor_rect = GetBoundsInScreen();
  dao::ShowNativeShareMac(url, title, native_view, anchor_rect);
#endif
}

void DaoControlCenterUtilitySection::OnQrClicked() {
  if (popup_) {
    popup_->ShowQrView();
  }
}

void DaoControlCenterUtilitySection::OnLockClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }
  auto* web_contents =
      popup_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  ShowPageInfoDialog(web_contents, PageInfoClosingCallback(),
                     bubble_anchor_util::Anchor::kLocationBar);
  if (popup_) {
    popup_->Hide();
  }
}

void DaoControlCenterUtilitySection::OnMoreClicked() {
  if (popup_) {
    popup_->ShowMoreMenu();
  }
}

}  // namespace dao
