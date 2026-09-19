// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <AppKit/AppKit.h>

#include <optional>

#import "base/apple/scoped_objc_class_swizzler.h"
#include "chrome/test/base/in_process_browser_test.h"
#import "content/app_shim_remote_cocoa/web_contents_view_cocoa.h"
#include "content/browser/web_contents/web_contents_view_mac.h"
#include "content/public/browser/web_contents_view_delegate.h"
#include "content/public/test/browser_test.h"
#include "ui/gfx/geometry/point_f.h"

@interface DaoDragPrimaryScreenFrame : NSObject
- (NSRect)frame;
@end

@implementation DaoDragPrimaryScreenFrame
- (NSRect)frame {
  return NSMakeRect(0, 0, 1440, 900);
}
@end

@interface DaoDragSecondaryScreen : NSObject
- (NSRect)frame;
@end

@implementation DaoDragSecondaryScreen
- (NSRect)frame {
  return NSMakeRect(1440, -180, 1920, 1080);
}

- (id)forwardingTargetForSelector:(SEL)selector {
  return NSScreen.screens.firstObject;
}
@end

@interface DaoDragSecondaryWindow : NSWindow
@end

@implementation DaoDragSecondaryWindow
- (NSScreen*)screen {
  return (NSScreen*)[[DaoDragSecondaryScreen alloc] init];
}
@end

namespace dao {
namespace {

class DragEndCapture : public content::WebContentsViewMac {
 public:
  DragEndCapture() : WebContentsViewMac(nullptr, nullptr) {}

  void EndDrag(uint32_t operation,
               const gfx::PointF& local_point,
               const gfx::PointF& screen_point,
               bool ended_by_mouse_release) override {
    screen_point_ = screen_point;
  }

  std::optional<gfx::PointF> screen_point_;
};

using DaoTabDragMacBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(DaoTabDragMacBrowserTest,
                       DragEndUsesPrimaryScreenCoordinates) {
  DaoDragSecondaryWindow* window = [[DaoDragSecondaryWindow alloc]
      initWithContentRect:NSMakeRect(1500, 100, 400, 300)
                styleMask:NSWindowStyleMaskBorderless
                  backing:NSBackingStoreBuffered
                    defer:NO];
  WebContentsViewCocoa* view =
      [[WebContentsViewCocoa alloc] initWithViewsHostableView:nullptr];
  window.contentView = view;
  DragEndCapture host;
  [view setHost:&host];
  NSDraggingSession* session = [[NSDraggingSession alloc] init];

  {
    base::apple::ScopedObjCClassSwizzler primary_screen_frame(
        [NSScreen class], [DaoDragPrimaryScreenFrame class], @selector(frame));
    // Points on the secondary display and above/left of the primary display.
    // Fractional coordinates must survive the native-to-Mojo conversion.
    const struct {
      NSPoint cocoa;
      gfx::PointF expected;
    } cases[] = {
        {NSMakePoint(1600.5, 700.25), gfx::PointF(1600.5, 199.75)},
        {NSMakePoint(1600, -100), gfx::PointF(1600, 1000)},
        {NSMakePoint(-100, 1000), gfx::PointF(-100, -100)},
    };
    for (const auto& test : cases) {
      host.screen_point_.reset();
      [view draggingSession:session
               endedAtPoint:test.cocoa
                  operation:NSDragOperationNone];
      EXPECT_EQ(std::make_optional(test.expected), host.screen_point_);
    }
  }

  [view setHost:nullptr];
  window.contentView = nil;
}

}  // namespace
}  // namespace dao
