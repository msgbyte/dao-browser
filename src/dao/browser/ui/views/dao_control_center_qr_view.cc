// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_qr_view.h"

#include "base/strings/utf_string_conversions.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/favicon/content/content_favicon_driver.h"
#include "components/qr_code_generator/qr_code_generator.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_qr_code_image.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/models/image_model.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
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

class QrCopyImageButton : public views::ImageButton {
  METADATA_HEADER(QrCopyImageButton, views::ImageButton)

 public:
  explicit QrCopyImageButton(PressedCallback callback)
      : ImageButton(std::move(callback)) {
    SetInstallFocusRingOnFocus(false);
    SetFocusBehavior(FocusBehavior::ALWAYS);
    SetFlipCanvasOnPaintForRTLUI(false);
    SetImageHorizontalAlignment(ALIGN_CENTER);
    SetImageVerticalAlignment(ALIGN_MIDDLE);
    SetMinimumImageSize(gfx::Size(kQrSize, kQrSize));
    SetBorder(views::CreateEmptyBorder(gfx::Insets(kQrImageCardInset)));
    SetCopied(false);
  }

  void SetCopied(bool copied) {
    copied_ = copied;
    const auto text = l10n_util::GetStringUTF16(
        copied ? IDS_DAO_CONTROL_CENTER_QR_IMAGE_COPIED
               : IDS_DAO_CONTROL_CENTER_QR_COPY_IMAGE);
    SetAccessibleName(text);
    SetTooltipText(text);
    SchedulePaint();
  }

 protected:
  void PaintButtonContents(gfx::Canvas* canvas) override {
    // The card fill must survive the popup's button-hover background reset.
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SK_ColorWHITE);
    canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), kQrCornerRadius, flags);
    ImageButton::PaintButtonContents(canvas);
    if (!GetEnabled() ||
        (GetState() != STATE_HOVERED && GetState() != STATE_PRESSED &&
         !HasFocus())) {
      return;
    }

    const gfx::RectF badge(width() - 28, 4, 24, 24);
    canvas->DrawRoundRect(badge, 6, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(SkColorSetARGB(24, 0, 0, 0));
    canvas->DrawRoundRect(badge, 6, flags);
    DrawLucideIcon(canvas, copied_ ? LucideIcon::kCheck : LucideIcon::kCopy,
                   gfx::RectF(badge.x() + 5, badge.y() + 5, 14, 14),
                   SkColorSetRGB(100, 100, 100));
  }

  void OnFocus() override {
    ImageButton::OnFocus();
    SchedulePaint();
  }

  void OnBlur() override {
    ImageButton::OnBlur();
    SchedulePaint();
  }

 private:
  bool copied_ = false;
};

BEGIN_METADATA(QrCopyImageButton)
END_METADATA

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

  copy_button_ = qr_row->AddChildView(std::make_unique<QrCopyImageButton>(
      base::BindRepeating(&DaoControlCenterQrView::OnCopyImageClicked,
                         base::Unretained(this))));
  copy_button_->SetEnabled(false);
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
  copy_button_->SetImageModel(views::Button::STATE_NORMAL, ui::ImageModel());
  url_label_->SetText(std::u16string());
  copy_button_->SetCopied(false);
  copy_button_->SetEnabled(false);

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
    copy_button_->SetImageModel(views::Button::STATE_NORMAL,
                                ui::ImageModel::FromImageSkia(qr_image));
    copy_button_->SetEnabled(!qr_image.isNull());
  }
}

void DaoControlCenterQrView::OnCopyImageClicked() {
  const gfx::ImageSkia image =
      copy_button_->GetImage(views::Button::STATE_NORMAL);
  if (image.isNull()) {
    return;
  }
  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteImage(image.GetRepresentation(1.0f).GetBitmap());
  }
  copy_button_->SetCopied(true);
}

void DaoControlCenterQrView::OnBackClicked() {
  if (popup_) {
    popup_->ShowMainPanel();
  }
}

}  // namespace dao
