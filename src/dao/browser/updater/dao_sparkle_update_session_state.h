// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UPDATER_DAO_SPARKLE_UPDATE_SESSION_STATE_H_
#define DAO_BROWSER_UPDATER_DAO_SPARKLE_UPDATE_SESSION_STATE_H_

namespace dao {

class DaoSparkleUpdateSessionState {
 public:
  DaoSparkleUpdateSessionState() = default;

  void OnStandardUpdateWillBeShown(bool user_initiated);
  void OnUpdateSessionFinished();

  bool ShouldDaoHandleInstallOnQuit() const;

 private:
  bool user_initiated_update_session_ = false;
};

}  // namespace dao

#endif  // DAO_BROWSER_UPDATER_DAO_SPARKLE_UPDATE_SESSION_STATE_H_
