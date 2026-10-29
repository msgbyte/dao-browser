// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_APP_MENU_BUTTON_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_APP_MENU_BUTTON_H_

#include <memory>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/base/mojom/menu_source_type.mojom-forward.h"
#include "ui/views/controls/button/menu_button.h"

class AppMenu;
class AppMenuModel;
class BrowserWindowInterface;

namespace dao {

// Windows entry point to the browser menu in the sidebar header.
class DaoAppMenuButton : public views::MenuButton {
  METADATA_HEADER(DaoAppMenuButton, views::MenuButton)

 public:
  DaoAppMenuButton(BrowserWindowInterface* browser,
                   base::RepeatingClosure on_menu_closed);
  DaoAppMenuButton(const DaoAppMenuButton&) = delete;
  DaoAppMenuButton& operator=(const DaoAppMenuButton&) = delete;
  ~DaoAppMenuButton() override;

  void ShowMenu(ui::mojom::MenuSourceType source_type);
  bool IsMenuShowing() const;

  // views::MenuButton:
  void PaintButtonContents(gfx::Canvas* canvas) override;

 private:
  void ButtonPressed(const ui::Event& event);
  void OnMenuClosed();

  const raw_ptr<BrowserWindowInterface> browser_;
  base::RepeatingClosure on_menu_closed_;
  // The menu must be destroyed before the model it references.
  std::unique_ptr<AppMenuModel> menu_model_;
  std::unique_ptr<AppMenu> menu_;
  base::WeakPtrFactory<DaoAppMenuButton> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_APP_MENU_BUTTON_H_
