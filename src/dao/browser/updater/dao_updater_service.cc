// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_updater_service.h"

#include <cstdint>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/observer_list.h"
#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#include "dao/browser/updater/dao_sparkle_updater_mac.h"
#endif

namespace dao {

// -----------------------------------------------------------------------------
// Impl: a platform-specific holder. The header forward-declares this class so
// the .h doesn't drag in <Foundation/Foundation.h>.
// -----------------------------------------------------------------------------

class DaoUpdaterService::Impl {
 public:
  Impl() = default;
  ~Impl() = default;

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void Init() {
    if (initialized_) {
      return;
    }
    initialized_ = true;
#if BUILDFLAG(IS_MAC)
    sparkle_ = std::make_unique<DaoSparkleUpdaterMac>();
    sparkle_->Start(
        base::BindRepeating(&DaoUpdaterService::Impl::SetReadyUpdate,
                            base::Unretained(this)),
        base::BindRepeating(&DaoUpdaterService::Impl::OnUpdateSessionFinished,
                            base::Unretained(this)));
#else
    LOG(WARNING) << "DaoUpdaterService: auto-update not supported on this "
                    "platform (compiled without Sparkle).";
#endif
  }

  void CheckForUpdatesUserInitiated() {
    if (!initialized_) {
      LOG(WARNING) << "DaoUpdaterService: CheckForUpdates before Init(); "
                      "ignoring.";
      return;
    }
#if BUILDFLAG(IS_MAC)
    if (sparkle_) {
      sparkle_->CheckForUpdatesUserInitiated();
    }
#endif
  }

  void AddObserver(DaoUpdaterServiceObserver* observer) {
    observers_.AddObserver(observer);
  }

  void RemoveObserver(DaoUpdaterServiceObserver* observer) {
    observers_.RemoveObserver(observer);
  }

  DaoUpdateStatus GetUpdateStatus() const {
#if BUILDFLAG(IS_MAC)
    return update_status_;
#else
    DaoUpdateStatus status;
    status.state = DaoUpdateState::kUnsupported;
    return status;
#endif
  }

  void SetReadyUpdate(std::string display_version,
                      base::OnceClosure install_callback) {
    DCHECK(!install_callback.is_null());
#if !BUILDFLAG(IS_MAC)
    (void)display_version;
    (void)install_callback;
    return;
#else
    if (install_callback.is_null()) {
      ClearReadyUpdate();
      return;
    }

    ready_install_callback_ = std::move(install_callback);
    update_status_.state = DaoUpdateState::kReady;
    update_status_.display_version = std::move(display_version);
    ++update_status_generation_;
    NotifyUpdateStatusChanged();
#endif
  }

  void ClearReadyUpdate() {
    ResetUpdateStatus();
    NotifyUpdateStatusChanged();
  }

  void OnUpdateSessionFinished() {
#if BUILDFLAG(IS_MAC)
    if (update_status_.state == DaoUpdateState::kIdle ||
        update_status_.state == DaoUpdateState::kReady) {
      return;
    }
    ClearReadyUpdate();
#endif
  }

  bool ApplyReadyUpdate() {
#if !BUILDFLAG(IS_MAC)
    return false;
#else
    if (update_status_.state != DaoUpdateState::kReady) {
      return false;
    }

    if (ready_install_callback_.is_null()) {
      ClearReadyUpdate();
      return false;
    }

    base::OnceClosure callback = std::move(ready_install_callback_);
    update_status_.state = DaoUpdateState::kApplying;
    const uint64_t applying_generation = ++update_status_generation_;
    NotifyUpdateStatusChanged();

    std::move(callback).Run();

    if (update_status_generation_ == applying_generation &&
        update_status_.state == DaoUpdateState::kApplying &&
        ready_install_callback_.is_null()) {
      ClearReadyUpdate();
    }
    return true;
#endif
  }

  void ResetForTesting() {
    ResetUpdateStatus();
  }

  // Test setup/cleanup hook: intentionally silent. Tests with live observers
  // should remove them first or request the current state again after reset.
  void ResetUpdateStatus() {
    ready_install_callback_.Reset();
    update_status_ = DaoUpdateStatus();
    ++update_status_generation_;
  }

  void NotifyUpdateStatusChanged() {
    const uint64_t notification_generation = update_status_generation_;
    const DaoUpdateStatus status = GetUpdateStatus();
    for (DaoUpdaterServiceObserver& observer : observers_) {
      observer.OnDaoUpdateStatusChanged(status);
      if (update_status_generation_ != notification_generation) {
        return;
      }
    }
  }

 private:
  bool initialized_ = false;
  DaoUpdateStatus update_status_;
  base::OnceClosure ready_install_callback_;
  base::ObserverList<
      DaoUpdaterServiceObserver,
      false,
      base::ObserverListReentrancyPolicy::kAllowReentrancy> observers_;
  uint64_t update_status_generation_ = 0;

#if BUILDFLAG(IS_MAC)
  std::unique_ptr<DaoSparkleUpdaterMac> sparkle_;
#endif
};

// -----------------------------------------------------------------------------
// DaoUpdaterService
// -----------------------------------------------------------------------------

// static
DaoUpdaterService* DaoUpdaterService::GetInstance() {
  static base::NoDestructor<DaoUpdaterService> instance;
  return instance.get();
}

DaoUpdaterService::DaoUpdaterService() : impl_(std::make_unique<Impl>()) {}
DaoUpdaterService::~DaoUpdaterService() = default;

void DaoUpdaterService::Init() {
  impl_->Init();
}

void DaoUpdaterService::CheckForUpdatesUserInitiated() {
  impl_->CheckForUpdatesUserInitiated();
}

bool DaoUpdaterService::IsSupported() const {
#if BUILDFLAG(IS_MAC)
  return true;
#else
  return false;
#endif
}

void DaoUpdaterService::AddObserver(DaoUpdaterServiceObserver* observer) {
  impl_->AddObserver(observer);
}

void DaoUpdaterService::RemoveObserver(DaoUpdaterServiceObserver* observer) {
  impl_->RemoveObserver(observer);
}

DaoUpdateStatus DaoUpdaterService::GetUpdateStatus() const {
  return impl_->GetUpdateStatus();
}

bool DaoUpdaterService::ApplyReadyUpdate() {
  return impl_->ApplyReadyUpdate();
}

void DaoUpdaterService::SetReadyUpdateForTesting(
    std::string display_version,
    base::OnceClosure install_callback) {
  impl_->SetReadyUpdate(std::move(display_version),
                        std::move(install_callback));
}

void DaoUpdaterService::ResetForTesting() {
  impl_->ResetForTesting();
}

}  // namespace dao
