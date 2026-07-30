// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/files/file_path.h"
#include "base/path_service.h"
#include "chrome/common/chrome_paths.h"
#include "dao/browser/mcp/helper/dao_mcp_stdio_server.h"

#ifndef DAO_MCP_VERSION
#define DAO_MCP_VERSION "0.0.0.0"
#endif

int main(int argc, char** argv) {
  base::AtExitManager at_exit_manager;
  base::CommandLine::Init(argc, argv);

  base::FilePath user_data_dir;
  const base::CommandLine* command_line =
      base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("user-data-dir")) {
    user_data_dir = command_line->GetSwitchValuePath("user-data-dir");
  } else {
    chrome::RegisterPathProvider();
    if (!base::PathService::Get(chrome::DIR_USER_DATA, &user_data_dir)) {
      return 1;
    }
  }
  if (user_data_dir.empty()) {
    return 1;
  }

  dao::DaoMcpStdioServer server(std::move(user_data_dir), DAO_MCP_VERSION);
  return server.Run();
}
