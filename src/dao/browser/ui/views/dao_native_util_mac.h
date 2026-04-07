// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_NATIVE_UTIL_MAC_H_
#define DAO_BROWSER_UI_VIEWS_DAO_NATIVE_UTIL_MAC_H_

#include "ui/gfx/native_widget_types.h"

namespace content {
class WebContents;
}

namespace dao {

// Place a transparent event-intercepting NSView on top of the web content's
// native view.  Mouse events are captured by the interceptor and forwarded
// to the compositor view (BridgedContentView), which dispatches them through
// the views framework.  The web content remains fully visible beneath the
// transparent interceptor.
void BlockWebContentNativeEvents(content::WebContents* web_contents);

// Remove the interceptor, restoring normal event flow to the web content.
void UnblockWebContentNativeEvents(content::WebContents* web_contents);

// Move macOS traffic light buttons (close/minimize/zoom) to the given
// origin within the window's content view. Call after the widget is shown.
void SetTrafficLightsPosition(gfx::NativeWindow window, int x, int y);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_NATIVE_UTIL_MAC_H_
