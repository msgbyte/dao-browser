// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace dao {

class DaoControlCenterPopup;

// A row of utility buttons: Share, QR Code, Mini Dao, Lock (Page Info), More.
class DaoControlCenterUtilitySection : public views::View {
  METADATA_HEADER(DaoControlCenterUtilitySection, views::View)

 public:
  explicit DaoControlCenterUtilitySection(DaoControlCenterPopup* popup);
  DaoControlCenterUtilitySection(const DaoControlCenterUtilitySection&) =
      delete;
  DaoControlCenterUtilitySection& operator=(
      const DaoControlCenterUtilitySection&) = delete;
  ~DaoControlCenterUtilitySection() override;

 private:
  void OnShareClicked();
  void OnQrClicked();
  void OnMiniDaoClicked();
  void OnLockClicked();
  void OnMoreClicked();

  raw_ptr<DaoControlCenterPopup> popup_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_UTILITY_SECTION_H_
