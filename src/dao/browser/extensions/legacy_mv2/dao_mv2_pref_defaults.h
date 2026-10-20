// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_

class PrefService;

namespace dao {

// Owns the retained MV2 availability preference: 0 follows Chromium,
// 1 disables MV2, 2 allows MV2, and 3 allows forced/recommended installs.
class DaoMV2PrefDefaults {
 public:
  DaoMV2PrefDefaults() = delete;

  // Preserve the existing profile key after Chromium removed its constant.
  static constexpr char kAvailabilityPref[] = "extensions.manifest_v2";

  // The restore-deprecation flag selects 0; Dao's default is 2.
  static int DefaultManifestV2Availability();

  // Only managed values override Dao's default, matching the original policy.
  static int GetManifestV2Availability(const PrefService* prefs);
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_
