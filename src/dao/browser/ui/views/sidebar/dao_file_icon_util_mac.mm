// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_file_icon_util_mac.h"

#import <AppKit/AppKit.h>

#include "base/apple/foundation_util.h"
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

}  // namespace dao
