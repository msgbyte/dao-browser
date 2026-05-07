// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_share_mac.h"

#import <AppKit/AppKit.h>

#include "base/strings/sys_string_conversions.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/mac/coordinate_conversion.h"

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

  // |anchor_rect| is in Chromium screen coordinates (origin at the top-left of
  // the primary display, Y axis pointing down). NSSharingServicePicker's
  // showRelativeToRect:ofView: expects the rect in |ns_view|'s local
  // coordinate system. Going through global Cocoa screen coordinates -> window
  // coordinates -> view coordinates is the only path that stays correct on
  // every screen, including non-primary displays whose origin in the global
  // coordinate space is non-zero (and possibly negative).
  //
  // The previous implementation manually flipped Y using ns_view.bounds.height
  // and assumed anchor_rect.x()/y() were already in view space. That happened
  // to land near the button when the window was on the primary display only
  // because the relative offsets cancelled out; on a secondary display the
  // assumption breaks and the popover lands far below the window.
  NSRect screen_rect_cocoa = gfx::ScreenRectToNSRect(anchor_rect);

  NSWindow* window = ns_view.window;
  NSRect rect_in_view;
  if (window) {
    NSRect rect_in_window = [window convertRectFromScreen:screen_rect_cocoa];
    rect_in_view = [ns_view convertRect:rect_in_window fromView:nil];
  } else {
    // Fallback: anchor at the center of the view if it is somehow not in a
    // window. Better than showing the picker at a random screen-derived
    // location.
    rect_in_view = NSMakeRect(NSMidX(ns_view.bounds),
                              NSMidY(ns_view.bounds), 1, 1);
  }

  [picker showRelativeToRect:rect_in_view
                      ofView:ns_view
               preferredEdge:NSMinYEdge];
}

}  // namespace dao
