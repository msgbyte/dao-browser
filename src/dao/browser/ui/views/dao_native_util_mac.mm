// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_util_mac.h"

#import <AppKit/AppKit.h>

#include "content/public/browser/web_contents.h"

// A transparent NSView placed on top of the web content's native view.
// It intercepts mouse clicks via hit-testing and forwards them to the
// compositor's BridgedContentView through the superview chain.
//
// Mouse-move / enter / exit events are NOT routed through this view.
// Instead, a global NSEvent local monitor is installed so that the
// BridgedContentView's own tracking area continues to fire, allowing
// the Chromium Views hover pipeline to work normally.
@interface DaoEventInterceptor : NSView
@end

@implementation DaoEventInterceptor

- (BOOL)isOpaque {
  return NO;
}
- (BOOL)acceptsFirstResponder {
  return NO;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
  return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
  // Intentionally empty – fully transparent.
}

- (NSView*)hitTest:(NSPoint)point {
  NSPoint local = [self convertPoint:point fromView:self.superview];
  if (NSPointInRect(local, self.bounds)) {
    return self;
  }
  return nil;
}

// Forward mouse click events to the compositor view so the views framework
// can dispatch them to the popup overlay.
- (void)mouseDown:(NSEvent*)event {
  [self.superview mouseDown:event];
}
- (void)mouseUp:(NSEvent*)event {
  [self.superview mouseUp:event];
}
- (void)mouseDragged:(NSEvent*)event {
  [self.superview mouseDragged:event];
}
- (void)rightMouseDown:(NSEvent*)event {
  [self.superview rightMouseDown:event];
}
- (void)rightMouseUp:(NSEvent*)event {
  [self.superview rightMouseUp:event];
}
- (void)otherMouseDown:(NSEvent*)event {
  [self.superview otherMouseDown:event];
}
- (void)otherMouseUp:(NSEvent*)event {
  [self.superview otherMouseUp:event];
}
- (void)scrollWheel:(NSEvent*)event {
  [self.superview scrollWheel:event];
}

@end

static DaoEventInterceptor* g_interceptor = nil;

// Global local-event monitor that re-sends mouse-move events to the
// window's contentView (BridgedContentView) so that the Chromium Views
// hover / enter / exit pipeline works regardless of which NSView AppKit's
// hit-test picks.
static id g_mouse_monitor = nil;

namespace dao {

void BlockWebContentNativeEvents(content::WebContents* web_contents) {
  if (!web_contents) {
    return;
  }
  NSView* native = web_contents->GetNativeView().GetNativeNSView();
  if (!native) {
    return;
  }
  NSView* parent = [native superview];
  if (!parent) {
    return;
  }

  // Remove stale interceptor if any.
  if (g_interceptor && [g_interceptor superview]) {
    [g_interceptor removeFromSuperview];
  }

  if (!g_interceptor) {
    g_interceptor = [[DaoEventInterceptor alloc] init];
  }

  g_interceptor.frame = native.frame;
  g_interceptor.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  // Insert directly above the web content's native view.
  [parent addSubview:g_interceptor
          positioned:NSWindowAbove
          relativeTo:native];

  // Install a local event monitor so that mouse-move / enter / exit events
  // bypass the interceptor's hit-test and still reach BridgedContentView.
  if (!g_mouse_monitor) {
    NSEventMask mask = NSEventMaskMouseMoved | NSEventMaskMouseEntered |
                       NSEventMaskMouseExited;
    NSWindow* window = [native window];
    g_mouse_monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:mask
                                    handler:^NSEvent*(NSEvent* event) {
                                      if ([event window] == window) {
                                        NSView* cv = [window contentView];
                                        // Directly call the BaseView method
                                        // which routes to
                                        // BridgedContentView::mouseEvent:.
                                        switch ([event type]) {
                                          case NSEventTypeMouseMoved:
                                            [cv mouseMoved:event];
                                            break;
                                          case NSEventTypeMouseEntered:
                                            [cv mouseEntered:event];
                                            break;
                                          case NSEventTypeMouseExited:
                                            [cv mouseExited:event];
                                            break;
                                          default:
                                            break;
                                        }
                                      }
                                      return event;
                                    }];
  }
}

void UnblockWebContentNativeEvents(content::WebContents* web_contents) {
  if (g_interceptor && [g_interceptor superview]) {
    [g_interceptor removeFromSuperview];
  }

  if (g_mouse_monitor) {
    [NSEvent removeMonitor:g_mouse_monitor];
    g_mouse_monitor = nil;
  }
}

}  // namespace dao
