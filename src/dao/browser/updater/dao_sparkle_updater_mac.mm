// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_sparkle_updater_mac.h"

#include <utility>

#import <Sparkle/Sparkle.h>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "dao/browser/updater/dao_sparkle_update_session_state.h"

@interface DaoSparkleUpdaterDelegate
    : NSObject <SPUUpdaterDelegate, SPUStandardUserDriverDelegate> {
 @private
  dao::DaoSparkleUpdaterMac::ReadyToInstallCallback ready_to_install_callback_;
  dao::DaoSparkleUpdaterMac::UpdateSessionFinishedCallback
      update_session_finished_callback_;
  dao::DaoSparkleUpdateSessionState update_session_state_;
}

- (instancetype)initWithReadyToInstallCallback:
                    (dao::DaoSparkleUpdaterMac::ReadyToInstallCallback)
                        readyToInstallCallback
            updateSessionFinishedCallback:
                (dao::DaoSparkleUpdaterMac::UpdateSessionFinishedCallback)
                    updateSessionFinishedCallback;

@end

@implementation DaoSparkleUpdaterDelegate

- (instancetype)initWithReadyToInstallCallback:
                    (dao::DaoSparkleUpdaterMac::ReadyToInstallCallback)
                        readyToInstallCallback
            updateSessionFinishedCallback:
                (dao::DaoSparkleUpdaterMac::UpdateSessionFinishedCallback)
                    updateSessionFinishedCallback {
  self = [super init];
  if (self) {
    ready_to_install_callback_ = std::move(readyToInstallCallback);
    update_session_finished_callback_ = std::move(updateSessionFinishedCallback);
  }
  return self;
}

- (BOOL)updater:(SPUUpdater*)updater
    willInstallUpdateOnQuit:(SUAppcastItem*)item
    immediateInstallationBlock:(void (^)(void))immediateInstallHandler {
  (void)updater;

  if (!update_session_state_.ShouldDaoHandleInstallOnQuit()) {
    return NO;
  }

  void (^install_block)(void) = [immediateInstallHandler copy];
  base::OnceClosure install_callback = base::BindOnce(^{
    if (install_block) {
      install_block();
    }
  });

  std::string display_version;
  if (item.displayVersionString) {
    display_version = base::SysNSStringToUTF8(item.displayVersionString);
  }

  if (!ready_to_install_callback_.is_null()) {
    ready_to_install_callback_.Run(std::move(display_version),
                                   std::move(install_callback));
  }

  return YES;
}

- (void)standardUserDriverWillHandleShowingUpdate:(BOOL)handleShowingUpdate
                                        forUpdate:(SUAppcastItem*)update
                                            state:(SPUUserUpdateState*)state {
  (void)handleShowingUpdate;
  (void)update;

  update_session_state_.OnStandardUpdateWillBeShown(state.userInitiated);
}

- (void)updater:(SPUUpdater*)updater
    didFinishUpdateCycleForUpdateCheck:(SPUUpdateCheck)updateCheck
                                 error:(NSError*)error {
  (void)updater;
  (void)updateCheck;
  (void)error;

  update_session_state_.OnUpdateSessionFinished();

  if (!update_session_finished_callback_.is_null()) {
    update_session_finished_callback_.Run();
  }
}

- (void)updater:(SPUUpdater*)updater didAbortWithError:(NSError*)error {
  (void)updater;
  (void)error;

  update_session_state_.OnUpdateSessionFinished();

  if (!update_session_finished_callback_.is_null()) {
    update_session_finished_callback_.Run();
  }
}

@end

namespace dao {

DaoSparkleUpdaterMac::DaoSparkleUpdaterMac() = default;

DaoSparkleUpdaterMac::~DaoSparkleUpdaterMac() {
  if (controller_) {
    SPUStandardUpdaterController* c =
        (__bridge_transfer SPUStandardUpdaterController*)controller_;
    (void)c;  // Released by ARC at scope exit.
    controller_ = nullptr;
  }
  if (delegate_) {
    DaoSparkleUpdaterDelegate* delegate =
        (__bridge_transfer DaoSparkleUpdaterDelegate*)delegate_;
    (void)delegate;  // Released by ARC at scope exit.
    delegate_ = nullptr;
  }
}

void DaoSparkleUpdaterMac::Start(
    ReadyToInstallCallback ready_to_install_callback,
    UpdateSessionFinishedCallback update_session_finished_callback) {
  if (controller_) {
    return;
  }

  DaoSparkleUpdaterDelegate* delegate = [[DaoSparkleUpdaterDelegate alloc]
      initWithReadyToInstallCallback:std::move(ready_to_install_callback)
        updateSessionFinishedCallback:std::move(
                                          update_session_finished_callback)];
  delegate_ = (__bridge_retained void*)delegate;

  // SPUStandardUpdaterController bundles SPUUpdater + SPUStandardUserDriver,
  // which is what we want — Sparkle's stock UI for "Check for Updates...",
  // "An update is available", and silent background install on quit.
  //
  // Passing startingUpdater:YES starts Sparkle's background scheduler
  // (interval comes from SUScheduledCheckInterval in Info.plist;
  // SUEnableAutomaticChecks must also be YES).
  SPUStandardUpdaterController* controller =
      [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                    updaterDelegate:delegate
                                                 userDriverDelegate:delegate];

  if (!controller) {
    LOG(ERROR) << "DaoSparkleUpdaterMac: failed to construct "
                  "SPUStandardUpdaterController.";
    DaoSparkleUpdaterDelegate* unused_delegate =
        (__bridge_transfer DaoSparkleUpdaterDelegate*)delegate_;
    (void)unused_delegate;  // Released by ARC at scope exit.
    delegate_ = nullptr;
    return;
  }

  // Retain into the type-erased void* slot.
  controller_ = (__bridge_retained void*)controller;

  // Run one silent startup check immediately. Sparkle still owns the recurring
  // schedule, and checkForUpdatesInBackground shows no UI unless an update is
  // found.
  [controller.updater checkForUpdatesInBackground];

  LOG(INFO) << "DaoSparkleUpdaterMac: started. Feed URL and check interval "
               "are configured via Info.plist.";
}

void DaoSparkleUpdaterMac::CheckForUpdatesUserInitiated() {
  if (!controller_) {
    LOG(WARNING) << "DaoSparkleUpdaterMac: CheckForUpdates before Start(); "
                    "ignoring.";
    return;
  }
  SPUStandardUpdaterController* controller =
      (__bridge SPUStandardUpdaterController*)controller_;
  // checkForUpdates: is the same selector the menu item would call when wired
  // through Sparkle's standard target/action plumbing. Calling it directly
  // makes the menu item independent of Sparkle's responder-chain wiring.
  [controller checkForUpdates:nil];
}

}  // namespace dao
