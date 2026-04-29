// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_NATIVE_SHARE_MAC_H_
#define DAO_BROWSER_UI_VIEWS_DAO_NATIVE_SHARE_MAC_H_

#include <string>

#include "ui/gfx/native_ui_types.h"

namespace gfx {
class Rect;
}

namespace dao {

// Show the macOS native share picker (NSSharingServicePicker) for the given
// URL.  |anchor_view| and |anchor_rect| determine where the picker appears.
void ShowNativeShareMac(const std::string& url,
                        const std::string& title,
                        gfx::NativeView anchor_view,
                        const gfx::Rect& anchor_rect);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_NATIVE_SHARE_MAC_H_
