// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_agent_lease_manager.h"

#include <memory>
#include <utility>

#include "base/check.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/browser_thread.h"

namespace dao {
namespace {

const void* const kDaoAgentLeaseManagerKey = &kDaoAgentLeaseManagerKey;

}  // namespace

DaoAgentLease::DaoAgentLease(base::WeakPtr<DaoAgentLeaseManager> manager,
                             uint64_t lease_id)
    : manager_(std::move(manager)), lease_id_(lease_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

DaoAgentLease::~DaoAgentLease() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  Reset();
}

DaoAgentLease::DaoAgentLease(DaoAgentLease&& other) noexcept {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(other.sequence_checker_);
  manager_ = std::move(other.manager_);
  lease_id_ = std::exchange(other.lease_id_, 0);
}

DaoAgentLease& DaoAgentLease::operator=(DaoAgentLease&& other) noexcept {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(other.sequence_checker_);
  if (this != &other) {
    Reset();
    manager_ = std::move(other.manager_);
    lease_id_ = std::exchange(other.lease_id_, 0);
  }
  return *this;
}

void DaoAgentLease::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t lease_id = std::exchange(lease_id_, 0);
  base::WeakPtr<DaoAgentLeaseManager> manager = std::move(manager_);
  manager_.reset();
  if (lease_id == 0) {
    return;
  }
  if (manager) {
    manager->Release(lease_id);
  }
}

DaoAgentLeaseManager::DaoAgentLeaseManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

DaoAgentLeaseManager::~DaoAgentLeaseManager() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
DaoAgentLeaseManager* DaoAgentLeaseManager::GetForProfile(Profile* profile) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  CHECK(profile);

  auto* manager = static_cast<DaoAgentLeaseManager*>(
      profile->GetUserData(kDaoAgentLeaseManagerKey));
  if (!manager) {
    auto owned_manager = std::make_unique<DaoAgentLeaseManager>();
    manager = owned_manager.get();
    profile->SetUserData(kDaoAgentLeaseManagerKey, std::move(owned_manager));
  }
  DCHECK_CALLED_ON_VALID_SEQUENCE(manager->sequence_checker_);
  return manager;
}

base::expected<DaoAgentLease, DaoToolError> DaoAgentLeaseManager::TryAcquire(
    DaoAgentClientId client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (owner_) {
    const bool dao_agent_blocked_by_mcp =
        client.type == DaoToolClient::kDaoAgent &&
        owner_->type == DaoToolClient::kMcp;
    return base::unexpected(MakeDaoToolError(
        dao_agent_blocked_by_mcp ? DaoToolErrorCode::kAgentControlBusy
                                 : DaoToolErrorCode::kLeaseBusy,
        "Browser control is already leased to another agent.",
        dao_agent_blocked_by_mcp));
  }

  active_lease_id_ = next_lease_id_++;
  owner_ = std::move(client);
  return DaoAgentLease(weak_factory_.GetWeakPtr(), active_lease_id_);
}

void DaoAgentLeaseManager::Release(uint64_t lease_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (lease_id != active_lease_id_) {
    return;
  }
  active_lease_id_ = 0;
  owner_.reset();
}

}  // namespace dao
