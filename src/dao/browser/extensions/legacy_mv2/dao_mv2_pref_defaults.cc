// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_pref_defaults.h"

#include "base/feature_list.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/extensions/legacy_mv2/dao_mv2_features.h"

namespace dao {

// static
int DaoMV2PrefDefaults::DefaultManifestV2Availability() {
  return base::FeatureList::IsEnabled(kRestoreManifestV2Deprecation) ? 0 : 2;
}

// static
int DaoMV2PrefDefaults::GetManifestV2Availability(const PrefService* prefs) {
  if (prefs && prefs->IsManagedPreference(kAvailabilityPref)) {
    const int availability = prefs->GetInteger(kAvailabilityPref);
    // Invalid managed values must not accidentally permit legacy extensions.
    return availability >= 0 && availability <= 3 ? availability : 0;
  }
  return DefaultManifestV2Availability();
}

}  // namespace dao
