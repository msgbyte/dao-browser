// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_COMMON_HOME_STORAGE_READS_H_
#define DAO_COMMON_HOME_STORAGE_READS_H_

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <optional>
#include <vector>

namespace dao {

// An empty vector means no keys were read. A missing vector means unknown.
using HomeStorageReads = std::optional<std::vector<uint32_t>>;
inline constexpr size_t kMaxHomeStorageReadKeys = 256;

inline bool AddHomeStorageRead(HomeStorageReads& into,
                               std::optional<uint32_t> key) {
  if (!into || (key && std::ranges::contains(*into, *key))) {
    return false;
  }
  if (!key || into->size() >= kMaxHomeStorageReadKeys) {
    into.reset();
  } else {
    into->push_back(*key);
  }
  return true;
}

inline void MergeHomeStorageReads(HomeStorageReads& into,
                                  const HomeStorageReads& other) {
  if (!into || !other || into->size() > kMaxHomeStorageReadKeys ||
      other->size() > kMaxHomeStorageReadKeys) {
    into.reset();
    return;
  }
  for (uint32_t key : *other) {
    AddHomeStorageRead(into, key);
    if (!into) {
      break;
    }
  }
}

}  // namespace dao

#endif  // DAO_COMMON_HOME_STORAGE_READS_H_
