// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_STORAGE_H_
#define DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_STORAGE_H_

#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace dao {

bool WritePinnedTabsFileAtomically(const base::FilePath& file_path,
                                   const std::string& data);
scoped_refptr<base::SequencedTaskRunner> GetPinnedTabsFileTaskRunner();

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_PINNED_TAB_STORAGE_H_
