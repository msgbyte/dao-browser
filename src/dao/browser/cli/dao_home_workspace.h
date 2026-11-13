// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_CLI_DAO_HOME_WORKSPACE_H_
#define DAO_BROWSER_CLI_DAO_HOME_WORKSPACE_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_file_snapshot.h"

namespace dao {

// Owns a nonblocking lock and pinned directory handles. No path is adopted from
// the caller: Open accepts only an intact checkout directly under system temp.
class DaoHomeWorkspace {
 public:
  template <typename T>
  using Result = base::expected<T, std::string>;
  static Result<std::unique_ptr<DaoHomeWorkspace>> Create(
      const HomeFileSnapshot& files,
      base::DictValue state);
  static Result<std::unique_ptr<DaoHomeWorkspace>> Open(
      const base::FilePath& path);
  ~DaoHomeWorkspace();

  const base::FilePath& path() const;
  base::DictValue& state() { return state_; }
  Result<HomeFileSnapshot> Capture() const;
  Result<HomeFileSnapshot> ReadBase() const;
  Result<HomeFileSnapshot> ReadSnapshot(const std::string& digest) const;
  Result<void> SaveSnapshot(const HomeFileSnapshot& files);
  Result<void> SaveState();
  Result<void> AdvanceBase(const std::string& revision,
                           const HomeFileSnapshot& files);
  Result<void> RecoverOperation(const base::DictValue& operation);
  Result<bool> IsClean() const;
  Result<void> Clean(bool discard);
  Result<base::FilePath> SaveArtifact(std::string_view extension,
                                      std::string_view bytes);

 private:
  struct Impl;
  explicit DaoHomeWorkspace(std::unique_ptr<Impl> impl);
  void PruneSnapshots();
  std::unique_ptr<Impl> impl_;
  base::DictValue state_;
};

}  // namespace dao
#endif  // DAO_BROWSER_CLI_DAO_HOME_WORKSPACE_H_
