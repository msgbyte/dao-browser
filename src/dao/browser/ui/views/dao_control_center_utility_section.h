// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Button;
}  // namespace views

namespace dao {

class DaoControlCenterPopup;

// A row of utility buttons: QR Code, Mini Dao, Security, dark mode, More.
class DaoControlCenterUtilitySection : public views::View {
  METADATA_HEADER(DaoControlCenterUtilitySection, views::View)

 public:
  explicit DaoControlCenterUtilitySection(DaoControlCenterPopup* popup);
  DaoControlCenterUtilitySection(const DaoControlCenterUtilitySection&) =
      delete;
  DaoControlCenterUtilitySection& operator=(
      const DaoControlCenterUtilitySection&) = delete;
  ~DaoControlCenterUtilitySection() override;

  void Refresh();

 private:
  void OnQrClicked();
  void OnMiniDaoClicked();
  void OnForceDarkModeClicked();
  void RefreshForceDarkModeState();
  void OnLockClicked();
  void OnMoreClicked();

  raw_ptr<DaoControlCenterPopup> popup_;
  raw_ptr<views::Button> force_dark_mode_button_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_
