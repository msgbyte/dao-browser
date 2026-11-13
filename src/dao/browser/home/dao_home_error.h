// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_ERROR_H_
#define DAO_BROWSER_HOME_DAO_HOME_ERROR_H_

namespace dao {

enum class HomeError {
  kInvalidArgument,
  kInvalidPath,
  kNotFound,
  kAlreadyExists,
  kUnsupportedFormat,
  kInvalidManifest,
  kInvalidPatch,
  kPatchContextMismatch,
  kQuotaExceeded,
  kStaleRevision,
  kInvalidDraft,
  kPermissionRequired,
  kCancelled,
  kIoError,
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_ERROR_H_
