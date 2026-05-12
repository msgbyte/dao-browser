// Copyright 2026 Dao Browser. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_QRCODE_DAO_QR_CODE_TYPES_H_
#define DAO_BROWSER_QRCODE_DAO_QR_CODE_TYPES_H_

#include <string>
#include <vector>

#include "url/gurl.h"

namespace dao {

// One decoded barcode. ZXing reports symbology in `format`
// (QR_CODE / MICRO_QR_CODE / DATA_MATRIX / AZTEC), preserved verbatim.
struct DecodedQrCode {
  std::string text;
  std::string format;
  bool is_url = false;
  GURL url;

  DecodedQrCode();
  DecodedQrCode(const DecodedQrCode&);
  DecodedQrCode& operator=(const DecodedQrCode&);
  DecodedQrCode(DecodedQrCode&&) noexcept;
  DecodedQrCode& operator=(DecodedQrCode&&) noexcept;
  ~DecodedQrCode();
};

using DecodedQrCodes = std::vector<DecodedQrCode>;

}  // namespace dao

#endif  // DAO_BROWSER_QRCODE_DAO_QR_CODE_TYPES_H_
