// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_IMAGE_H_
#define DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_IMAGE_H_

#include "ui/gfx/image/image_skia.h"

namespace gfx {
class Image;
}

namespace qr_code_generator {
struct GeneratedCode;
}

namespace dao {

// Renders QR data with Dao visual styling and a compact light margin around
// the encoded modules.
gfx::ImageSkia RenderDaoQrCode(const qr_code_generator::GeneratedCode& code,
                               int size);
gfx::ImageSkia RenderDaoQrCode(const qr_code_generator::GeneratedCode& code,
                               int size,
                               const gfx::Image& center_icon);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_QR_CODE_IMAGE_H_
