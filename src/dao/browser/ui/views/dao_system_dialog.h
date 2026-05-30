// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_SYSTEM_DIALOG_H_
#define DAO_BROWSER_UI_VIEWS_DAO_SYSTEM_DIALOG_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "ui/base/accelerators/accelerator.h"
#include "ui/base/ui_base_types.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"

namespace views {
class DialogDelegate;
}  // namespace views

namespace dao {

struct DaoDialogShortcut {
  ui::Accelerator accelerator;
  std::u16string keycap;
};

struct DaoSystemDialogOptions {
  bool show_enter_for_default = true;
  bool show_esc_for_cancel = true;
};

std::u16string PlatformShortcutKeycap(std::u16string_view key,
                                      bool include_shift);

void ConfigureDaoSystemDialog(
    views::DialogDelegate* delegate,
    const DaoSystemDialogOptions& options = DaoSystemDialogOptions());

std::unique_ptr<views::MdTextButton> CreateDaoDialogButton(
    views::Button::PressedCallback callback,
    std::u16string_view label,
    std::optional<DaoDialogShortcut> shortcut,
    ui::ButtonStyle style);

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_SYSTEM_DIALOG_H_
