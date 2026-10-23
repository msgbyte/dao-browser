// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_MCP_DAO_LOCAL_AGENT_INTEGRATION_H_
#define DAO_BROWSER_MCP_DAO_LOCAL_AGENT_INTEGRATION_H_

#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/values.h"

namespace dao {

// Entry skill with this app's CLI path as a fallback for agents without
// ~/.local/bin on PATH.
std::string BuildDaoEntrySkill(const base::FilePath& helper_path,
                              bool lite = false);

// Installs or removes what local agents need to find Dao: the
// ~/.local/bin/dao-browser link to the CLI inside this app, and the entry
// skill in the skill folders of agents found under `home`. Only files Dao
// created are updated or removed; a user's own files are left alone.
// Blocking; call on a MayBlock sequence.
void UpdateDaoLocalAgentIntegration(const base::FilePath& home,
                                    const base::FilePath& helper_path,
                                    bool enabled);

// Posts UpdateDaoLocalAgentIntegration() for this app and the user's home.
// Does nothing in test processes.
void ScheduleDaoLocalAgentIntegrationUpdate(bool enabled);

// What Dao installed for each local agent it knows, for the settings page:
// `{agents: [{id, detected, installed}], cliLinked}`. Blocking.
base::DictValue GetDaoLocalAgentIntegrationStatus(
    const base::FilePath& home,
    const base::FilePath& helper_path);

// Reads the status after any pending update for this app and home.
void RequestDaoLocalAgentIntegrationStatus(
    base::OnceCallback<void(base::DictValue)> callback);

}  // namespace dao

#endif  // DAO_BROWSER_MCP_DAO_LOCAL_AGENT_INTEGRATION_H_
