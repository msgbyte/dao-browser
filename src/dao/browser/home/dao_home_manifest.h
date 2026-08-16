// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_MANIFEST_H_
#define DAO_BROWSER_HOME_DAO_HOME_MANIFEST_H_

#include <string>
#include <string_view>

#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_types.h"

namespace dao {

base::expected<HomeManifest, HomeError> ParseHomeManifest(
    std::string_view json);
base::DictValue HomeManifestToValue(const HomeManifest& manifest);

bool IsValidHomeRelativePath(std::string_view path);
bool IsHomeRoute(std::string_view path);
std::string HomeConnectorPermissionFingerprint(
    const HomeConnectorPermission& permission);
std::string HomeConnectorGrantFingerprint(
    const HomeConnectorPermission& permission,
    const HomeLimits& limits);
std::string HomeConnectorFingerprint(const HomeConnector& connector,
                                     const HomeLimits& limits,
                                     std::string_view module_source,
                                     std::string_view schema_source);

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_MANIFEST_H_
