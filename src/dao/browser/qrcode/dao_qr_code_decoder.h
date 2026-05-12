// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_QRCODE_DAO_QR_CODE_DECODER_H_
#define DAO_BROWSER_QRCODE_DAO_QR_CODE_DECODER_H_

#include "base/functional/callback_forward.h"
#include "dao/browser/qrcode/dao_qr_code_types.h"

class SkBitmap;

namespace dao {

// Off-thread QR decoder. The blocking ZXing call runs on the ThreadPool;
// the callback is invoked on the calling sequence. Empty result means
// "no codes found" — failures (corrupt input, ZXing exception) are also
// surfaced as an empty vector so callers don't need to distinguish.
class DaoQrCodeDecoder {
 public:
  using Callback = base::OnceCallback<void(DecodedQrCodes)>;

  static void DecodeBitmapAsync(SkBitmap bitmap, Callback callback);

  // Public for unit tests. Blocks the calling thread.
  static DecodedQrCodes DecodeBitmapBlocking(const SkBitmap& bitmap);

 private:
  DaoQrCodeDecoder() = delete;
};

}  // namespace dao

#endif  // DAO_BROWSER_QRCODE_DAO_QR_CODE_DECODER_H_
