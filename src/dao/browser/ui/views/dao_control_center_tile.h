// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_TILE_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_TILE_H_

#include <memory>
#include <string>

#include "base/memory/raw_ptr.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/views/controls/button/button.h"

namespace views {
class Label;
}

namespace dao {

// Shared tile surface for the main window and Mini Dao site controls.
class DaoControlCenterTileButton : public views::Button {
  METADATA_HEADER(DaoControlCenterTileButton, views::Button)

 public:
  explicit DaoControlCenterTileButton(PressedCallback callback);
  virtual void ApplyTheme();
  void SetVisualEnabled(bool enabled);

  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;

 protected:
  bool visually_enabled() const { return visually_enabled_; }

 private:
  bool visually_enabled_ = true;
};

// A compact tile with an icon above a short label.
class DaoControlCenterActionTile : public DaoControlCenterTileButton {
  METADATA_HEADER(DaoControlCenterActionTile, DaoControlCenterTileButton)

 public:
  DaoControlCenterActionTile(const std::u16string& label_text,
                             LucideIcon icon,
                             PressedCallback callback);
  void ApplyTheme() override;
  void Layout(PassKey) override;
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  LucideIcon icon_;
  raw_ptr<views::Label> label_ = nullptr;
};

std::unique_ptr<views::View> CreateControlCenterTileRow();

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_TILE_H_
