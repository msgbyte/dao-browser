// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/extensions/legacy_mv2/dao_mv2_install_notice.h"

#include "dao/browser/strings/grit/dao_strings.h"
#include "extensions/common/extension.h"
#include "ui/base/l10n/l10n_util.h"

namespace dao {

// static
bool DaoMV2InstallNotice::ShouldShowLegacyMV2Notice(
    const extensions::Extension* extension) {
  if (!extension) {
    return false;
  }
  return extension->manifest_version() == 2;
}

// static
std::u16string DaoMV2InstallNotice::GetLegacyMV2NoticeText() {
  return l10n_util::GetStringUTF16(IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE);
}

}  // namespace dao
