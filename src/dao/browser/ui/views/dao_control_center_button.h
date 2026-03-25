// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_BUTTON_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_BUTTON_H_

#include "base/memory/raw_ptr.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/controls/button/button.h"

class Browser;

namespace dao {

// A small icon button (Lucide "sliders-horizontal") placed at the right side
// of DaoAddressBarView.  Clicking it toggles the control center popup.
class DaoControlCenterButton : public views::Button {
  METADATA_HEADER(DaoControlCenterButton, views::Button)

 public:
  explicit DaoControlCenterButton(Browser* browser);
  DaoControlCenterButton(const DaoControlCenterButton&) = delete;
  DaoControlCenterButton& operator=(const DaoControlCenterButton&) = delete;
  ~DaoControlCenterButton() override;

  // Update icon color to match address bar adaptive theme.
  void SetIconColor(SkColor color);

  // views::Button:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  void OnClicked();
  void UpdateBackground();

  raw_ptr<Browser> browser_;
  SkColor icon_color_;
  bool hovered_ = false;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_BUTTON_H_
