// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/exclusive_access/exclusive_access_test.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/test/base/ui_test_utils.h"
#import "content/app_shim_remote_cocoa/render_widget_host_view_cocoa.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "ui/views/test/mock_activation_controller.h"
#include "ui/views/widget/widget.h"

namespace dao {

class DaoFullscreenEscapeMacBrowserTest : public ExclusiveAccessTest {
 public:
  DaoFullscreenEscapeMacBrowserTest() {
    feature_list_.InitAndEnableFeature(
        features::kPressAndHoldEscToExitBrowserFullscreen);
  }

 protected:
  BrowserView* view() {
    return BrowserView::GetBrowserViewForBrowser(browser());
  }

  void SendKey(NSEventType type,
               unsigned short code,
               NSString* characters,
               NSEventModifierFlags modifiers = 0,
               bool repeat = false,
               NSWindow* window = nil) {
    if (!window) {
      window = view()->GetNativeWindow().GetNativeNSWindow();
    }
    NSEvent* event =
        [NSEvent keyEventWithType:type
                               location:NSZeroPoint
                          modifierFlags:modifiers
                              timestamp:NSProcessInfo.processInfo.systemUptime
                           windowNumber:window.windowNumber
                                context:nil
                             characters:characters
            charactersIgnoringModifiers:characters
                              isARepeat:repeat
                                keyCode:code];
    [NSApp sendEvent:event];
    base::RunLoop().RunUntilIdle();
  }

  void EnterBrowserFullscreen() {
    ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
    ui_test_utils::ToggleFullscreenModeAndWait(browser());
    ASSERT_TRUE(view()->IsFullscreen());
  }

  void CheckCommandBarDismissal(unsigned short code, NSString* characters) {
    EnterBrowserFullscreen();
    const int tab_count = browser()->tab_strip_model()->count();
    auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
    EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(10);
    EXPECT_CALL(*mock_controller(), HandleUserHeldEscape()).Times(0);
    for (int attempt = 0; attempt < 10; ++attempt) {
      SCOPED_TRACE(attempt);
      SendKey(NSEventTypeKeyDown, code, characters, NSEventModifierFlagCommand);
      SendKey(NSEventTypeKeyUp, code, characters, NSEventModifierFlagCommand);
      ASSERT_TRUE(view()->dao_command_bar()->GetVisible());

      SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
      ASSERT_FALSE(view()->dao_command_bar()->GetVisible());
      ASSERT_TRUE(IsEscKeyHoldTimerRunning());
      Wait(base::Milliseconds(100));
      NSResponder* responder =
          view()->GetNativeWindow().GetNativeNSWindow().firstResponder;
      ASSERT_TRUE([responder isKindOfClass:[RenderWidgetHostViewCocoa class]]);
      EXPECT_TRUE([(RenderWidgetHostViewCocoa*)responder
          suppressNextKeyUpForTesting:kVK_Escape]);
      SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
      EXPECT_FALSE(IsEscKeyHoldTimerRunning());
      Wait(base::Seconds(3));
      ASSERT_TRUE(view()->IsFullscreen());
      EXPECT_FALSE(IsExclusiveAccessBubbleDisplayed());
      EXPECT_EQ(tab_count, browser()->tab_strip_model()->count());
      EXPECT_EQ(contents, browser()->tab_strip_model()->GetActiveWebContents());
      EXPECT_EQ(GURL("about:blank"), contents->GetLastCommittedURL());
      EXPECT_TRUE(
          content::EvalJs(contents, "document.hasFocus()").ExtractBool());
    }
  }

 private:
  base::test::ScopedFeatureList feature_list_;
};

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       NewTabDismissalCancelsHold) {
  CheckCommandBarDismissal(kVK_ANSI_T, @"t");
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       AddressDismissalCancelsHold) {
  CheckCommandBarDismissal(kVK_ANSI_L, @"l");
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       PageTapAndModifiedReleaseCancelOnce) {
  EnterBrowserFullscreen();
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(2);
  EXPECT_CALL(*mock_controller(), HandleUserHeldEscape()).Times(0);
  const NSEventModifierFlags release_modifiers[] = {0,
                                                    NSEventModifierFlagCommand};
  for (NSEventModifierFlags modifiers : release_modifiers) {
    SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
    ASSERT_TRUE(IsEscKeyHoldTimerRunning());
    Wait(base::Milliseconds(100));
    SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b", modifiers);
    EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  }
  Wait(base::Seconds(3));
  EXPECT_TRUE(view()->IsFullscreen());
  EXPECT_FALSE(IsExclusiveAccessBubbleDisplayed());
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       DelayedPageReplyDoesNotRestartHold) {
  EnterBrowserFullscreen();
  auto* contents = browser()->tab_strip_model()->GetActiveWebContents();
  contents->Focus();
  ASSERT_TRUE(content::ExecJs(contents, R"(
    document.addEventListener('keydown', () => {
      const deadline = performance.now() + 300;
      while (performance.now() < deadline) {}
    }, {once: true});
  )"));
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(1);
  EXPECT_CALL(*mock_controller(), HandleUserHeldEscape()).Times(0);
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  ASSERT_TRUE(IsEscKeyHoldTimerRunning());
  Wait(base::Milliseconds(100));
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  Wait(base::Seconds(3));
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  EXPECT_TRUE(view()->IsFullscreen());
  EXPECT_FALSE(IsExclusiveAccessBubbleDisplayed());
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       HeldEscapeWithRepeatStillExits) {
  EnterBrowserFullscreen();
  view()->dao_command_bar()->ShowForNewTab();
  base::RunLoop().RunUntilIdle();
  EXPECT_CALL(*mock_controller(), HandleUserHeldEscape()).Times(1);
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(0);
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  ASSERT_FALSE(view()->dao_command_bar()->GetVisible());
  ASSERT_TRUE(IsEscKeyHoldTimerRunning());
  Wait(base::Milliseconds(300));
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b", 0, true);
  Wait(base::Milliseconds(300));
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b", 0, true);
  Wait(base::Seconds(1));
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  EXPECT_FALSE(view()->IsFullscreen());
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       PageToNativeFocusCancelsHold) {
  EnterBrowserFullscreen();
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(1);
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  ASSERT_TRUE(IsEscKeyHoldTimerRunning());
  view()->dao_command_bar()->Show();
  base::RunLoop().RunUntilIdle();
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  Wait(base::Seconds(3));
  EXPECT_TRUE(view()->IsFullscreen());
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       OtherWindowReleaseDoesNotCancelHold) {
  EnterBrowserFullscreen();
  NSWindow* other =
      [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 100, 100)
                                  styleMask:NSWindowStyleMaskTitled
                                    backing:NSBackingStoreBuffered
                                      defer:NO];
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(1);
  view()->dao_command_bar()->Show();
  base::RunLoop().RunUntilIdle();
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  ASSERT_TRUE(IsEscKeyHoldTimerRunning());
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b", 0, false, other);
  EXPECT_TRUE(IsEscKeyHoldTimerRunning());
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
}

class DaoFullscreenEscapeMacActivationBrowserTest
    : public DaoFullscreenEscapeMacBrowserTest {
 private:
  views::test::MockActivationController activation_controller_;
};

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacActivationBrowserTest,
                       DeactivationCancelsHoldBeforeReleaseInAnotherWindow) {
  EnterBrowserFullscreen();
  view()->GetWidget()->Activate();
  ASSERT_TRUE(view()->GetWidget()->IsActive());
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(1);
  EXPECT_CALL(*mock_controller(), HandleUserHeldEscape()).Times(0);
  view()->dao_command_bar()->Show();
  base::RunLoop().RunUntilIdle();
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  ASSERT_TRUE(IsEscKeyHoldTimerRunning());
  auto* other_browser = CreateBrowser(browser()->GetProfile());
  NSWindow* other = BrowserView::GetBrowserViewForBrowser(other_browser)
                        ->GetNativeWindow()
                        .GetNativeNSWindow();
  BrowserView::GetBrowserViewForBrowser(other_browser)->GetWidget()->Activate();
  ASSERT_FALSE(view()->GetWidget()->IsActive());
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b", 0, false, other);
  Wait(base::Seconds(3));
  EXPECT_TRUE(view()->IsFullscreen());
  EXPECT_FALSE(
      BrowserView::GetBrowserViewForBrowser(other_browser)->IsFullscreen());
  CloseBrowserSynchronously(other_browser);
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       KeyboardLockRetainsTapAndHoldSemantics) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  EnterActiveTabFullscreen();
  ASSERT_TRUE(RequestKeyboardLock(/*esc_key_locked=*/true));
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  EXPECT_CALL(*mock_controller(), HandleUserReleasedEscapeEarly()).Times(1);
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  Wait(base::Milliseconds(100));
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
  EXPECT_TRUE(GetExclusiveAccessManager()
                  ->keyboard_lock_controller()
                  ->IsKeyboardLockActive());
  EXPECT_TRUE(IsWindowFullscreenForTabOrPending());
  EXPECT_CALL(*mock_controller(), HandleUserHeldEscape()).Times(1);
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  Wait(base::Seconds(2));
  EXPECT_FALSE(GetExclusiveAccessManager()
                   ->keyboard_lock_controller()
                   ->IsKeyboardLockActive());
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsWindowFullscreenForTabOrPending());
}

IN_PROC_BROWSER_TEST_F(DaoFullscreenEscapeMacBrowserTest,
                       PageFullscreenAndPointerLockStillExitOnPress) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  EnterActiveTabFullscreen();
  RequestToLockPointer(/*user_gesture=*/true,
                       /*last_unlocked_by_target=*/false);
  ASSERT_TRUE(GetExclusiveAccessManager()
                  ->pointer_lock_controller()
                  ->IsPointerLocked());
  browser()->tab_strip_model()->GetActiveWebContents()->Focus();
  SendKey(NSEventTypeKeyDown, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsWindowFullscreenForTabOrPending());
  EXPECT_FALSE(GetExclusiveAccessManager()
                   ->pointer_lock_controller()
                   ->IsPointerLocked());
  SendKey(NSEventTypeKeyUp, kVK_Escape, @"\x1b");
  EXPECT_FALSE(IsEscKeyHoldTimerRunning());
}

}  // namespace dao
