// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_file_snapshot.h"

#include <array>
#include <cstdint>
#include <set>

#include "base/base64.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "crypto/hash.h"

namespace dao {

bool IsValidHomeSnapshotPath(std::string_view path) {
  if (path.empty() || path.size() > 240 || !base::IsStringUTF8(path) ||
      path.find_first_of("\\:") != std::string_view::npos) {
    return false;
  }
#if BUILDFLAG(IS_WIN)
  if (path.find_first_of("*?\"<>|") != std::string_view::npos) {
    return false;
  }
#endif
  for (const unsigned char byte : path) {
    if (byte < 32 || byte == 127) {
      return false;
    }
  }
  for (std::string_view part : base::SplitStringPiece(
           path, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (part.empty() || part == "." || part == "..") {
      return false;
    }
#if BUILDFLAG(IS_WIN)
    if (part.back() == '.' || part.back() == ' ') {
      return false;
    }
    const std::string name = base::ToLowerASCII(part.substr(0, part.find('.')));
    if (name == "con" || name == "prn" || name == "aux" || name == "nul" ||
        (name.size() == 4 &&
         (name.starts_with("com") || name.starts_with("lpt")) &&
         name[3] >= '0' && name[3] <= '9')) {
      return false;
    }
#endif
  }
  return true;
}

base::expected<void, HomeError> ValidateHomeFileSnapshot(
    const HomeFileSnapshot& files) {
  if (files.size() > kMaxHomeSnapshotFiles) {
    return base::unexpected(HomeError::kQuotaExceeded);
  }
  size_t bytes = 0;
  std::set<std::string> paths;
  std::map<std::string, std::string> spellings;
  for (const auto& [path, contents] : files) {
    if (!IsValidHomeSnapshotPath(path) ||
        !paths.insert(base::ToLowerASCII(path)).second) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    for (size_t end = path.find('/');; end = path.find('/', end + 1)) {
      const std::string prefix = path.substr(0, end);
      const auto [found, inserted] =
          spellings.emplace(base::ToLowerASCII(prefix), prefix);
      if (!inserted && found->second != prefix) {
        return base::unexpected(HomeError::kInvalidPath);
      }
      if (end == std::string::npos) {
        break;
      }
    }
    if (contents.size() > kMaxHomeSnapshotBytes - bytes) {
      return base::unexpected(HomeError::kQuotaExceeded);
    }
    bytes += contents.size();
  }
  for (const std::string& path : paths) {
    for (size_t slash = path.find('/'); slash != std::string::npos;
         slash = path.find('/', slash + 1)) {
      if (paths.contains(path.substr(0, slash))) {
        return base::unexpected(HomeError::kInvalidPath);
      }
    }
  }
  return base::ok();
}

std::string HomeFileSnapshotDigest(const HomeFileSnapshot& files) {
  crypto::hash::Hasher hasher(crypto::hash::kSha256);
  for (const auto& [path, contents] : files) {
    for (std::string_view value :
         {std::string_view(path), std::string_view(contents)}) {
      std::array<uint8_t, 8> length;
      uint64_t size = value.size();
      for (size_t index = length.size(); index > 0; --index) {
        length[index - 1] = size & 0xff;
        size >>= 8;
      }
      hasher.Update(length);
      hasher.Update(value);
    }
  }
  std::array<uint8_t, crypto::hash::kSha256Size> digest;
  hasher.Finish(digest);
  return base::ToLowerASCII(base::HexEncode(digest));
}

base::DictValue EncodeHomeFileSnapshot(const HomeFileSnapshot& files) {
  base::DictValue encoded;
  for (const auto& [path, contents] : files) {
    encoded.Set(path, base::Base64Encode(contents));
  }
  return encoded;
}

base::expected<HomeFileSnapshot, HomeError> DecodeHomeFileSnapshot(
    const base::DictValue& encoded_files) {
  if (encoded_files.size() > kMaxHomeSnapshotFiles) {
    return base::unexpected(HomeError::kQuotaExceeded);
  }
  HomeFileSnapshot files;
  size_t bytes = 0;
  for (const auto [path, value] : encoded_files) {
    if (!IsValidHomeSnapshotPath(path)) {
      return base::unexpected(HomeError::kInvalidPath);
    }
    const std::string* encoded = value.GetIfString();
    if (!encoded) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    if (encoded->size() > ((kMaxHomeSnapshotBytes - bytes + 2) / 3) * 4) {
      return base::unexpected(HomeError::kQuotaExceeded);
    }
    std::string decoded;
    if (!base::Base64Decode(*encoded, &decoded)) {
      return base::unexpected(HomeError::kInvalidArgument);
    }
    if (decoded.size() > kMaxHomeSnapshotBytes - bytes) {
      return base::unexpected(HomeError::kQuotaExceeded);
    }
    bytes += decoded.size();
    files.emplace(path, std::move(decoded));
  }
  auto valid = ValidateHomeFileSnapshot(files);
  if (!valid.has_value()) {
    return base::unexpected(valid.error());
  }
  return files;
}

}  // namespace dao
