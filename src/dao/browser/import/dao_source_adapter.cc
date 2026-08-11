// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_source_adapter.h"

namespace dao::import {

BookmarkEntry::BookmarkEntry() = default;
BookmarkEntry::BookmarkEntry(const BookmarkEntry&) = default;
BookmarkEntry& BookmarkEntry::operator=(const BookmarkEntry&) = default;
BookmarkEntry::BookmarkEntry(BookmarkEntry&&) = default;
BookmarkEntry& BookmarkEntry::operator=(BookmarkEntry&&) = default;
BookmarkEntry::~BookmarkEntry() = default;

PasswordEntry::PasswordEntry() = default;
PasswordEntry::PasswordEntry(const PasswordEntry&) = default;
PasswordEntry& PasswordEntry::operator=(const PasswordEntry&) = default;
PasswordEntry::PasswordEntry(PasswordEntry&&) = default;
PasswordEntry& PasswordEntry::operator=(PasswordEntry&&) = default;
PasswordEntry::~PasswordEntry() = default;

}  // namespace dao::import
