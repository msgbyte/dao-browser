// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_update_manifest.h"

#include <array>
#include "base/base64.h"
#include "base/files/file.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "crypto/hash.h"
#include "crypto/keypair.h"
#include "crypto/sign.h"

namespace dao {
WindowsUpdateManifest::WindowsUpdateManifest() = default;
WindowsUpdateManifest::~WindowsUpdateManifest() = default;
WindowsUpdateManifest::WindowsUpdateManifest(const WindowsUpdateManifest&) =
    default;
WindowsUpdateManifest& WindowsUpdateManifest::operator=(
    const WindowsUpdateManifest&) = default;
WindowsUpdateManifest::WindowsUpdateManifest(WindowsUpdateManifest&&) = default;
WindowsUpdateManifest& WindowsUpdateManifest::operator=(
    WindowsUpdateManifest&&) = default;
namespace {
WindowsUpdateManifestResult Parse(std::string_view envelope,
                                  const base::Version& current_dao,
                                  const base::Version& current_chromium,
                                  std::string_view public_key) {
  const auto invalid = base::unexpected(WindowsUpdateManifestError::kInvalid);
  if (envelope.size() > kWindowsUpdateManifestLimit || !current_dao.IsValid() ||
      !current_chromium.IsValid())
    return invalid;
  auto outer = base::JSONReader::ReadDict(envelope, base::JSON_PARSE_RFC);
  if (!outer || outer->size() != 2)
    return invalid;
  const auto* payload = outer->FindString("payload");
  const auto* signature_text = outer->FindString("signature");
  std::string key_bytes, signature;
  if (!payload || !signature_text ||
      !base::Base64Decode(public_key, &key_bytes) || key_bytes.size() != 32 ||
      !base::Base64Decode(*signature_text, &signature) ||
      signature.size() != 64 ||
      base::Base64Encode(signature) != *signature_text)
    return invalid;
  const auto key = crypto::keypair::PublicKey::FromEd25519PublicKey(
      base::as_byte_span(key_bytes).first<32>());
  if (!crypto::sign::Verify(crypto::sign::ED25519, key,
                            base::as_byte_span(*payload),
                            base::as_byte_span(signature)))
    return invalid;
  auto value = base::JSONReader::ReadDict(*payload, base::JSON_PARSE_RFC);
  if (!value || value->size() != 8 || value->FindInt("schema") != 1 ||
      !value->FindString("platform") ||
      *value->FindString("platform") != "windows" ||
      !value->FindString("arch") || *value->FindString("arch") != "x64")
    return invalid;
  const auto* version = value->FindString("version");
  const auto* chromium = value->FindString("chromiumVersion");
  const auto* url = value->FindString("url");
  const auto* hash = value->FindString("sha256");
  const auto size = value->FindInt("size");
  if (!version || !chromium || !url || !hash || !size || *size <= 0 ||
      *size > kWindowsUpdateInstallerLimit || hash->size() != 64 ||
      hash->find_first_not_of("0123456789abcdef") != std::string::npos)
    return invalid;
  base::Version next(*version), engine(*chromium);
  if (!next.IsValid() || next.components().size() != 3 ||
      next.GetString() != *version || !engine.IsValid() ||
      engine.components().size() != 4 || engine.GetString() != *chromium ||
      engine < current_chromium ||
      *url != std::string(kWindowsUpdateOrigin) + "/dao-browser-" + *version +
                  "-windows-x64.exe")
    return invalid;
  if (next <= current_dao)
    return base::unexpected(WindowsUpdateManifestError::kNoUpdate);
  WindowsUpdateManifest result;
  result.version = *version;
  result.chromium_version = *chromium;
  result.url = GURL(*url);
  result.size = *size;
  result.sha256 = *hash;
  return result;
}
}  // namespace

WindowsUpdateManifestResult ParseSignedWindowsUpdate(
    std::string_view envelope,
    const base::Version& current_dao,
    const base::Version& current_chromium) {
  return Parse(envelope, current_dao, current_chromium,
               kWindowsUpdatePublicKey);
}

WindowsUpdateManifestResult ParseSignedWindowsUpdateWithKeyForTesting(
    std::string_view envelope,
    const base::Version& current_dao,
    const base::Version& current_chromium,
    std::string_view public_key) {
  return Parse(envelope, current_dao, current_chromium, public_key);
}

bool IsWindowsUpdateCacheFileName(std::string_view name) {
  if (name.ends_with(".download")) {
    return base::Uuid::ParseLowercase(name.substr(0, name.size() - 9))
        .is_valid();
  }
  constexpr std::string_view prefix = "dao-browser-";
  constexpr std::string_view suffix = "-windows-x64.exe";
  if (!name.starts_with(prefix) || !name.ends_with(suffix) ||
      name.size() <= prefix.size() + suffix.size())
    return false;
  const std::string text(
      name.substr(prefix.size(), name.size() - prefix.size() - suffix.size()));
  const base::Version version(text);
  return version.IsValid() && version.components().size() == 3 &&
         version.GetString() == text;
}

bool VerifyWindowsUpdateFile(const base::FilePath& path,
                             const WindowsUpdateManifest& manifest) {
  base::File file(path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid() || file.GetLength() != manifest.size)
    return false;
  crypto::hash::Hasher hash(crypto::hash::kSha256);
  std::array<uint8_t, 65536> buffer;
  int64_t total = 0;
  while (true) {
    auto count = file.ReadAtCurrentPos(buffer);
    if (!count)
      return false;
    if (*count == 0)
      break;
    total += *count;
    if (total > manifest.size)
      return false;
    hash.Update(base::span(buffer).first(*count));
  }
  std::array<uint8_t, 32> digest;
  hash.Finish(digest);
  return total == manifest.size &&
         base::ToLowerASCII(base::HexEncode(digest)) == manifest.sha256;
}
}  // namespace dao
