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
enum class FolderEdit { kAdd, kRename, kRemove };
// Settings edit the actual profile folders; removal releases every child tab.
// Names are trimmed, unique ignoring case, and limited to 100 codepoints.
std::optional<std::string> EditFolderData(const std::string& json,
                                          FolderEdit edit,
                                          const std::string& id,
                                          const std::string& name);
// Validates and edits the latest file under the shared storage lock.
std::optional<std::string> EditFolderFile(const base::FilePath& path,
                                          FolderEdit edit,
                                          const std::string& id,
                                          const std::string& name);
std::optional<std::string> ReadFolderFile(const base::FilePath& path);
// Apply only the changes from base_json to json to the latest profile file.
// Invalid/unreadable data fails closed, preserving the previous file.
bool UpdateFolderFile(const base::FilePath& path,
                      const std::string& base_json,
                      const std::string& json);

// Classification commits only if the latest normalized snapshot matches base.
bool UpdateFolderFileIfUnchanged(const base::FilePath& path,
                                 const std::string& base_json,
                                 const std::string& json);

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_
