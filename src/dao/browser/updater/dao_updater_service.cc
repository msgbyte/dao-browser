// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_updater_service.h"

#include "base/logging.h"
#include "base/no_destructor.h"
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
    sparkle_->Start();
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

 private:
  bool initialized_ = false;

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

}  // namespace dao
