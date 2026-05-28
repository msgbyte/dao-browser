// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_api_router.h"

#include "base/no_destructor.h"

namespace dao {

// static
DaoMV2APIRouter& DaoMV2APIRouter::Get() {
  static base::NoDestructor<DaoMV2APIRouter> instance;
  return *instance;
}

DaoMV2APIRouter::DaoMV2APIRouter() = default;
DaoMV2APIRouter::~DaoMV2APIRouter() = default;

bool DaoMV2APIRouter::WebRequestBlockingEnabled() const {
  // Pass-through: upstream Chromium is in charge of webRequest blocking.
  // When upstream removes this for MV2, switch to a Dao implementation
  // here and update the router-defaults test.
  return true;
}

bool DaoMV2APIRouter::BackgroundPagePersistenceEnabled() const {
  // Pass-through: upstream Chromium is in charge of persistent background
  // pages. Same fork-when-removed contract as above.
  return true;
}

}  // namespace dao
