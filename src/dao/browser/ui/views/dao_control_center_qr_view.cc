// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_qr_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/qr_code_generator/qr_code_generator.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_qr_code_image.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kQrSize = 240;
constexpr int kQrPadding = 12;
constexpr int kQrCornerRadius = 10;
constexpr int kBackButtonCornerRadius = 8;
constexpr int kBackButtonInsetH = 8;
constexpr int kBackButtonInsetV = 6;
constexpr int kQrCardInsetH = 14;
constexpr int kQrImageCardInset = 6;

// Back button styled like the More-menu items: full-width, left-aligned,
// hover-highlight. Avoids the previous "centered, no feedback" look.
class QrBackButton : public views::LabelButton {
  METADATA_HEADER(QrBackButton, views::LabelButton)

 public:
  QrBackButton(views::Button::PressedCallback callback)
      : LabelButton(std::move(callback),
                    l10n_util::GetStringUTF16(
                        IDS_DAO_CONTROL_CENTER_BACK_BUTTON_LABEL)) {
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(l10n_util::GetStringUTF16(
        IDS_DAO_CONTROL_CENTER_BACK_BUTTON_ACCESSIBLE_NAME));
    SetEnabledTextColors(ControlCenterLabelColor());
    label()->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 13,
                                        gfx::Font::Weight::NORMAL));
    SetBorder(views::CreateEmptyBorder(
        gfx::Insets::VH(kBackButtonInsetV, kBackButtonInsetH)));
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    LabelButton::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        ControlCenterHoverBg(), kBackButtonCornerRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    LabelButton::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }
};

BEGIN_METADATA(QrBackButton)
END_METADATA

gfx::Image GetFaviconForWebContents(content::WebContents* web_contents) {
  if (!web_contents) {
    return gfx::Image();
  }
  auto* favicon_driver =
      favicon::ContentFaviconDriver::FromWebContents(web_contents);
  if (!favicon_driver || !favicon_driver->FaviconIsValid()) {
    return gfx::Image();
  }
  return favicon_driver->GetFavicon();
}

}  // namespace

BEGIN_METADATA(DaoControlCenterQrView)
END_METADATA

DaoControlCenterQrView::DaoControlCenterQrView(
    DaoControlCenterPopup* popup)
    : popup_(popup) {
  // Outer layout: vertical stack that stretches children to full width so the
  // back button can left-align inside the popup card. The QR image and URL
  // are then re-centered inside their own horizontal sub-rows.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(kQrPadding, 0), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Back button \u2014 wrapped in a left-aligned row so the button only occupies
  // its own preferred width. Without this wrapper, the outer kStretch layout
  // would stretch the button across the full popup width and the hover
  // highlight would span the entire row.
  auto back_row = std::make_unique<views::View>();
  auto* back_row_layout =
      back_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kQrCardInsetH), 0));
  back_row_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);
  back_row->AddChildView(std::make_unique<QrBackButton>(base::BindRepeating(
      &DaoControlCenterQrView::OnBackClicked, base::Unretained(this))));
  AddChildView(std::move(back_row));

  // QR code, wrapped in a centered row so it doesn't get stretched by the
  // outer kStretch alignment.
  auto qr_row = std::make_unique<views::View>();
  auto* qr_row_layout =
      qr_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(0, kQrCardInsetH), 0));
  qr_row_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);

  // QR image card \u2014 white background, rounded, with a small inner padding so
  // the QR isn't flush against the rounded corners.
  auto qr_card = std::make_unique<views::View>();
  qr_card->SetBackground(
      views::CreateRoundedRectBackground(SK_ColorWHITE, kQrCornerRadius));
  qr_card->SetBorder(views::CreateEmptyBorder(
      gfx::Insets(kQrImageCardInset)));
  auto* qr_card_layout =
      qr_card->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  qr_card_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  qr_card_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  qr_image_ = qr_card->AddChildView(std::make_unique<views::ImageView>());
  qr_image_->SetPreferredSize(gfx::Size(kQrSize, kQrSize));

  qr_row->AddChildView(std::move(qr_card));
  AddChildView(std::move(qr_row));

  // URL label below QR. Centered, capped to two lines so a long URL doesn't
  // blow up the popup height; the elided tail gets "\u2026".
  url_label_ = AddChildView(std::make_unique<views::Label>());
  url_label_->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 11,
                                         gfx::Font::Weight::NORMAL));
  url_label_->SetEnabledColor(ControlCenterSecondaryTextColor());
  url_label_->SetMultiLine(true);
  url_label_->SetMaxLines(2);
  url_label_->SetMaximumWidth(kQrSize + 2 * kQrCardInsetH);
  url_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  url_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  url_label_->SetBorder(views::CreateEmptyBorder(
      gfx::Insets::VH(0, kQrCardInsetH)));
}

DaoControlCenterQrView::~DaoControlCenterQrView() = default;

void DaoControlCenterQrView::GenerateQrCode() {
  if (!popup_ || !popup_->browser()) {
    return;
  }
  auto* web_contents =
      popup_->browser()->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }

  std::string url = web_contents->GetVisibleURL().spec();
  url_label_->SetText(base::UTF8ToUTF16(url));

  auto result = qr_code_generator::GenerateCode(
      base::as_byte_span(url));
  if (result.has_value()) {
    gfx::ImageSkia qr_image = RenderDaoQrCode(
        result.value(), kQrSize, GetFaviconForWebContents(web_contents));
    qr_image_->SetImage(ui::ImageModel::FromImageSkia(qr_image));
  }
}

void DaoControlCenterQrView::OnBackClicked() {
  if (popup_) {
    popup_->ShowMainPanel();
  }
}

}  // namespace dao
