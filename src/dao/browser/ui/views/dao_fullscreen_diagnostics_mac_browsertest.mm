// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <AppKit/AppKit.h>

#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/test/browser_test.h"

namespace dao {

using DaoFullscreenDiagnosticsBrowserTest = InProcessBrowserTest;

// Temporary debugging coverage: the log smoke test checks both raw callbacks,
// including the enter failure that the windowed controller otherwise ignores.
IN_PROC_BROWSER_TEST_F(DaoFullscreenDiagnosticsBrowserTest,
                       NativeFailureCallbacksKeepWindowedState) {
  auto* view = BrowserView::GetBrowserViewForBrowser(browser());
  NSWindow* window = view->GetNativeWindow().GetNativeNSWindow();
  ASSERT_TRUE([window.delegate
      respondsToSelector:@selector(windowDidFailToEnterFullScreen:)]);
  ASSERT_TRUE([window.delegate
      respondsToSelector:@selector(windowDidFailToExitFullScreen:)]);
  [window.delegate windowDidFailToEnterFullScreen:window];
  [window.delegate windowDidFailToExitFullScreen:window];
  EXPECT_FALSE(view->IsFullscreen());
}

}  // namespace dao
