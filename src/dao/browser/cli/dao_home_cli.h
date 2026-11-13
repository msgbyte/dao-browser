// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef DAO_BROWSER_CLI_DAO_HOME_CLI_H_
#define DAO_BROWSER_CLI_DAO_HOME_CLI_H_

#include <optional>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/files/file_path.h"

namespace dao {
int RunDaoHomeCommand(const base::CommandLine& command_line,
                      const std::vector<std::string>& args,
                      base::FilePath user_data_dir,
                      const std::string& client_name,
                      const std::optional<base::FilePath>& browser_path,
                      const std::string& version);
}  // namespace dao
#endif  // DAO_BROWSER_CLI_DAO_HOME_CLI_H_
