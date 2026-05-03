// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UPDATER_DAO_SPARKLE_UPDATER_MAC_H_
#define DAO_BROWSER_UPDATER_DAO_SPARKLE_UPDATER_MAC_H_

#include "base/memory/raw_ptr_exclusion.h"

namespace dao {

// Thin Objective-C++ wrapper around Sparkle's SPUStandardUpdaterController.
// One instance per process, owned by DaoUpdaterService::Impl. Methods must be
// called on the UI thread (= main NSRunLoop).
//
// All update behaviour (feed URL, automatic checks, automatic install on
// quit) is configured declaratively via Info.plist keys (see
// branding/mac/dao.entitlements is for sandboxing — Sparkle keys live in the
// app bundle's Info.plist, set by the chrome/app/app-Info.plist patch).
//
// We keep this class out of the public dao_updater_service.h so callers don't
// have to drag in <Foundation/Foundation.h>.
class DaoSparkleUpdaterMac {
 public:
  DaoSparkleUpdaterMac();
  ~DaoSparkleUpdaterMac();

  DaoSparkleUpdaterMac(const DaoSparkleUpdaterMac&) = delete;
  DaoSparkleUpdaterMac& operator=(const DaoSparkleUpdaterMac&) = delete;

  // Constructs the SPUStandardUpdaterController and tells Sparkle to start
  // its scheduled checks. Safe to call before any windows exist; Sparkle
  // schedules its first check off the main run loop.
  void Start();

  // Equivalent to clicking "Check for Updates..." in the app menu — shows
  // user-visible progress UI and an "up to date" sheet if no update is found.
  void CheckForUpdatesUserInitiated();

 private:
  // Strongly-retained SPUStandardUpdaterController. Released in the dtor.
  // We type-erase to a void* slot so this header stays compileable without
  // the Sparkle headers; the .mm casts to the real Objective-C type via
  // ARC `__bridge_retained` / `__bridge_transfer`.
  //
  // RAW_PTR_EXCLUSION: this points to an Objective-C object retained via
  // ARC bridge cast (CFRetain semantics), not to a Chromium-allocated
  // C++ heap object. raw_ptr<T> would mishandle the lifetime model and
  // emit dangling-pointer false positives across the bridge boundary.
  RAW_PTR_EXCLUSION void* controller_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UPDATER_DAO_SPARKLE_UPDATER_MAC_H_
