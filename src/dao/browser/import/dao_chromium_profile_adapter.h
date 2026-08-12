// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_CHROMIUM_PROFILE_ADAPTER_H_
#define DAO_BROWSER_IMPORT_DAO_CHROMIUM_PROFILE_ADAPTER_H_

#include <cstdint>
#include <memory>
#include <optional>

#include "base/files/file_path.h"
#include "dao/browser/import/dao_migration_types.h"
#include "dao/browser/import/dao_source_adapter.h"

namespace dao::import {

class DaoChromiumProfileAdapter : public DaoSourceAdapter {
public:
  explicit DaoChromiumProfileAdapter(
      base::FilePath profile_path,
      std::unique_ptr<PasswordDecryptor> password_decryptor = nullptr);
  DaoChromiumProfileAdapter(const DaoChromiumProfileAdapter &) = delete;
  DaoChromiumProfileAdapter &
  operator=(const DaoChromiumProfileAdapter &) = delete;
  ~DaoChromiumProfileAdapter() override;

  ReadBatch<BookmarkEntry> ReadBookmarks() override;
  ReadBatch<HistoryVisit> ReadHistory() override;
  ReadBatch<PasswordEntry> ReadPasswords() override;
  ReadBatch<TabEntry> ReadTabs() override;
  ReadBatch<ExtensionEntry> ReadExtensions() override;

  // Returns the number of source candidates without decrypting passwords.
  // The caller must use a snapshot when counting tabs because the Chromium
  // session reader may rotate session files.
  std::optional<uint64_t> CountCandidates(DataCategory category);

private:
  base::FilePath profile_path_;
  std::unique_ptr<PasswordDecryptor> password_decryptor_;
};

} // namespace dao::import

#endif // DAO_BROWSER_IMPORT_DAO_CHROMIUM_PROFILE_ADAPTER_H_
