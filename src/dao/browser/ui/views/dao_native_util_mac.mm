// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_native_util_mac.h"

#import <AppKit/AppKit.h>

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_cross_window_drag.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace {

// Forward declaration — defined below the @implementation so the
// interceptor methods can detach themselves after a successful native
// drop. Without this, the transparent DaoEventInterceptor stays mounted
// on every window's contentView after the first cross-window drag, which
// blocks subsequent HTML5 drags in the sidebar WebUI (dragstart never
// fires because the overlay sits in front of the sidebar WebView's
// NSView). Symptom: "first drag works, then tabs can't be dragged at all".
void ClearAllDaoEventInterceptors();

// Drive DaoSplitView's drop-zone overlay from the native interceptor.
// Looks up the Browser that owns |window|, converts the Cocoa drag
// location into DaoSplitView-local coords, and forwards to
// UpdateNativeDropIndicator / HideNativeDropIndicator. No-op if the
// window is not a Dao browser window or the split view is missing.
void UpdateNativeSplitIndicatorForWindow(NSWindow* window, NSPoint cocoa_loc);
void HideNativeSplitIndicatorForWindow(NSWindow* window);

// Cross-window tab-drop fallback. Runs when BridgedContentView's drop
// pipeline refuses our drag (which happens in v147 because the pasteboard
// carries the `chromium-renderer-initiated-drag` marker and the Views-level
// DropHelper short-circuits before reaching DaoSplitView::CanDrop). We
// parse the payload ourselves, find source/target browsers via the native
// window, and execute the detach+insert directly on TabStripModel.
//
// payload: "dao-tab-drag:<source_session_id>:<tab_index>"
// Approximate tab item height in the sidebar WebUI. Used to turn a cursor
// Y offset within the sidebar into a tab index when the user drops a
// cross-window drag. A real indicator that mirrors the exact DOM layout
// would require two-way native/JS plumbing; this estimate gets us close
// enough to feel natural for normal tab counts.
constexpr int kSidebarTabHeightPx = 36;

// Height of the sidebar header area (traffic lights + favorites) before
// the tab list starts. The "+ New Tab" button sits within this band.
constexpr int kSidebarHeaderPx = 60;

// Compute the insertion index in the target's tab strip for a cross-window
// drop. Returns target_model->count() (append) when the cursor is outside
// the sidebar's tab list.
int ComputeCrossWindowInsertIndex(Browser* target_browser,
                                  NSWindow* target_window,
                                  NSPoint cursor_in_window_cocoa) {
  TabStripModel* model = target_browser->tab_strip_model();
  const int default_index = model->count();

  BrowserView* bv =
      BrowserView::GetBrowserViewForBrowser(target_browser);
  if (!bv) {
    return default_index;
  }
  dao::DaoSidebarView* sidebar = bv->dao_sidebar();
  if (!sidebar || !sidebar->GetVisible()) {
    return default_index;
  }

  // Cursor: NSWindow (Cocoa) coords (origin bottom-left, Y grows up).
  // Views: origin top-left, Y grows down. Flip Y.
  CGFloat window_height = [target_window frame].size.height;
  const int cursor_x = static_cast<int>(cursor_in_window_cocoa.x);
  const int cursor_y_views =
      static_cast<int>(window_height - cursor_in_window_cocoa.y);

  gfx::Rect sidebar_bounds = sidebar->bounds();
  if (!sidebar_bounds.Contains(cursor_x, cursor_y_views)) {
    // Dropped outside the sidebar (e.g. on the content area) — append.
    return default_index;
  }

  // Y relative to where the tab list begins inside the sidebar.
  int y_in_list =
      cursor_y_views - sidebar_bounds.y() - kSidebarHeaderPx;
  if (y_in_list < 0) {
    return 0;
  }
  // Mirror the sidebar TS onDragOver_ logic: determine which tab the cursor
  // is over, then split into upper half (insert BEFORE that tab) and lower
  // half (insert AFTER). Without this, drops on the upper half of tab N
  // still land AFTER tab N because a plain floor() ignores the sub-tab
  // position.
  int tab_under = y_in_list / kSidebarTabHeightPx;
  int y_in_tab = y_in_list % kSidebarTabHeightPx;
  int idx = (y_in_tab < kSidebarTabHeightPx / 2) ? tab_under : tab_under + 1;
  if (idx < 0) idx = 0;
  if (idx > model->count()) idx = model->count();
  return idx;
}

bool HandleDaoTabDrop(NSWindow* target_window,
                      NSPoint cursor_in_window_cocoa,
                      NSString* payload) {
  if (!target_window || !payload) {
    return false;
  }
  const std::string text = base::SysNSStringToUTF8(payload);
  int source_sid = 0;
  int tab_index = 0;
  if (!dao::ParseDaoTabDragPayload(text, &source_sid, &tab_index)) {
    LOG(ERROR) << "[Dao-Xwin] HandleDaoTabDrop: malformed payload";
    return false;
  }

  // Find the target Browser — the one whose window matches target_window.
  Browser* target_browser = nullptr;
  Browser* last_active = chrome::FindLastActive();
  if (!last_active) {
    LOG(ERROR) << "[Dao-Xwin] HandleDaoTabDrop: no last-active browser";
    return false;
  }
  for (Browser* b :
       chrome::FindAllBrowsersWithProfile(last_active->profile())) {
    if (!b->window()) {
      continue;
    }
    gfx::NativeWindow nw = b->window()->GetNativeWindow();
    if (nw.GetNativeNSWindow() == target_window) {
      target_browser = b;
      break;
    }
  }
  if (!target_browser) {
    LOG(ERROR) << "[Dao-Xwin] HandleDaoTabDrop: no target browser for window";
    return false;
  }

  // If the cursor is over the content area (DaoSplitView), offer the drop
  // there first — it may create a split, swap panes, or activate the tab.
  // PerformSplitTabDrop returns false if the point is outside the split
  // view; in that case we fall back to a plain sidebar tab move.
  {
    CGFloat window_height = [target_window frame].size.height;
    const int cursor_x = static_cast<int>(cursor_in_window_cocoa.x);
    const int cursor_y_views =
        static_cast<int>(window_height - cursor_in_window_cocoa.y);
    gfx::Point pt_in_browser_view(cursor_x, cursor_y_views);
    if (dao::PerformSplitTabDrop(target_browser, pt_in_browser_view, text)) {
      LOG(ERROR) << "[Dao-Xwin] split drop handled";
      return true;
    }
  }

  const int insert_at = ComputeCrossWindowInsertIndex(
      target_browser, target_window, cursor_in_window_cocoa);
  const bool ok = dao::ExecuteCrossWindowTabMove(target_browser, source_sid,
                                                 tab_index, insert_at);
  LOG(ERROR) << "[Dao-Xwin] HandleDaoTabDrop: moved tab " << tab_index
             << " from sid=" << source_sid << " (insert_at=" << insert_at
             << ", success=" << ok << ")";
  return ok;
}

}  // namespace

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

// Returns YES when the drag carries a Dao-tab payload that we should
// handle natively (bypassing BridgedContentView, whose Views-level drop
// pipeline refuses renderer-initiated drags in v147).
- (BOOL)isDaoTabDrag:(id<NSDraggingInfo>)sender {
  NSString* s =
      [sender.draggingPasteboard stringForType:NSPasteboardTypeString];
  return s && [s hasPrefix:@"dao-tab-drag:"];
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  if ([self isDaoTabDrag:sender]) {
    // Our own drop pipeline — accept the drag so macOS keeps routing
    // draggingUpdated/performDragOperation to us.
    return NSDragOperationMove;
  }
  // Non-Dao drag (e.g. files from Finder): fall through to BridgedContentView.
  NSView* cv = [[self window] contentView];
  return cv ? [cv draggingEntered:sender] : NSDragOperationNone;
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
  if ([self isDaoTabDrag:sender]) {
    // Drive the DaoSplitView drop-zone indicator directly. Without this,
    // the user gets no visual feedback about where the split will happen.
    NSWindow* window = [self window];
    NSPoint loc = sender.draggingLocation;
    UpdateNativeSplitIndicatorForWindow(window, loc);
    return NSDragOperationMove;
  }
  NSView* cv = [[self window] contentView];
  return cv ? [cv draggingUpdated:sender] : NSDragOperationNone;
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
  if ([self isDaoTabDrag:sender]) {
    HideNativeSplitIndicatorForWindow([self window]);
    return;
  }
  NSView* cv = [[self window] contentView];
  if (cv) [cv draggingExited:sender];
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
  if ([self isDaoTabDrag:sender]) {
    return YES;  // We'll do the work in performDragOperation.
  }
  NSView* cv = [[self window] contentView];
  return cv ? [cv prepareForDragOperation:sender] : NO;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  if ([self isDaoTabDrag:sender]) {
    NSString* payload =
        [sender.draggingPasteboard stringForType:NSPasteboardTypeString];
    NSPoint loc = sender.draggingLocation;
    LOG(ERROR) << "[Dao-Xwin] interceptor performDragOperation handling "
                  "Dao tab drag at window coord (" << loc.x << "," << loc.y
               << ")";
    const BOOL ok = HandleDaoTabDrop([self window], loc, payload) ? YES : NO;
    // Dao-tab drops complete entirely in the native layer, so the WebUI
    // dragend path that normally sends `tabDragActive(false)` may not
    // fire (the source tab/window may be gone). Tear down every
    // interceptor here so the next drag starts with the sidebar WebView
    // unobstructed.
    ClearAllDaoEventInterceptors();
    return ok;
  }
  NSView* cv = [[self window] contentView];
  return cv ? [cv performDragOperation:sender] : NO;
}

- (void)concludeDragOperation:(id<NSDraggingInfo>)sender {
  NSView* cv = [[self window] contentView];
  if (cv) [cv concludeDragOperation:sender];
}

- (void)draggingEnded:(id<NSDraggingInfo>)sender {
  // Dao-tab drag is ending (either successful drop just processed in
  // performDragOperation, or cancelled mid-air). Either way, clear all
  // interceptors so we don't leave them mounted on every window.
  if ([self isDaoTabDrag:sender]) {
    ClearAllDaoEventInterceptors();
    return;
  }
  NSView* cv = [[self window] contentView];
  if (cv && [cv respondsToSelector:@selector(draggingEnded:)]) {
    [cv draggingEnded:sender];
  }
}

@end

// Per-window interceptor table. Cross-window tab drag fires
// BlockWebContentNativeEvents on EVERY browser window, so we must keep one
// interceptor per NSWindow — a single global static would be repeatedly
// removed-and-reinstalled, ending up on only the last window while the
// source window lost its interceptor and the HTML5 drag never escaped to
// the native layer.
static NSMapTable<NSWindow*, DaoEventInterceptor*>*
EnsureInterceptorTable() {
  static NSMapTable<NSWindow*, DaoEventInterceptor*>* table = nil;
  if (!table) {
    // Weak keys so entries auto-drop when a window is deallocated; strong
    // values so the interceptor stays alive while mounted.
    table = [NSMapTable
        mapTableWithKeyOptions:NSMapTableWeakMemory
                  valueOptions:NSMapTableStrongMemory];
  }
  return table;
}

namespace {

// Resolve the Browser owning |window| from the profile's browser list.
// Returns nullptr if not found.
Browser* BrowserForWindow(NSWindow* window) {
  if (!window) {
    return nullptr;
  }
  Browser* last_active = chrome::FindLastActive();
  if (!last_active) {
    return nullptr;
  }
  for (Browser* b :
       chrome::FindAllBrowsersWithProfile(last_active->profile())) {
    if (b->window() && b->window()->GetNativeWindow().GetNativeNSWindow() ==
                           window) {
      return b;
    }
  }
  return nullptr;
}

void UpdateNativeSplitIndicatorForWindow(NSWindow* window,
                                         NSPoint cocoa_loc) {
  Browser* target = BrowserForWindow(window);
  if (!target) {
    return;
  }
  const CGFloat window_height = [window frame].size.height;
  gfx::Point pt_in_bv(static_cast<int>(cocoa_loc.x),
                      static_cast<int>(window_height - cocoa_loc.y));
  dao::UpdateSplitDropIndicator(target, pt_in_bv);
}

void HideNativeSplitIndicatorForWindow(NSWindow* window) {
  if (Browser* target = BrowserForWindow(window)) {
    dao::HideSplitDropIndicator(target);
  }
}

NSRect TargetFrameForWebContentsInContentView(NSView* native,
                                              NSView* content_view) {
  if (!native || !content_view) {
    return NSZeroRect;
  }

  NSRect target_frame = [native convertRect:[native bounds]
                                     toView:content_view];
  target_frame = NSIntersectionRect(target_frame, [content_view bounds]);
  if (NSIsEmptyRect(target_frame)) {
    return NSZeroRect;
  }
  return target_frame;
}

// Detach every mounted interceptor from its contentView and drop the
// table entries. Called after a native Dao-tab drop completes, so the
// next HTML5 drag in any window's sidebar starts with an unobstructed
// WebView.
void ClearAllDaoEventInterceptors() {
  NSMapTable* table = EnsureInterceptorTable();
  NSEnumerator* values = [table objectEnumerator];
  DaoEventInterceptor* interceptor = nil;
  while ((interceptor = [values nextObject])) {
    if ([interceptor superview]) {
      [interceptor removeFromSuperview];
    }
  }
  [table removeAllObjects];
}

}  // namespace

namespace dao {

void BlockWebContentNativeEvents(content::WebContents* web_contents) {
  if (!web_contents) {
    LOG(ERROR) << "[Dao-Xwin] BlockWebContentNativeEvents: null WebContents";
    return;
  }
  NSView* native = web_contents->GetNativeView().GetNativeNSView();
  if (!native) {
    LOG(ERROR) << "[Dao-Xwin] BlockWebContentNativeEvents: no NSView";
    return;
  }
  NSWindow* window = [native window];
  if (!window) {
    LOG(ERROR) << "[Dao-Xwin] BlockWebContentNativeEvents: no window";
    return;
  }
  NSView* contentView = [window contentView];
  if (!contentView) {
    LOG(ERROR) << "[Dao-Xwin] BlockWebContentNativeEvents: no contentView";
    return;
  }

  NSMapTable* table = EnsureInterceptorTable();
  DaoEventInterceptor* interceptor = [table objectForKey:window];
  LOG(ERROR) << "[Dao-Xwin] BlockWebContentNativeEvents mounting for window "
             << window
             << " existing_interceptor=" << (interceptor ? "yes" : "no");

  NSRect target_frame =
      TargetFrameForWebContentsInContentView(native, contentView);
  if (NSIsEmptyRect(target_frame)) {
    LOG(ERROR) << "[Dao-Xwin] BlockWebContentNativeEvents: empty target frame";
    return;
  }

  // If one is already mounted on this window, expand it to cover this
  // WebContents too. Re-mounting would briefly tear it down and break an
  // in-flight drag session.
  if (interceptor && [interceptor superview] == contentView) {
    interceptor.frame = NSUnionRect([interceptor frame], target_frame);
    return;
  }

  if (!interceptor) {
    interceptor = [[DaoEventInterceptor alloc] init];
    [table setObject:interceptor forKey:window];
  } else if ([interceptor superview]) {
    // Stale mount on a different contentView; detach before re-mounting.
    [interceptor removeFromSuperview];
  }

  // Cover only the WebContents native view, not the full window contentView.
  // The sidebar WebUI must remain unobstructed so a tab dragged out of the
  // sidebar can re-enter and resume the HTML5 drag/drop path. The interceptor
  // still sits above RenderWidgetHostViewCocoa in the page content area, so
  // web pages cannot swallow split/cross-window tab drags.
  interceptor.frame = target_frame;
  interceptor.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

  [contentView addSubview:interceptor
          positioned:NSWindowAbove
          relativeTo:nil];
}

void UnblockWebContentNativeEvents(content::WebContents* web_contents) {
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

  NSMapTable* table = EnsureInterceptorTable();
  DaoEventInterceptor* interceptor = [table objectForKey:window];
  if (interceptor && [interceptor superview]) {
    [interceptor removeFromSuperview];
  }
  [table removeObjectForKey:window];
}

void SetTrafficLightsPosition(gfx::NativeWindow native_window, int x, int y) {
  if (!native_window)
    return;
  NSWindow* window = native_window.GetNativeNSWindow();
  if (!window)
    return;

  // BrowserWindowFrame._shouldCenterTrafficLights runs during the native
  // layout cycle and resets button positions AFTER views::View::Layout().
  // Use dispatch_async to schedule our repositioning after the native layout
  // completes, so our changes stick.
  CGFloat targetX = (CGFloat)x;
  dispatch_async(dispatch_get_main_queue(), ^{
    NSButton* close = [window standardWindowButton:NSWindowCloseButton];
    NSButton* minimize = [window standardWindowButton:NSWindowMiniaturizeButton];
    NSButton* zoom = [window standardWindowButton:NSWindowZoomButton];
    if (!close || !minimize || !zoom)
      return;

    CGFloat shift = targetX - close.frame.origin.x;
    if (fabs(shift) < 0.5)
      return;  // Already in position, skip.

    NSRect closeFrame = close.frame;
    closeFrame.origin.x += shift;
    close.frame = closeFrame;

    NSRect minFrame = minimize.frame;
    minFrame.origin.x += shift;
    minimize.frame = minFrame;

    NSRect zoomFrame = zoom.frame;
    zoomFrame.origin.x += shift;
    zoom.frame = zoomFrame;
  });
}

}  // namespace dao
