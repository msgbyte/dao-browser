// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_AGENT_LOCK_BANNER_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_AGENT_LOCK_BANNER_VIEW_H_

#include "ui/views/view.h"

namespace dao {

// A thin, translucent purple banner shown at the top of the content area
// when the active tab is locked by the AI agent.  The banner is purely
// informational and does not block input (input is already blocked at the
// WebContents level by DaoAgentLockTabHelper).
class DaoAgentLockBannerView : public views::View {
  METADATA_HEADER(DaoAgentLockBannerView, views::View)

 public:
  DaoAgentLockBannerView();
  ~DaoAgentLockBannerView() override;

  DaoAgentLockBannerView(const DaoAgentLockBannerView&) = delete;
  DaoAgentLockBannerView& operator=(const DaoAgentLockBannerView&) = delete;

  // Show or hide based on whether the active tab is agent-locked.
  void SetLocked(bool locked);

 protected:
  void OnPaint(gfx::Canvas* canvas) override;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_AGENT_LOCK_BANNER_VIEW_H_
