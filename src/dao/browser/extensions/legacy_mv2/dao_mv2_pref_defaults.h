// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_

namespace dao {

// Single-point owner of the Dao-controlled default value for the
// `extensions.manifest_v2.availability` pref (the integer encoding of
// `internal::GlobalSettings::ManifestV2Setting`).
//
// Returns 2 (= ManifestV2Setting::kEnabled, "all MV2 extensions allowed,
// exempt from deprecation") when `kRestoreManifestV2Deprecation` is OFF
// (Dao default). Returns 0 (= ManifestV2Setting::kDefault, "Chromium's
// standard deprecation behavior") when the feature is ON.
//
// Safe to call before `FeatureList` is initialized: `IsEnabled` returns
// the feature's static default (false) in that window, which gives the
// Dao default — the desired pre-init behavior.
class DaoMV2PrefDefaults {
 public:
  DaoMV2PrefDefaults() = delete;

  static int DefaultManifestV2Availability();
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_PREF_DEFAULTS_H_
