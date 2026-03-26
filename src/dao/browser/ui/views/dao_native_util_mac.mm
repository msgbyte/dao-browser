// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_util_mac.h"

#import <AppKit/AppKit.h>

#include "content/public/browser/web_contents.h"

// A transparent NSView placed on top of the web content's native view.
// It intercepts mouse clicks AND drag-and-drop events via hit-testing and
// forwards them through the correct channels.
//
// Mouse events are forwarded to the superview so the Chromium Views hover
// and click pipeline works normally.
//
// Drag-and-drop events use a different path: they are forwarded to the
// window's contentView (BridgedContentView), which is the NSView that
// implements NSDraggingDestination and routes events through Chromium's
// DragDropClientMac → DropHelper → views::View hierarchy.  This is
// necessary because the interceptor's superview is typically an
// intermediate clipping container, NOT the BridgedContentView.
//
// The interceptor must register for dragged types so macOS recognizes it
// as a valid drag destination and sends NSDraggingDestination messages
// to it (instead of walking past it to RWHV underneath).
@interface DaoEventInterceptor : NSView
@end

@implementation DaoEventInterceptor

- (instancetype)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    // Register for common drag types so macOS treats this view as a
    // valid drag destination.  Without this, macOS skips this view
    // during hitTest-based drag routing and sends events directly to
    // RenderWidgetHostViewCocoa underneath.
    [self registerForDraggedTypes:@[
      NSPasteboardTypeString, NSPasteboardTypeURL,
      NSPasteboardTypeFileURL
    ]];
  }
  return self;
}

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

// Forward mouse click events to the superview so the views framework
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

// --- NSDraggingDestination protocol ---
// Forward all drag-and-drop events to the window's contentView
// (BridgedContentView).  BridgedContentView.draggingUpdated: calls
// DragDropClientMac.DragUpdate which walks the views::View tree via
// DropHelper to find the correct drop target (DaoSplitView).
//
// We CANNOT forward to self.superview because that is an intermediate
// clipping container that does NOT implement NSDraggingDestination.

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  return cv ? [cv draggingEntered:sender] : NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  return cv ? [cv draggingUpdated:sender] : NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  if (cv) [cv draggingExited:sender];
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  return cv ? [cv prepareForDragOperation:sender] : NO;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  return cv ? [cv performDragOperation:sender] : NO;
}

- (void)concludeDragOperation:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  if (cv) [cv concludeDragOperation:sender];
}

- (void)draggingEnded:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  if (cv && [cv respondsToSelector:@selector(draggingEnded:)]) {
    [cv draggingEnded:sender];
  }
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
  NSWindow* window = [native window];
  if (!window) {
    return;
  }
  NSView* contentView = [window contentView];
  if (!contentView) {
    return;
  }

  // Remove stale interceptor if any.
  if (g_interceptor && [g_interceptor superview]) {
    [g_interceptor removeFromSuperview];
  }

  if (!g_interceptor) {
    g_interceptor = [[DaoEventInterceptor alloc] init];
  }

  // Cover the ENTIRE window contentView so that no native WebContents view
  // (RenderWidgetHostViewCocoa) can intercept drag events.  Placing the
  // interceptor as the topmost subview of BridgedContentView ensures that
  // macOS hit-tests land on the interceptor first.  The interceptor's
  // NSDraggingDestination methods forward to the contentView
  // (BridgedContentView), which routes through DragDropClientMac →
  // DropHelper → views::View tree, reaching DaoSplitView.
  g_interceptor.frame = contentView.bounds;
  g_interceptor.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  [contentView addSubview:g_interceptor
          positioned:NSWindowAbove
          relativeTo:nil];
}

void UnblockWebContentNativeEvents(content::WebContents* web_contents) {
  if (g_interceptor && [g_interceptor superview]) {
    [g_interceptor removeFromSuperview];
  }
}

}  // namespace dao
