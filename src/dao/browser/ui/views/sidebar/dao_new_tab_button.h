// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_NEW_TAB_BUTTON_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_NEW_TAB_BUTTON_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/controls/button/button.h"

class Browser;

namespace dao {

class DaoNewTabButton : public views::Button {
  METADATA_HEADER(DaoNewTabButton, views::Button)

 public:
  explicit DaoNewTabButton(Browser* browser);
  DaoNewTabButton(const DaoNewTabButton&) = delete;
  DaoNewTabButton& operator=(const DaoNewTabButton&) = delete;
  ~DaoNewTabButton() override;

  void SetHighlighted(bool highlighted);

 private:
  void UpdateBackground();
  void OnNewTabClicked();

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> plus_icon_ = nullptr;
  bool highlighted_ = false;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_NEW_TAB_BUTTON_H_
