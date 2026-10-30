// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"

#include <windows.h>

#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>
#include <wrl/client.h>

#include "base/win/scoped_gdi_object.h"
#include "skia/ext/image_operations.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/win/icon_util.h"

namespace dao {
namespace {

SkBitmap LoadShellIcon(int icon_index, int image_list_size) {
  Microsoft::WRL::ComPtr<IImageList> image_list;
  if (FAILED(SHGetImageList(image_list_size, IID_PPV_ARGS(&image_list)))) {
    return {};
  }

  HICON handle = nullptr;
  const HRESULT result =
      image_list->GetIcon(icon_index, ILD_TRANSPARENT, &handle);
  base::win::ScopedGDIObject<HICON> icon(handle);
  if (FAILED(result) || !icon.is_valid()) {
    return {};
  }
  SkBitmap bitmap = IconUtil::CreateSkBitmapFromHICON(icon.get());
  if (image_list_size == SHIL_JUMBO && !bitmap.drawsNothing()) {
    // Shell image lists can return a small icon in a padded 256px cell and
    // do not implement IImageList2::GetOriginalSize. Reject mostly empty cells
    // so older file associations retain a full-size icon via the next list.
    gfx::Rect visible_bounds;
    for (int y = 0; y < bitmap.height(); ++y) {
      for (int x = 0; x < bitmap.width(); ++x) {
        if (SkColorGetA(bitmap.getColor(x, y)) != 0) {
          visible_bounds.Union(gfx::Rect(x, y, 1, 1));
        }
      }
    }
    if (visible_bounds.width() <= bitmap.width() / 2 &&
        visible_bounds.height() <= bitmap.height() / 2) {
      return {};
    }
  }
  return bitmap;
}

}  // namespace

gfx::ImageSkia GetFileIcon(const base::FilePath& file_path, int icon_size) {
  if (icon_size <= 0) {
    return {};
  }
  SHFILEINFOW info = {};
  if (!SHGetFileInfoW(file_path.value().c_str(), FILE_ATTRIBUTE_NORMAL, &info,
                     sizeof(info), SHGFI_ICON | SHGFI_SYSICONINDEX |
                                       SHGFI_USEFILEATTRIBUTES |
                                       (icon_size <= 16 ? SHGFI_SMALLICON
                                                        : SHGFI_LARGEICON))) {
    return {};
  }
  base::win::ScopedGDIObject<HICON> icon(info.hIcon);
  SkBitmap bitmap;
  for (int image_list_size :
       {SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE, SHIL_SMALL}) {
    bitmap = LoadShellIcon(info.iIcon, image_list_size);
    if (!bitmap.drawsNothing()) {
      break;
    }
  }
  if (bitmap.drawsNothing() && icon.is_valid()) {
    bitmap = IconUtil::CreateSkBitmapFromHICON(icon.get());
  }
  if (bitmap.drawsNothing()) {
    return {};
  }

  // Resize from the native pixels once per scale. The sidebar encodes the 2x
  // representation, while native drag images still need the logical DIP size.
  gfx::ImageSkia image;
  for (int scale : {1, 2}) {
    const int pixel_size = icon_size * scale;
    image.AddRepresentation(gfx::ImageSkiaRep(
        skia::ImageOperations::Resize(bitmap,
                                     skia::ImageOperations::RESIZE_LANCZOS3,
                                     pixel_size, pixel_size),
        static_cast<float>(scale)));
  }
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
