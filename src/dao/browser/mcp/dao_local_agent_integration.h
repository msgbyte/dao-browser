// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_LOCAL_AGENT_INTEGRATION_H_
#define DAO_BROWSER_MCP_DAO_LOCAL_AGENT_INTEGRATION_H_

#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/values.h"

namespace dao {

// Entry skill with this app's CLI path as a fallback for agents without
// ~/.local/bin on PATH.
std::string BuildDaoEntrySkill(const base::FilePath& helper_path,
                              bool lite = false);

// Explicitly installs/updates or removes one integration: shared, claude, cli.
// Only Dao-owned files are changed. Returns false on conflicts or I/O errors.
// Blocking; call on a MayBlock sequence.
bool UpdateDaoLocalAgentIntegration(const base::FilePath& home,
                                   const base::FilePath& helper_path,
                                   std::string_view id,
                                   bool installed);

// Performs the requested operation and returns status with a success field.
// Does not touch the developer's home in test processes.
void RequestDaoLocalAgentIntegrationUpdate(
    std::string id,
    bool installed,
    base::OnceCallback<void(base::DictValue)> callback);

// Read-only status: {agents: [{id, detected, installed, needsUpdate}],
// cliLinked, cliNeedsUpdate}. Blocking.
base::DictValue GetDaoLocalAgentIntegrationStatus(
    const base::FilePath& home,
    const base::FilePath& helper_path);

// Reads the status after any pending update for this app and home.
void RequestDaoLocalAgentIntegrationStatus(
    base::OnceCallback<void(base::DictValue)> callback);

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_LOCAL_AGENT_INTEGRATION_H_
