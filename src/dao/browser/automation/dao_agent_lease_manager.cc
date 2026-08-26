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
                             tabs::TabHandle target_handle,
                             uint64_t lease_id)
    : manager_(std::move(manager)),
      target_handle_(target_handle),
      lease_id_(lease_id) {
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
  target_handle_ = std::exchange(other.target_handle_, tabs::TabHandle());
  lease_id_ = std::exchange(other.lease_id_, 0);
}

DaoAgentLease& DaoAgentLease::operator=(DaoAgentLease&& other) noexcept {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  DCHECK_CALLED_ON_VALID_SEQUENCE(other.sequence_checker_);
  if (this != &other) {
    Reset();
    manager_ = std::move(other.manager_);
    target_handle_ = std::exchange(other.target_handle_, tabs::TabHandle());
    lease_id_ = std::exchange(other.lease_id_, 0);
  }
  return *this;
}

void DaoAgentLease::Reset() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const uint64_t lease_id = std::exchange(lease_id_, 0);
  const tabs::TabHandle target_handle =
      std::exchange(target_handle_, tabs::TabHandle());
  base::WeakPtr<DaoAgentLeaseManager> manager = std::move(manager_);
  manager_.reset();
  if (lease_id == 0) {
    return;
  }
  if (manager) {
    manager->Release(target_handle, lease_id);
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
  return TryAcquire(tabs::TabHandle::Null(), std::move(client));
}

base::expected<DaoAgentLease, DaoToolError> DaoAgentLeaseManager::TryAcquire(
    tabs::TabHandle target_handle,
    DaoAgentClientId client) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto existing = target_handle == tabs::TabHandle::Null()
                      ? leases_.begin()
                      : leases_.find(target_handle);
  if (existing == leases_.end() &&
      target_handle != tabs::TabHandle::Null()) {
    existing = leases_.find(tabs::TabHandle::Null());
  }
  if (existing != leases_.end()) {
    const bool dao_agent_blocked_by_mcp =
        client.type == DaoToolClient::kDaoAgent &&
        existing->second.owner.type == DaoToolClient::kMcp;
    return base::unexpected(MakeDaoToolError(
        dao_agent_blocked_by_mcp ? DaoToolErrorCode::kAgentControlBusy
                                 : DaoToolErrorCode::kLeaseBusy,
        "Browser control is already leased to another agent.",
        dao_agent_blocked_by_mcp));
  }

  const uint64_t lease_id = next_lease_id_++;
  leases_.emplace(target_handle,
                  LeaseState{lease_id, std::move(client)});
  return DaoAgentLease(weak_factory_.GetWeakPtr(), target_handle, lease_id);
}

void DaoAgentLeaseManager::Release(tabs::TabHandle target_handle,
                                   uint64_t lease_id) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto existing = leases_.find(target_handle);
  if (existing == leases_.end() || existing->second.lease_id != lease_id) {
    return;
  }
  leases_.erase(existing);
}

}  // namespace dao
