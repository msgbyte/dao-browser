// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_SOURCE_ADAPTER_H_
#define DAO_BROWSER_IMPORT_DAO_SOURCE_ADAPTER_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "base/containers/span.h"
#include "base/time/time.h"
#include "url/gurl.h"

namespace dao::import {

struct BookmarkEntry {
  BookmarkEntry();
  BookmarkEntry(const BookmarkEntry&);
  BookmarkEntry& operator=(const BookmarkEntry&);
  BookmarkEntry(BookmarkEntry&&);
  BookmarkEntry& operator=(BookmarkEntry&&);
  ~BookmarkEntry();

  bool in_toolbar = false;
  bool is_folder = false;
  GURL url;
  std::vector<std::u16string> path;
  std::u16string title;
  base::Time creation_time;
};

struct HistoryVisit {
  GURL url;
  std::u16string title;
  base::Time visit_time;
};

struct PasswordEntry {
  PasswordEntry();
  PasswordEntry(const PasswordEntry&);
  PasswordEntry& operator=(const PasswordEntry&);
  PasswordEntry(PasswordEntry&&);
  PasswordEntry& operator=(PasswordEntry&&);
  ~PasswordEntry();

  GURL origin;
  std::u16string username;
  std::u16string password;
  std::string signon_realm;
  base::Time date_created;
};

struct TabEntry {
  GURL url;
  std::u16string title;
  int window_index = 0;
  int tab_index = 0;
  bool pinned = false;
};

struct ExtensionEntry {
  std::string id;
  std::string name;
  bool enabled = false;
};

class PasswordDecryptor {
 public:
  virtual ~PasswordDecryptor() = default;
  virtual std::optional<std::u16string> Decrypt(
      base::span<const uint8_t> encrypted_value) = 0;
};

template <typename T>
struct ReadBatch {
  bool success = false;
  std::string error_code;
  std::vector<T> records;
};

class DaoSourceAdapter {
 public:
  virtual ~DaoSourceAdapter() = default;

  virtual ReadBatch<BookmarkEntry> ReadBookmarks() = 0;
  virtual ReadBatch<HistoryVisit> ReadHistory() = 0;
  virtual ReadBatch<PasswordEntry> ReadPasswords() = 0;
  virtual ReadBatch<TabEntry> ReadTabs() = 0;
  virtual ReadBatch<ExtensionEntry> ReadExtensions() = 0;
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_SOURCE_ADAPTER_H_
