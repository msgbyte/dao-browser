// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/updater/dao_sparkle_update_session_state.h"

namespace dao {

void DaoSparkleUpdateSessionState::OnStandardUpdateWillBeShown(
    bool user_initiated) {
  user_initiated_update_session_ = user_initiated;
}

void DaoSparkleUpdateSessionState::OnUpdateSessionFinished() {
  user_initiated_update_session_ = false;
}

bool DaoSparkleUpdateSessionState::ShouldDaoHandleInstallOnQuit() const {
  return !user_initiated_update_session_;
}

}  // namespace dao
