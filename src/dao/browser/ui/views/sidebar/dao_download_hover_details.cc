// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_download_hover_details.h"

#include <algorithm>

#include "base/i18n/number_formatting.h"
#include "chrome/grit/generated_resources.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/l10n/time_format.h"
#include "ui/base/text/bytes_formatting.h"

namespace dao {

namespace {

std::u16string JoinValueAndDetail(const std::u16string& value,
                                  const std::u16string& detail) {
  if (value.empty()) {
    return detail;
  }
  if (detail.empty()) {
    return value;
  }
  return l10n_util::GetStringFUTF16(IDS_DAO_DOWNLOAD_HOVER_VALUE_WITH_DETAIL,
                                    value, detail);
}

}  // namespace

DownloadHoverDetails BuildDownloadHoverDetails(
    int64_t received_bytes,
    int64_t total_bytes,
    int percent,
    int64_t bytes_per_second,
    std::optional<base::TimeDelta> remaining) {
  DownloadHoverDetails details;
  const uint64_t safe_received =
      static_cast<uint64_t>(std::max<int64_t>(0, received_bytes));
  std::u16string amount = ui::FormatBytes(base::ByteSize(safe_received));
  if (total_bytes > 0) {
    const std::u16string total =
        ui::FormatBytes(base::ByteSize(static_cast<uint64_t>(total_bytes)));
    amount = l10n_util::GetStringFUTF16(IDS_DOWNLOAD_TAB_PROGRESS_SIZE, amount,
                                        total);
  }
  details.size_line = total_bytes > 0 && percent >= 0
                          ? JoinValueAndDetail(amount,
                                               base::FormatPercent(percent))
                          : amount;

  std::u16string speed;
  if (bytes_per_second > 0) {
    speed = ui::FormatSpeed(
        base::ByteSize(static_cast<uint64_t>(bytes_per_second)));
  }
  std::u16string time_remaining;
  if (remaining.has_value() && remaining->is_positive()) {
    time_remaining =
        ui::TimeFormat::Simple(ui::TimeFormat::FORMAT_REMAINING,
                               ui::TimeFormat::LENGTH_SHORT, *remaining);
  }
  details.status_line = JoinValueAndDetail(speed, time_remaining);
  return details;
}

}  // namespace dao
