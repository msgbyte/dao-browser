// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_
#define DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_

#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"

namespace base {
class SequencedTaskRunner;
}

namespace dao {

scoped_refptr<base::SequencedTaskRunner> GetFolderFileTaskRunner();
std::optional<std::string> MergeFolderData(const std::string& base_json,
                                          const std::string& json,
                                          const std::string& current_json);
std::optional<std::string> ReadFolderFile(const base::FilePath& path);
// Apply only the changes from base_json to json to the latest profile file.
// Invalid/unreadable data fails closed, preserving the previous file.
bool UpdateFolderFile(const base::FilePath& path,
                      const std::string& base_json,
                      const std::string& json);

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_
