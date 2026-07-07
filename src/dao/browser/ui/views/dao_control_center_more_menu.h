// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/view.h"

namespace views {
class LabelButton;
}

namespace dao {

class DaoControlCenterPopup;

// Sub-panel with additional actions: share, clear cache, clear cookies.
class DaoControlCenterMoreMenu : public views::View {
  METADATA_HEADER(DaoControlCenterMoreMenu, views::View)

 public:
  explicit DaoControlCenterMoreMenu(DaoControlCenterPopup* popup);
  DaoControlCenterMoreMenu(const DaoControlCenterMoreMenu&) = delete;
  DaoControlCenterMoreMenu& operator=(const DaoControlCenterMoreMenu&) =
      delete;
  ~DaoControlCenterMoreMenu() override;

 private:
  void OnBackClicked();
  void OnShareClicked();
  void OnClearCacheClicked();
  void OnClearCookiesClicked();

  raw_ptr<DaoControlCenterPopup> popup_;
  raw_ptr<views::LabelButton> share_button_ = nullptr;
  raw_ptr<views::LabelButton> clear_cache_button_ = nullptr;
  raw_ptr<views::LabelButton> clear_cookies_button_ = nullptr;

  base::WeakPtrFactory<DaoControlCenterMoreMenu> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_MORE_MENU_H_
