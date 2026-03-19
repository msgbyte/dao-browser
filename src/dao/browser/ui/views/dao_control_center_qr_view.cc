// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_qr_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/qr_code_generator/qr_code_generator.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kQrSize = 200;
constexpr int kQrPadding = 16;
constexpr int kQrCornerRadius = 8;

// Render QR code data to an ImageSkia.
gfx::ImageSkia RenderQrCode(const qr_code_generator::GeneratedCode& code,
                             int size) {
  const int data_size = code.data.size();
  const int qr_size = code.qr_size;
  if (qr_size <= 0) {
    return gfx::ImageSkia();
  }

  SkBitmap bitmap;
  bitmap.allocN32Pixels(size, size);
  bitmap.eraseColor(SK_ColorWHITE);

  float module_size = static_cast<float>(size) / qr_size;
  for (int y = 0; y < qr_size; ++y) {
    for (int x = 0; x < qr_size; ++x) {
      int idx = y * qr_size + x;
      if (idx < data_size && code.data[idx] != 0) {
        int px = static_cast<int>(x * module_size);
        int py = static_cast<int>(y * module_size);
        int pw = static_cast<int>((x + 1) * module_size) - px;
        int ph = static_cast<int>((y + 1) * module_size) - py;
        for (int dy = 0; dy < ph && (py + dy) < size; ++dy) {
          for (int dx = 0; dx < pw && (px + dx) < size; ++dx) {
            *bitmap.getAddr32(px + dx, py + dy) = SK_ColorBLACK;
          }
        }
      }
    }
  }

  return gfx::ImageSkia::CreateFromBitmap(bitmap, 1.0f);
}
}  // namespace

BEGIN_METADATA(DaoControlCenterQrView)
END_METADATA

DaoControlCenterQrView::DaoControlCenterQrView(
    DaoControlCenterPopup* popup)
    : popup_(popup) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(kQrPadding), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Back button
  auto back_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoControlCenterQrView::OnBackClicked,
                          base::Unretained(this)),
      u"\u2190 Back");
  back_btn->SetInstallFocusRingOnFocus(false);
  back_btn->SetAccessibleName(u"Back");
  back_btn->SetEnabledTextColors(SkColorSetRGB(100, 100, 100));
  auto* back_btn_ptr = AddChildView(std::move(back_btn));
  // Left-align the back button
  back_btn_ptr->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  // QR code image
  qr_image_ = AddChildView(std::make_unique<views::ImageView>());
  qr_image_->SetPreferredSize(gfx::Size(kQrSize, kQrSize));
  qr_image_->SetBackground(
      views::CreateRoundedRectBackground(SK_ColorWHITE, kQrCornerRadius));

  // URL label below QR code
  url_label_ = AddChildView(std::make_unique<views::Label>());
  url_label_->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 11,
                                         gfx::Font::Weight::NORMAL));
  url_label_->SetEnabledColor(SkColorSetRGB(160, 160, 160));
  url_label_->SetMultiLine(true);
  url_label_->SetMaximumWidth(kQrSize);
  url_label_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
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
    gfx::ImageSkia qr_image = RenderQrCode(result.value(), kQrSize);
    qr_image_->SetImage(qr_image);
  }
}

void DaoControlCenterQrView::OnBackClicked() {
  if (popup_) {
    popup_->ShowMainPanel();
  }
}

}  // namespace dao
