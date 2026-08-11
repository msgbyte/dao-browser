// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_CHROMIUM_PASSWORD_DECRYPTOR_MAC_H_
#define DAO_BROWSER_IMPORT_DAO_CHROMIUM_PASSWORD_DECRYPTOR_MAC_H_

#include <array>
#include <optional>

#include "dao/browser/import/dao_migration_types.h"
#include "dao/browser/import/dao_source_adapter.h"

namespace dao::import {

class DaoChromiumPasswordDecryptorMac : public PasswordDecryptor {
 public:
  explicit DaoChromiumPasswordDecryptorMac(SourceKind source_kind);
  DaoChromiumPasswordDecryptorMac(const DaoChromiumPasswordDecryptorMac&) =
      delete;
  DaoChromiumPasswordDecryptorMac& operator=(
      const DaoChromiumPasswordDecryptorMac&) = delete;
  ~DaoChromiumPasswordDecryptorMac() override;

  std::optional<std::u16string> Decrypt(
      base::span<const uint8_t> encrypted_value) override;

 private:
  bool LoadKey();

  SourceKind source_kind_;
  std::optional<std::array<uint8_t, 16>> key_;
  bool key_lookup_attempted_ = false;
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_CHROMIUM_PASSWORD_DECRYPTOR_MAC_H_
