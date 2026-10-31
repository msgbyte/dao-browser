// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"

class TabStripModel;

namespace views {
class Button;
}  // namespace views

namespace dao {

class DaoControlCenterPopup;

// Utility tiles: wide dark mode and high performance tiles with a status line,
// above compact QR Code, Mini Dao and More tiles.
class DaoControlCenterUtilitySection : public views::View,
                                       public TabStripModelObserver,
                                       public content::WebContentsObserver {
  METADATA_HEADER(DaoControlCenterUtilitySection, views::View)

 public:
  explicit DaoControlCenterUtilitySection(DaoControlCenterPopup* popup);
  DaoControlCenterUtilitySection(const DaoControlCenterUtilitySection&) =
      delete;
  DaoControlCenterUtilitySection& operator=(
      const DaoControlCenterUtilitySection&) = delete;
  ~DaoControlCenterUtilitySection() override;

  // Re-applies theme colors and refreshes the dark mode status.
  void Refresh();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  void OnQrClicked();
  void OnMiniDaoClicked();
  void OnForceDarkModeClicked();
  void RefreshForceDarkModeState();
  void OnHighPerformanceModeClicked();
  void ObserveActiveWebContents();
  void UpdateHighPerformanceMode(bool enabled);
  void OnMoreClicked();

  raw_ptr<DaoControlCenterPopup> popup_;
  raw_ptr<views::Button> force_dark_mode_button_ = nullptr;
  raw_ptr<views::Button> high_performance_button_ = nullptr;
  base::CallbackListSubscription high_performance_subscription_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_
