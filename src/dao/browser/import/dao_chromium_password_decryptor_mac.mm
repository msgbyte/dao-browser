// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_chromium_password_decryptor_mac.h"

#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "base/containers/span.h"
#include "base/notreached.h"
#include "base/strings/utf_string_conversions.h"
#include "crypto/aes_cbc.h"
#include "crypto/apple/keychain_v2.h"
#include "crypto/kdf.h"
#include "crypto/subtle_passkey.h"

namespace dao::import {
namespace {

struct KeychainIdentity {
  const char* service;
  const char* account;
};

KeychainIdentity IdentityForSource(SourceKind source_kind) {
  switch (source_kind) {
    case SourceKind::kChrome:
      return {"Chrome Safe Storage", "Chrome"};
    case SourceKind::kArc:
      return {"Arc Safe Storage", "Arc"};
    case SourceKind::kEdge:
      return {"Microsoft Edge Safe Storage", "Microsoft Edge"};
    case SourceKind::kSafari:
    case SourceKind::kFirefox:
      return {nullptr, nullptr};
  }
  NOTREACHED();
}

constexpr auto kSalt =
    std::to_array<uint8_t>({'s', 'a', 'l', 't', 'y', 's', 'a', 'l', 't'});
constexpr auto kIv =
    std::to_array<uint8_t>({' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
                            ' ', ' ', ' ', ' ', ' ', ' '});

}  // namespace

DaoChromiumPasswordDecryptorMac::DaoChromiumPasswordDecryptorMac(
    SourceKind source_kind)
    : source_kind_(source_kind) {}

DaoChromiumPasswordDecryptorMac::~DaoChromiumPasswordDecryptorMac() = default;

std::optional<std::u16string> DaoChromiumPasswordDecryptorMac::Decrypt(
    base::span<const uint8_t> encrypted_value) {
  constexpr auto kVersion = std::to_array<uint8_t>({'v', '1', '0'});
  if (encrypted_value.size() < kVersion.size() ||
      !std::equal(kVersion.begin(), kVersion.end(), encrypted_value.begin()) ||
      !LoadKey()) {
    return std::nullopt;
  }

  std::optional<std::vector<uint8_t>> plaintext = crypto::aes_cbc::Decrypt(
      *key_, kIv, encrypted_value.subspan(kVersion.size()));
  if (!plaintext) {
    return std::nullopt;
  }
  return base::UTF8ToUTF16(std::string(plaintext->begin(), plaintext->end()));
}

bool DaoChromiumPasswordDecryptorMac::LoadKey() {
  if (key_) {
    return true;
  }
  if (key_lookup_attempted_) {
    return false;
  }
  key_lookup_attempted_ = true;

  KeychainIdentity identity = IdentityForSource(source_kind_);
  if (!identity.service || !identity.account) {
    return false;
  }
  auto password = crypto::apple::KeychainV2::GetInstance().FindGenericPassword(
      identity.service, identity.account);
  if (!password.has_value() || password->empty()) {
    return false;
  }

  std::array<uint8_t, 16> derived_key;
  crypto::kdf::Pbkdf2HmacSha1({.iterations = 1003}, *password, kSalt,
                              derived_key, crypto::SubtlePassKey{});
  key_ = derived_key;
  return true;
}

}  // namespace dao::import
