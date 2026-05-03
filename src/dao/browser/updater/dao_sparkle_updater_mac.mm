// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_sparkle_updater_mac.h"

#import <Sparkle/Sparkle.h>

#include "base/logging.h"

namespace dao {

DaoSparkleUpdaterMac::DaoSparkleUpdaterMac() = default;

DaoSparkleUpdaterMac::~DaoSparkleUpdaterMac() {
  if (controller_) {
    SPUStandardUpdaterController* c =
        (__bridge_transfer SPUStandardUpdaterController*)controller_;
    (void)c;  // Released by ARC at scope exit.
    controller_ = nullptr;
  }
}

void DaoSparkleUpdaterMac::Start() {
  if (controller_) {
    return;
  }

  // SPUStandardUpdaterController bundles SPUUpdater + SPUStandardUserDriver,
  // which is what we want — Sparkle's stock UI for "Check for Updates...",
  // "An update is available", and silent background install on quit.
  //
  // Passing startingUpdater:YES makes Sparkle begin its scheduled background
  // checks immediately (interval comes from SUScheduledCheckInterval in
  // Info.plist; SUEnableAutomaticChecks must also be YES).
  //
  // We pass nil for the delegates: declarative config via Info.plist is
  // enough for the static feed URL + EdDSA pubkey + auto-install behaviour.
  // If we ever need callbacks (e.g. "an update is ready, please relaunch"
  // toast), this is where a real delegate object would be passed.
  SPUStandardUpdaterController* controller =
      [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                    updaterDelegate:nil
                                                 userDriverDelegate:nil];

  if (!controller) {
    LOG(ERROR) << "DaoSparkleUpdaterMac: failed to construct "
                  "SPUStandardUpdaterController.";
    return;
  }

  // Retain into the type-erased void* slot.
  controller_ = (__bridge_retained void*)controller;

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
