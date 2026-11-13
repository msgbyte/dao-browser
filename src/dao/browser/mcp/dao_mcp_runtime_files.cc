// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_mcp_runtime_files.h"

namespace dao {

DaoMcpRuntimeFiles::DaoMcpRuntimeFiles(base::FilePath user_data_dir,
                                      base::ProcessId browser_pid)
    : files_(base::MakeRefCounted<DaoLocalIpcRuntimeFiles>(
          std::move(user_data_dir), browser_pid)) {}

DaoMcpRuntimeFiles::~DaoMcpRuntimeFiles() = default;

}  // namespace dao
