// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_NATIVE_UTIL_MAC_H_
#define DAO_BROWSER_UI_VIEWS_DAO_NATIVE_UTIL_MAC_H_

#include "base/functional/callback.h"
#include "ui/base/dragdrop/mojom/drag_drop_types.mojom-forward.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/native_ui_types.h"

namespace content {
class WebContents;
}

namespace dao {

using TabDragTearOffCallback =
    base::OnceCallback<void(const gfx::Point& screen_point)>;

// Place a transparent event-intercepting NSView on top of the web content's
// native view.  Mouse events are captured by the interceptor and forwarded
// to the compositor view (BridgedContentView), which dispatches them through
// the views framework.  The web content remains fully visible beneath the
// transparent interceptor.
void BlockWebContentNativeEvents(content::WebContents* web_contents);

// Remove the interceptor, restoring normal event flow to the web content.
void UnblockWebContentNativeEvents(content::WebContents* web_contents);

// Observe the native drag session started by |source_web_contents|. The
// callback runs once only when AppKit ends an unaccepted drag with a physical
// mouse release and the release is outside every Dao browser window. Successful
// drops, Escape cancellation, non-mouse termination, and source destruction
// only clean up drag state. A later call replaces any pending observation.
void ObserveTabDragNativeCompletion(content::WebContents* source_web_contents,
                                    TabDragTearOffCallback tear_off_callback);

// Settle the pending native tab drag for |source_web_contents|. The completion
// is ignored when it belongs to another WebContents or no drag is pending.
void CompleteTabDragNativeCompletion(content::WebContents* source_web_contents,
                                     const gfx::PointF& screen_point,
                                     ui::mojom::DragOperation operation,
                                     bool ended_by_mouse_release);

// Cancel the pending native tab drag when its source WebContents is destroyed.
// The completion is ignored when it belongs to another WebContents.
void CancelTabDragNativeCompletion(content::WebContents* source_web_contents);

// Finish a native tab drag in every browser window. This removes native event
// interceptors and resets DaoSplitView's drag-only hit-testing state even when
// the WebUI dragend event was not delivered.
void EndTabDragNativeEvents();

// Arm a process-wide watchdog that force-finishes a native tab drag once the
// mouse button is physically released. This is the last-resort reset for the
// sticky DaoSplitView::tab_drag_active_ state: every logical drag-end path
// (native drop, draggingEnded, WebUI dragend, cancel, source-window teardown)
// shares one physical invariant — the mouse button comes up. Idempotent: arming
// while already armed is a no-op, so fanning this out across all windows is
// safe.
void ArmTabDragWatchdog();

// Stop the tab-drag watchdog. Idempotent. Called from EndTabDragNativeEvents
// via SetTabDragActive(false), and directly when a drag ends cleanly.
void StopTabDragWatchdog();

// Move macOS traffic light buttons (close/minimize/zoom) to the given
// origin within the window's content view. Call after the widget is shown.
void SetTrafficLightsPosition(gfx::NativeWindow window, int x, int y);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_NATIVE_UTIL_MAC_H_
