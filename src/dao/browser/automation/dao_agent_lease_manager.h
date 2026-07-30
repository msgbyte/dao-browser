// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_AGENT_LEASE_MANAGER_H_
#define DAO_BROWSER_AUTOMATION_DAO_AGENT_LEASE_MANAGER_H_

#include <cstdint>
#include <optional>

#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/supports_user_data.h"
#include "base/types/expected.h"
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

  DaoAgentLease(base::WeakPtr<DaoAgentLeaseManager> manager, uint64_t lease_id);

  base::WeakPtr<DaoAgentLeaseManager> manager_;
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

  base::expected<DaoAgentLease, DaoToolError> TryAcquire(
      DaoAgentClientId client);

 private:
  friend class DaoAgentLease;

  void Release(uint64_t lease_id);

  uint64_t next_lease_id_ = 1;
  uint64_t active_lease_id_ = 0;
  std::optional<DaoAgentClientId> owner_;
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoAgentLeaseManager> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_AGENT_LEASE_MANAGER_H_
