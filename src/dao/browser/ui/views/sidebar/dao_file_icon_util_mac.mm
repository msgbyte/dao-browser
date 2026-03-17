// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"

#import <AppKit/AppKit.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <string>

#include "base/apple/foundation_util.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "ui/gfx/image/image_skia_util_mac.h"

namespace dao {

gfx::ImageSkia GetFileIcon(const base::FilePath& file_path, int icon_size) {
  NSString* ns_path = base::SysUTF8ToNSString(file_path.value());
  NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:ns_path];
  if (!icon) {
    return gfx::ImageSkia();
  }
  [icon setSize:NSMakeSize(icon_size, icon_size)];
  gfx::ImageSkia image = gfx::ImageSkiaFromNSImage(icon);
  // Make the image safe to pass from background thread to UI thread.
  image.MakeThreadSafe();
  return image;
}

bool IsImageFile(const base::FilePath& file_path) {
  std::string ext = base::ToLowerASCII(file_path.Extension());
  return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" ||
         ext == ".bmp" || ext == ".tiff" || ext == ".tif" || ext == ".webp" ||
         ext == ".heic" || ext == ".heif" || ext == ".ico" || ext == ".avif";
}

gfx::ImageSkia GetFileThumbnail(const base::FilePath& file_path,
                                int thumb_size) {
  if (!IsImageFile(file_path)) {
    return gfx::ImageSkia();
  }

  NSString* ns_path = base::SysUTF8ToNSString(file_path.value());
  NSURL* url = [NSURL fileURLWithPath:ns_path];

  CGImageSourceRef source =
      CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
  if (!source) {
    return gfx::ImageSkia();
  }

  // Request a larger thumbnail for quality, then center-crop to square.
  int request_size = thumb_size * 3;
  NSDictionary* options = @{
    (__bridge id)kCGImageSourceThumbnailMaxPixelSize : @(request_size),
    (__bridge id)kCGImageSourceCreateThumbnailFromImageAlways : @YES,
    (__bridge id)kCGImageSourceCreateThumbnailWithTransform : @YES,
  };

  CGImageRef cg_thumb = CGImageSourceCreateThumbnailAtIndex(
      source, 0, (__bridge CFDictionaryRef)options);
  CFRelease(source);
  if (!cg_thumb) {
    return gfx::ImageSkia();
  }

  // Center-crop to square for uniform display.
  size_t w = CGImageGetWidth(cg_thumb);
  size_t h = CGImageGetHeight(cg_thumb);
  size_t side = std::min(w, h);
  CGRect crop_rect =
      CGRectMake((w - side) / 2.0, (h - side) / 2.0, side, side);
  CGImageRef cropped = CGImageCreateWithImageInRect(cg_thumb, crop_rect);
  CFRelease(cg_thumb);
  if (!cropped) {
    return gfx::ImageSkia();
  }

  NSImage* ns_image =
      [[NSImage alloc] initWithCGImage:cropped
                                  size:NSMakeSize(thumb_size, thumb_size)];
  CFRelease(cropped);

  gfx::ImageSkia result = gfx::ImageSkiaFromNSImage(ns_image);
  result.MakeThreadSafe();
  return result;
}

}  // namespace dao
