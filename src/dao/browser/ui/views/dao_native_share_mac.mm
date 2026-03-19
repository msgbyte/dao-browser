// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_share_mac.h"

#import <AppKit/AppKit.h>

#include "base/strings/sys_string_conversions.h"
#include "ui/gfx/geometry/rect.h"

namespace dao {

void ShowNativeShareMac(const std::string& url,
                        const std::string& title,
                        gfx::NativeView anchor_view,
                        const gfx::Rect& anchor_rect) {
  NSView* ns_view = anchor_view.GetNativeNSView();
  if (!ns_view) {
    return;
  }

  NSURL* ns_url = [NSURL URLWithString:base::SysUTF8ToNSString(url)];
  if (!ns_url) {
    return;
  }

  NSArray* items = @[ ns_url ];
  NSSharingServicePicker* picker =
      [[NSSharingServicePicker alloc] initWithItems:items];

  // Convert anchor rect to NSView coordinates (flipped)
  NSRect ns_rect = NSMakeRect(anchor_rect.x(),
                               ns_view.bounds.size.height - anchor_rect.bottom(),
                               anchor_rect.width(),
                               anchor_rect.height());
  [picker showRelativeToRect:ns_rect
                      ofView:ns_view
               preferredEdge:NSMinYEdge];
}

}  // namespace dao
