// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_API_ROUTER_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_API_ROUTER_H_

#include "base/no_destructor.h"

namespace dao {

// Forward-looking compat seam for Manifest V2 APIs. Every method delegates
// to upstream Chromium today (returns true / pass-through). When a future
// Chromium upgrade removes a specific MV2 capability, that method's body
// becomes the entry point into a Dao-maintained reimplementation. The
// router has zero runtime cost when everything is pass-through and forces
// every future MV2-related fork through one obvious file.
class DaoMV2APIRouter {
 public:
  static DaoMV2APIRouter& Get();

  DaoMV2APIRouter(const DaoMV2APIRouter&) = delete;
  DaoMV2APIRouter& operator=(const DaoMV2APIRouter&) = delete;

  // Returns true if MV2 extensions' `chrome.webRequest` listeners can still
  // block / cancel / redirect requests upstream. Pass-through today.
  bool WebRequestBlockingEnabled() const;

  // Returns true if MV2 persistent background pages persist across tab
  // navigation upstream. Pass-through today.
  bool BackgroundPagePersistenceEnabled() const;

 private:
  friend class base::NoDestructor<DaoMV2APIRouter>;
  DaoMV2APIRouter();
  ~DaoMV2APIRouter();
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_API_ROUTER_H_
