// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_FILE_SNAPSHOT_H_
#define DAO_BROWSER_HOME_DAO_HOME_FILE_SNAPSHOT_H_

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_error.h"

namespace dao {

using HomeFileSnapshot = std::map<std::string, std::string>;
inline constexpr size_t kMaxHomeSnapshotFiles = 500;
inline constexpr size_t kMaxHomeSnapshotBytes = 20 * 1024 * 1024;
inline constexpr size_t kMaxHomeSummaryBytes = 500;

bool IsValidHomeSnapshotPath(std::string_view path);
base::expected<void, HomeError> ValidateHomeFileSnapshot(
    const HomeFileSnapshot& files);

// SHA-256 over sorted path/byte pairs, with each string preceded by its
// unsigned 64-bit big-endian byte length. The result is lowercase hexadecimal.
std::string HomeFileSnapshotDigest(const HomeFileSnapshot& files);
base::DictValue EncodeHomeFileSnapshot(const HomeFileSnapshot& files);
base::expected<HomeFileSnapshot, HomeError> DecodeHomeFileSnapshot(
    const base::DictValue& encoded_files);

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_FILE_SNAPSHOT_H_
