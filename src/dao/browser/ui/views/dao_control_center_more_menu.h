// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"

class TabStripModel;

namespace views {
class LabelButton;
}

namespace dao {

class DaoControlCenterPopup;

// Sub-panel with additional actions and per-tab settings.
class DaoControlCenterMoreMenu : public views::View,
                                 public TabStripModelObserver,
                                 public content::WebContentsObserver {
  METADATA_HEADER(DaoControlCenterMoreMenu, views::View)

 public:
  explicit DaoControlCenterMoreMenu(DaoControlCenterPopup* popup);
  DaoControlCenterMoreMenu(const DaoControlCenterMoreMenu&) = delete;
  DaoControlCenterMoreMenu& operator=(const DaoControlCenterMoreMenu&) =
      delete;
  ~DaoControlCenterMoreMenu() override;

  void ApplyTheme();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void WebContentsDestroyed() override;

 private:
  void OnBackClicked();
  void OnShareClicked();
  void OnClearCacheClicked();
  void OnClearCookiesClicked();
  void OnHighPerformanceModeClicked();
  void ObserveActiveWebContents();
  void UpdateHighPerformanceMode(bool enabled);
  void ClearActiveSiteBrowsingData(uint64_t remove_mask,
                                   views::LabelButton* button,
                                   int idle_string_id,
                                   int toast_string_id);
  void OnClearButtonOperationFinished(views::LabelButton* button,
                                      int idle_string_id,
                                      int toast_string_id);

  raw_ptr<DaoControlCenterPopup> popup_;
  raw_ptr<views::LabelButton> share_button_ = nullptr;
  raw_ptr<views::LabelButton> clear_cache_button_ = nullptr;
  raw_ptr<views::LabelButton> clear_cookies_button_ = nullptr;
  raw_ptr<views::LabelButton> high_performance_button_ = nullptr;
  raw_ptr<views::View> cleanup_separator_ = nullptr;
  base::CallbackListSubscription high_performance_subscription_;

  base::WeakPtrFactory<DaoControlCenterMoreMenu> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_
