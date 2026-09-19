// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_
#define DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/scoped_refptr.h"

namespace base {
class SequencedTaskRunner;
}  // namespace base

namespace dao {

struct DaoFolderWindowSnapshot {
  DaoFolderWindowSnapshot();
  ~DaoFolderWindowSnapshot();

  DaoFolderWindowSnapshot(const DaoFolderWindowSnapshot&);
  DaoFolderWindowSnapshot& operator=(const DaoFolderWindowSnapshot&);
  DaoFolderWindowSnapshot(DaoFolderWindowSnapshot&&);
  DaoFolderWindowSnapshot& operator=(DaoFolderWindowSnapshot&&);

  std::string id;
  std::set<std::string> tab_ids;
  std::string json;
  bool legacy = false;
};

class DaoFolderStorage {
 public:
  DaoFolderStorage();
  ~DaoFolderStorage();

  DaoFolderStorage(const DaoFolderStorage&);
  DaoFolderStorage& operator=(const DaoFolderStorage&);
  DaoFolderStorage(DaoFolderStorage&&);
  DaoFolderStorage& operator=(DaoFolderStorage&&);

  bool LoadFromJson(const std::string& json);
  std::string ToJson() const;

  const DaoFolderWindowSnapshot* FindById(const std::string& id) const;
  const DaoFolderWindowSnapshot* FindClaimableSnapshot(
      const std::vector<std::string>& preferred_ids,
      const std::set<std::string>& current_tab_ids,
      const std::set<std::string>& claimed_ids) const;
  void UpsertSnapshot(const std::string& id,
                      std::set<std::string> tab_ids,
                      std::string json);
  void RemoveSnapshot(const std::string& id);

 private:
  std::vector<DaoFolderWindowSnapshot> snapshots_;
};

bool WriteFolderFileAtomically(const base::FilePath& file_path,
                               const std::string& data);
bool ReadFolderFileWithPendingWrite(const base::FilePath& file_path,
                                    std::string* data);
uint64_t ReserveFolderFileWrite(const base::FilePath& file_path,
                                const std::string& data);
bool WriteReservedFolderFileAtomically(const base::FilePath& file_path,
                                       const std::string& data,
                                       uint64_t generation);
scoped_refptr<base::SequencedTaskRunner> GetFolderFileTaskRunner();

}  // namespace dao

#endif  // DAO_BROWSER_UI_WEBUI_DAO_FOLDER_STORAGE_H_
