// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_INSTALL_NOTICE_H_
#define DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_INSTALL_NOTICE_H_

#include <string>

namespace extensions {
class Extension;
}

namespace dao {

class DaoMV2InstallNotice {
 public:
  DaoMV2InstallNotice() = delete;

  // True iff the extension is a Manifest V2 extension (manifest_version == 2)
  // and therefore should display the legacy-MV2 notice in the install dialog.
  // Returns false for null pointers and for unknown manifest versions.
  static bool ShouldShowLegacyMV2Notice(const extensions::Extension* extension);

  // Returns the localized notice text for the install dialog. Bound to
  // IDS_DAO_INSTALL_PROMPT_LEGACY_MV2_NOTICE.
  static std::u16string GetLegacyMV2NoticeText();
};

}  // namespace dao

#endif  // DAO_BROWSER_EXTENSIONS_LEGACY_MV2_DAO_MV2_INSTALL_NOTICE_H_
