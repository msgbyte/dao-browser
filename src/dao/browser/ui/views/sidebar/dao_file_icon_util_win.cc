// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"

#include <windows.h>
#include <shellapi.h>

#include "base/win/scoped_gdi_object.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/win/icon_util.h"

namespace dao {

gfx::ImageSkia GetFileIcon(const base::FilePath& file_path, int icon_size) {
  if (icon_size <= 0) {
    return {};
  }
  SHFILEINFOW info = {};
  if (!SHGetFileInfoW(file_path.value().c_str(), FILE_ATTRIBUTE_NORMAL, &info,
                      sizeof(info), SHGFI_ICON | SHGFI_USEFILEATTRIBUTES |
                                        (icon_size <= 16 ? SHGFI_SMALLICON
                                                         : SHGFI_LARGEICON))) {
    return {};
  }
  base::win::ScopedGDIObject<HICON> icon(info.hIcon);
  SkBitmap bitmap = IconUtil::CreateSkBitmapFromHICON(
      icon.get(), gfx::Size(icon_size, icon_size));
  if (bitmap.isNull()) {
    return {};
  }
  gfx::ImageSkia image = gfx::ImageSkia::CreateFrom1xBitmap(bitmap);
  image.MakeThreadSafe();
  return image;
}

bool IsImageFile(const base::FilePath& file_path) {
  return file_path.MatchesExtension(FILE_PATH_LITERAL(".jpg")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".jpeg")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".png")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".gif")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".bmp")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".webp")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".ico")) ||
         file_path.MatchesExtension(FILE_PATH_LITERAL(".avif"));
}

gfx::ImageSkia GetFileThumbnail(const base::FilePath& file_path, int thumb_size) {
  // ImageIO thumbnails are macOS-only. An empty result selects the existing
  // system-file-icon fallback in the download UI.
  return {};
}

}  // namespace dao
