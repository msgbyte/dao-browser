// Copyright 2026 Dao Browser Authors. All rights reserved.
#ifndef DAO_BROWSER_UPDATER_DAO_WINDOWS_UPDATER_H_
#define DAO_BROWSER_UPDATER_DAO_WINDOWS_UPDATER_H_

#include <memory>
#include <optional>
#include <string>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "dao/browser/updater/dao_update_manifest.h"
#include "dao/browser/updater/dao_updater_service.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"

namespace dao {
class DaoWindowsUpdater {
 public:
  using StatusCallback = base::RepeatingCallback<void(const DaoUpdateStatus&)>;
  DaoWindowsUpdater();
  ~DaoWindowsUpdater();
  void Start(StatusCallback callback,
             scoped_refptr<network::SharedURLLoaderFactory> factory);
  void CheckForUpdates(bool user_initiated);
  bool ApplyReadyUpdate();
  void Shutdown();
  bool PrepareForExit();

 private:
  friend class DaoWindowsUpdaterTest;
  struct Cache;
  struct Handoff;
  static Cache LoadCache();
  static std::unique_ptr<Handoff> PrepareHandoff(
      base::FilePath installer,
      WindowsUpdateManifest manifest);
  void OnCacheLoaded(Cache cache);
  void ScheduleNextCheck(base::Time last_check);
  void SetState(DaoUpdateState state,
                DaoUpdateError error = DaoUpdateError::kNone);
  void StartRequest(const GURL& url, base::TimeDelta timeout);
  void OnManifest(std::optional<std::string> envelope);
  void DownloadUpdate();
  void OnDownloaded(base::FilePath path);
  void OnVerified(bool valid);
  void OnHandoff(std::unique_ptr<Handoff> handoff);
  void OnClosingBrowsers(bool closing);
  void Fail(DaoUpdateError error);

  StatusCallback callback_;
  scoped_refptr<network::SharedURLLoaderFactory> factory_;
  DaoUpdateStatus status_;
  base::FilePath cache_dir_;
  base::FilePath installer_;
  std::string envelope_;
  std::optional<WindowsUpdateManifest> manifest_;
  std::unique_ptr<network::SimpleURLLoader> loader_;
  std::unique_ptr<Handoff> handoff_;
  base::CallbackListSubscription closing_subscription_;
  base::OneShotTimer timer_;
  base::OneShotTimer handoff_timer_;
  bool supported_ = false;
  bool stopped_ = false;
  bool exit_confirmed_ = false;
  base::WeakPtrFactory<DaoWindowsUpdater> weak_factory_{this};
};
}  // namespace dao
#endif  // DAO_BROWSER_UPDATER_DAO_WINDOWS_UPDATER_H_
