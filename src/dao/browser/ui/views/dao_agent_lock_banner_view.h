// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_AGENT_LOCK_BANNER_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_AGENT_LOCK_BANNER_VIEW_H_

#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

namespace dao {

// A translucent lock overlay shown over the page content when the active tab
// is controlled by the AI agent.
class DaoAgentLockBannerView : public views::View,
                               public ui::NativeThemeObserver {
  METADATA_HEADER(DaoAgentLockBannerView, views::View)

 public:
  DaoAgentLockBannerView();
  ~DaoAgentLockBannerView() override;

  DaoAgentLockBannerView(const DaoAgentLockBannerView&) = delete;
  DaoAgentLockBannerView& operator=(const DaoAgentLockBannerView&) = delete;

  // Show or hide based on whether the active tab is agent-locked.
  void SetLocked(bool locked);

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 protected:
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  float GetAnimationPhase() const;

  bool locked_ = false;
  base::TimeTicks animation_start_time_;
  base::RepeatingTimer animation_timer_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_AGENT_LOCK_BANNER_VIEW_H_
