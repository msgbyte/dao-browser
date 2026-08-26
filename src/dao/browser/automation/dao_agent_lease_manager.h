// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_AGENT_LEASE_MANAGER_H_
#define DAO_BROWSER_AUTOMATION_DAO_AGENT_LEASE_MANAGER_H_

#include <cstdint>
#include <map>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/supports_user_data.h"
#include "base/types/expected.h"
#include "components/tabs/public/tab_interface.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

class Profile;

namespace dao {

class DaoAgentLeaseManager;

class DaoAgentLease {
 public:
  // Lease ownership must remain on the browser UI sequence.
  ~DaoAgentLease();

  DaoAgentLease(const DaoAgentLease&) = delete;
  DaoAgentLease& operator=(const DaoAgentLease&) = delete;
  DaoAgentLease(DaoAgentLease&& other) noexcept;
  DaoAgentLease& operator=(DaoAgentLease&& other) noexcept;

  void Reset();

 private:
  friend class DaoAgentLeaseManager;

  DaoAgentLease(base::WeakPtr<DaoAgentLeaseManager> manager,
                tabs::TabHandle target_handle,
                uint64_t lease_id);

  base::WeakPtr<DaoAgentLeaseManager> manager_;
  tabs::TabHandle target_handle_;
  uint64_t lease_id_ = 0;
  SEQUENCE_CHECKER(sequence_checker_);
};

class DaoAgentLeaseManager : public base::SupportsUserData::Data {
 public:
  // The manager must be created, used, and destroyed on the browser UI
  // sequence.
  DaoAgentLeaseManager();
  ~DaoAgentLeaseManager() override;

  DaoAgentLeaseManager(const DaoAgentLeaseManager&) = delete;
  DaoAgentLeaseManager& operator=(const DaoAgentLeaseManager&) = delete;

  static DaoAgentLeaseManager* GetForProfile(Profile* profile);

  // Conservatively reserves all tabs. Kept for callers that do not have a
  // concrete browser target.
  base::expected<DaoAgentLease, DaoToolError> TryAcquire(
      DaoAgentClientId client);
  base::expected<DaoAgentLease, DaoToolError> TryAcquire(
      tabs::TabHandle target_handle,
      DaoAgentClientId client);

 private:
  friend class DaoAgentLease;

  struct LeaseState {
    uint64_t lease_id;
    DaoAgentClientId owner;
  };

  void Release(tabs::TabHandle target_handle, uint64_t lease_id);

  uint64_t next_lease_id_ = 1;
  std::map<tabs::TabHandle, LeaseState> leases_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoAgentLeaseManager> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_AGENT_LEASE_MANAGER_H_
