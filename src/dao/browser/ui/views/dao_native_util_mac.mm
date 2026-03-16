// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_util_mac.h"

#import <AppKit/AppKit.h>

#include "content/public/browser/web_contents.h"

// A transparent NSView placed on top of the web content's native view.
// It intercepts all mouse hit-tests and forwards the resulting events
// to its superview (the compositor / BridgedContentView), which then
// dispatches them through the Chromium views event pipeline.
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

// Forward mouse events to the compositor view so the views framework
// can dispatch them to the command bar overlay.
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
}

void UnblockWebContentNativeEvents(content::WebContents* web_contents) {
  if (g_interceptor && [g_interceptor superview]) {
    [g_interceptor removeFromSuperview];
  }
}

}  // namespace dao
