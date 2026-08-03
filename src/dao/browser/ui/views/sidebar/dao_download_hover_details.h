// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_HOVER_DETAILS_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_HOVER_DETAILS_H_

#include <cstdint>
#include <optional>
#include <string>

#include "base/time/time.h"

namespace dao {

struct DownloadHoverDetails {
  std::u16string size_line;
  std::u16string status_line;
};

DownloadHoverDetails BuildDownloadHoverDetails(
    int64_t received_bytes,
    int64_t total_bytes,
    int percent,
    int64_t bytes_per_second,
    std::optional<base::TimeDelta> remaining);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_DOWNLOAD_HOVER_DETAILS_H_
