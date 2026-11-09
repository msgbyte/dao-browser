// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/automation/dao_devtools_client.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
}

namespace dao {

class DaoControlCenterPopup;

// Sub-panel with additional actions.
class DaoControlCenterMoreMenu : public views::View,
                                 public content::WebContentsObserver {
  METADATA_HEADER(DaoControlCenterMoreMenu, views::View)

 public:
  explicit DaoControlCenterMoreMenu(DaoControlCenterPopup* popup);
  DaoControlCenterMoreMenu(const DaoControlCenterMoreMenu&) = delete;
  DaoControlCenterMoreMenu& operator=(const DaoControlCenterMoreMenu&) =
      delete;
  ~DaoControlCenterMoreMenu() override;

  void ApplyTheme();

 private:
  void OnBackClicked();
  void OnShareClicked();
  void OnScreenshotClicked();
  void OnScreenshotCaptured(DaoDevToolsClient::CommandResult result);
  void FinishScreenshot(bool failed);
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;
  void OnClearCacheClicked();
  void OnClearCookiesClicked();
  void ClearActiveSiteBrowsingData(uint64_t remove_mask,
                                   views::LabelButton* button,
                                   int idle_string_id,
                                   int toast_string_id);
  void OnClearButtonOperationFinished(views::LabelButton* button,
                                      int idle_string_id,
                                      int toast_string_id);

  raw_ptr<DaoControlCenterPopup> popup_;
  raw_ptr<views::LabelButton> share_button_ = nullptr;
  raw_ptr<views::LabelButton> screenshot_button_ = nullptr;
  raw_ptr<views::LabelButton> clear_cache_button_ = nullptr;
  raw_ptr<views::LabelButton> clear_cookies_button_ = nullptr;
  raw_ptr<views::View> cleanup_separator_ = nullptr;

  DaoDevToolsClient screenshot_client_;
  base::OneShotTimer screenshot_timeout_;

  base::WeakPtrFactory<DaoControlCenterMoreMenu> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_
