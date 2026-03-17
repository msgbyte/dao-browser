// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_FILE_ICON_UTIL_MAC_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_FILE_ICON_UTIL_MAC_H_

#include "base/files/file_path.h"
#include "ui/gfx/image/image_skia.h"

namespace dao {

// Returns the system file type icon for the given file path, resized to
// |icon_size| DIP. Must be called on a thread that allows blocking I/O.
gfx::ImageSkia GetFileIcon(const base::FilePath& file_path, int icon_size);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_FILE_ICON_UTIL_MAC_H_
