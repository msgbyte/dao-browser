// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_RESULT_DIALOG_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_RESULT_DIALOG_VIEW_H_

#include "base/memory/weak_ptr.h"
#include "dao/browser/qrcode/dao_qr_code_types.h"
#include "ui/views/window/dialog_delegate.h"

namespace content {
class WebContents;
}  // namespace content

namespace dao {

// Modal dialog showing the result of a QR-code decode invoked from the image
// context menu. Each decoded entry renders as a row with the payload text and
// action buttons (Copy, plus Open if the payload is a URL).
//
// Inherits views::DialogDelegate (not DialogDelegateView) because the view
// subclass's default constructor is private and not extensible. We hold a
// separate contents View that owns the row layout.
class DaoQrCodeResultDialogView : public views::DialogDelegate {
 public:
  // Builds the dialog and shows it as a tab-modal child of `web_contents`.
  // `results` must not be empty; callers should suppress the dialog and show a
  // toast instead when nothing was decoded.
  static void Show(content::WebContents* web_contents, DecodedQrCodes results);

  DaoQrCodeResultDialogView(content::WebContents* web_contents,
                            DecodedQrCodes results);
  DaoQrCodeResultDialogView(const DaoQrCodeResultDialogView&) = delete;
  DaoQrCodeResultDialogView& operator=(const DaoQrCodeResultDialogView&) =
      delete;
  ~DaoQrCodeResultDialogView() override;

 private:
  // Builds the row stack into the contents view returned via GetContentsView().
  std::unique_ptr<views::View> BuildContents();

  // Copies `text` to the system clipboard.
  void OnCopy(const std::string& text);

  // Opens `url` in a new foreground tab and closes the dialog.
  void OnOpen(const GURL& url);

  base::WeakPtr<content::WebContents> host_web_contents_;
  DecodedQrCodes results_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_RESULT_DIALOG_VIEW_H_
