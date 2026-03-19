// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_QR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_QR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class ImageView;
class Label;
}

namespace dao {

class DaoControlCenterPopup;

// Displays a QR code of the current page URL.
// Shown when the user clicks the QR button in the utility section.
class DaoControlCenterQrView : public views::View {
  METADATA_HEADER(DaoControlCenterQrView, views::View)

 public:
  explicit DaoControlCenterQrView(DaoControlCenterPopup* popup);
  DaoControlCenterQrView(const DaoControlCenterQrView&) = delete;
  DaoControlCenterQrView& operator=(const DaoControlCenterQrView&) = delete;
  ~DaoControlCenterQrView() override;

  // Generate and display the QR code for the active tab's URL.
  void GenerateQrCode();

 private:
  void OnBackClicked();

  raw_ptr<DaoControlCenterPopup> popup_;
  raw_ptr<views::ImageView> qr_image_ = nullptr;
  raw_ptr<views::Label> url_label_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_QR_VIEW_H_
