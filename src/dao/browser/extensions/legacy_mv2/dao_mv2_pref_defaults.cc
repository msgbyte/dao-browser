// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"

#include "base/feature_list.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"

namespace dao {

// static
int DaoMV2PrefDefaults::DefaultManifestV2Availability() {
  // Values mirror `internal::GlobalSettings::ManifestV2Setting`:
  //   0 = kDefault, 1 = kDisabled, 2 = kEnabled, 3 = kEnabledForForceInstalled.
  // We use kDefault (0) for "restore Chromium deprecation" and kEnabled (2)
  // for "Dao default: MV2 fully exempted".
  return base::FeatureList::IsEnabled(kRestoreManifestV2Deprecation) ? 0 : 2;
}

}  // namespace dao
