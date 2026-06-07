// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UPDATER_DAO_UPDATER_SERVICE_H_
#define DAO_BROWSER_UPDATER_DAO_UPDATER_SERVICE_H_

#include <memory>
#include <string>

#include "base/functional/callback.h"
#include "base/no_destructor.h"
#include "base/observer_list_types.h"

namespace dao {

enum class DaoUpdateState {
  kIdle,
  kReady,
  kApplying,
  kUnsupported,
};

struct DaoUpdateStatus {
  DaoUpdateState state = DaoUpdateState::kIdle;
  std::string display_version;
};

class DaoUpdaterServiceObserver : public base::CheckedObserver {
 public:
  virtual void OnDaoUpdateStatusChanged(const DaoUpdateStatus& status) = 0;
};

// Process-wide auto-update facade. Wraps the platform-specific updater
// implementation (Sparkle on macOS). Browser code should never #include the
// platform impl directly.
//
// Threading: all methods must be called on the UI thread.
//
// Lifetime: a single instance lives for the duration of the process. It is
// created lazily on first GetInstance() call and torn down at process exit.
//
// Typical use:
//   - From the post-startup hook (e.g. ChromeBrowserMainPartsMac):
//       DaoUpdaterService::GetInstance()->Init();
//   - From the "Check for Updates..." menu command:
//       DaoUpdaterService::GetInstance()->CheckForUpdatesUserInitiated();
class DaoUpdaterService {
 public:
  static DaoUpdaterService* GetInstance();

  DaoUpdaterService(const DaoUpdaterService&) = delete;
  DaoUpdaterService& operator=(const DaoUpdaterService&) = delete;

  // Starts the platform updater. Idempotent: a second call is a no-op. Safe to
  // call before any browser window exists. On macOS this constructs the
  // SPUStandardUpdaterController, asks Sparkle to begin its scheduled
  // background checks, and performs one silent startup check.
  void Init();

  // Triggers a user-visible update check (shows progress UI even if no update
  // is available). Wired to the menu command. No-op if Init() hasn't run.
  void CheckForUpdatesUserInitiated();

  // True iff the platform supports auto-update in this build configuration.
  // On macOS this is always true; provided as a hook for future platforms or
  // for developer builds that want to disable the updater entirely.
  bool IsSupported() const;

  void AddObserver(DaoUpdaterServiceObserver* observer);
  void RemoveObserver(DaoUpdaterServiceObserver* observer);

  DaoUpdateStatus GetUpdateStatus() const;
  bool ApplyReadyUpdate();

  void SetReadyUpdateForTesting(std::string display_version,
                                base::OnceClosure install_callback);
  // Test setup/cleanup hook. This intentionally resets silently without
  // notifying observers; tests with live observers should remove them first or
  // request the current state again after reset.
  void ResetForTesting();

 private:
  friend class base::NoDestructor<DaoUpdaterService>;

  DaoUpdaterService();
  ~DaoUpdaterService();

  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UPDATER_DAO_UPDATER_SERVICE_H_
