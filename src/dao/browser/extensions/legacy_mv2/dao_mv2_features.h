// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_FEATURES_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_FEATURES_H_

#include "base/feature_list.h"

namespace dao {

// When ENABLED, Dao Browser falls back to Chromium's standard Manifest V2
// deprecation behavior (MV2 extensions are disabled/blocked the same way
// upstream Chrome handles them). When DISABLED (the default), Dao keeps
// MV2 extensions fully enabled and installable.
//
// The flag is intentionally reverse-named: "Default" in chrome://flags means
// Dao behavior (MV2 on), "Enabled" means restoring upstream deprecation.
BASE_DECLARE_FEATURE(kRestoreManifestV2Deprecation);

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_FEATURES_H_
