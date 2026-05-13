// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Linked into browser_tests on macOS to keep the test process from stealing
// foreground focus during local development. Swizzles NSApplication's
// activate/activateIgnoringOtherApps: into no-ops and clamps
// setActivationPolicy: to Accessory.
//
// The initial Accessory policy is applied via dispatch_async to the main
// queue rather than directly: touching [NSApplication sharedApplication]
// from a load-time constructor would instantiate the base class and crash
// Chromium's RegisterBrowserCrApp() (it expects to install BrowserCrApplication
// as the NSApp subclass before NSApp exists).

#import <AppKit/AppKit.h>
#import <dispatch/dispatch.h>
#import <objc/runtime.h>

namespace {

void SwapMethods(Class cls, SEL original, SEL replacement) {
  Method orig = class_getInstanceMethod(cls, original);
  Method repl = class_getInstanceMethod(cls, replacement);
  if (orig && repl) {
    method_exchangeImplementations(orig, repl);
  }
}

}  // namespace

@interface NSApplication (DaoTestNoFocusSteal)
@end

@implementation NSApplication (DaoTestNoFocusSteal)

- (void)dao_test_activateIgnoringOtherApps:(BOOL)flag {
}

- (void)dao_test_activate {
}

- (void)dao_test_setActivationPolicy:(NSApplicationActivationPolicy)policy {
  // Not infinite recursion: after the swizzle, this selector resolves to
  // the original setActivationPolicy: implementation.
  [self dao_test_setActivationPolicy:NSApplicationActivationPolicyAccessory];
}

@end

namespace {

__attribute__((constructor)) void DaoMakeBrowserTestsAccessory() {
  Class cls = [NSApplication class];
  SwapMethods(cls, @selector(activateIgnoringOtherApps:),
              @selector(dao_test_activateIgnoringOtherApps:));
  SwapMethods(cls, @selector(activate), @selector(dao_test_activate));
  SwapMethods(cls, @selector(setActivationPolicy:),
              @selector(dao_test_setActivationPolicy:));

  dispatch_async(dispatch_get_main_queue(), ^{
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
  });
}

}  // namespace
