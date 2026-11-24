// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_COMMON_HOME_LOCAL_STORAGE_H_
#define DAO_COMMON_HOME_LOCAL_STORAGE_H_

#include <stdint.h>

#include <bit>
#include <optional>

#include "base/containers/span.h"
#include "base/token.h"
#include "dao/common/home_storage_reads.h"
#include "third_party/blink/public/platform/web_common.h"

namespace blink {
class WebLocalFrame;
}

namespace dao {

// FNV-1a over the complete UTF-16LE key, without allocating a converted string.
inline uint32_t HomeLocalStorageStringHash(base::span<const uint8_t> payload,
                                           bool is_latin1) {
  static_assert(std::endian::native == std::endian::little);
  constexpr uint32_t kFnvPrime = 16777619u;
  uint32_t hash = 2166136261u;
  for (uint8_t byte : payload) {
    hash = (hash ^ byte) * kFnvPrime;
    if (is_latin1) {
      hash *= kFnvPrime;
    }
  }
  return hash;
}

// CachedStorageArea prefixes local storage keys with 0 (UTF-16) or 1 (Latin-1).
inline std::optional<uint32_t> HomeLocalStorageKeyHash(
    base::span<const uint8_t> encoded_key) {
  if (encoded_key.empty()) {
    return std::nullopt;
  }
  const auto [format, payload] = encoded_key.split_at<1>();
  if (format[0] > 1 || (format[0] == 0 && payload.size() % 2 != 0)) {
    return std::nullopt;
  }
  return HomeLocalStorageStringHash(payload, format[0] == 1);
}

BLINK_MODULES_EXPORT base::Token GetHomeLocalStorageSourceId(
    blink::WebLocalFrame* frame);
BLINK_MODULES_EXPORT void EnableHomeLocalStorageReadTracking(
    blink::WebLocalFrame* frame);
BLINK_MODULES_EXPORT HomeStorageReads
GetHomeLocalStorageReadKeys(blink::WebLocalFrame* frame);

BLINK_MODULES_EXPORT void MarkHomeLocalStorageDocumentReplaced(
    blink::WebLocalFrame* frame,
    bool was_initial_document);

}  // namespace dao

#endif  // DAO_COMMON_HOME_LOCAL_STORAGE_H_
