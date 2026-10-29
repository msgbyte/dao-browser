// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_app_menu_button.h"

#include <utility>

#include "base/functional/bind.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/accelerator_utils.h"
#include "chrome/browser/ui/toolbar/app_menu_model.h"
#include "chrome/browser/ui/views/toolbar/app_menu.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/menu_source_utils.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/events/event_constants.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/menu/menu_runner.h"

namespace dao {

namespace {

class DaoAppMenuModel : public AppMenuModel {
 public:
  explicit DaoAppMenuModel(BrowserWindowInterface* browser)
      : AppMenuModel(AcceleratorProviderForBrowser(browser), browser) {}

  bool GetAcceleratorForCommandId(
      int command_id,
      ui::Accelerator* accelerator) const override {
    if (!AppMenuModel::GetAcceleratorForCommandId(command_id, accelerator)) {
      return false;
    }
    // These keys belong to Dao's sidebar, duplicate-tab, Agent, and copy-URL
    // actions. Do not advertise Chromium's conflicting default shortcuts.
    const int modifiers = accelerator->modifiers();
    const auto key = accelerator->key_code();
    if (modifiers == ui::EF_CONTROL_DOWN &&
        (key == ui::VKEY_S || key == ui::VKEY_D || key == ui::VKEY_E)) {
      return false;
    }
    return !(modifiers == (ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN) &&
             key == ui::VKEY_C);
  }

 protected:
  void Build() override {
    AppMenuModel::Build();
    // Keep the native menu's policy checks, dynamic submenus, and localized
    // labels while making Settings the first, directly accessible action.
    const auto settings_index = GetIndexOfCommandId(IDC_OPTIONS);
    if (settings_index) {
      const auto label = GetLabelAt(*settings_index);
      const auto icon = GetIconAt(*settings_index);
      RemoveItemAt(*settings_index);
      InsertItemAt(0, IDC_OPTIONS, label);
      SetIcon(0, icon);
      InsertSeparatorAt(1, ui::NORMAL_SEPARATOR);
    }
  }
};

}  // namespace

DaoAppMenuButton::DaoAppMenuButton(BrowserWindowInterface* browser,
                                 base::RepeatingClosure on_menu_closed)
    : MenuButton(base::BindRepeating(&DaoAppMenuButton::ButtonPressed,
                                     base::Unretained(this))),
      browser_(browser),
      on_menu_closed_(std::move(on_menu_closed)) {
  SetPreferredSize(gfx::Size(32, 32));
  SetInstallFocusRingOnFocus(false);
  const auto label = l10n_util::GetStringUTF16(IDS_DAO_APP_MENU);
  SetTooltipText(label);
  SetAccessibleName(label);
  GetViewAccessibility().SetIsCollapsed();
}

DaoAppMenuButton::~DaoAppMenuButton() = default;

void DaoAppMenuButton::ShowMenu(ui::mojom::MenuSourceType source_type) {
  if (!GetWidget() || IsMenuShowing()) {
    return;
  }

  // Rebuild each time so profile, policy, and recently closed tabs are current.
  menu_.reset();
  menu_model_ = std::make_unique<DaoAppMenuModel>(browser_);
  menu_model_->Init();
  int run_types = views::MenuRunner::HAS_MNEMONICS;
  if (source_type == ui::mojom::MenuSourceType::kKeyboard) {
    run_types |= views::MenuRunner::SHOULD_SHOW_MNEMONICS |
                 views::MenuRunner::INVOKED_FROM_KEYBOARD;
  }
  menu_ = std::make_unique<AppMenu>(
      browser_, menu_model_.get(), run_types,
      base::BindRepeating(&DaoAppMenuButton::OnMenuClosed,
                          weak_factory_.GetWeakPtr()));
  GetViewAccessibility().SetIsExpanded();
  // RunMenu owns the pressed lock and restores focus when the menu closes.
  // A command may close the browser, so do not access members after this call.
  menu_->RunMenu(button_controller(), source_type,
                 views::MenuAnchorPosition::kTopLeft);
}

bool DaoAppMenuButton::IsMenuShowing() const {
  return menu_ && menu_->IsShowing();
}

void DaoAppMenuButton::PaintButtonContents(gfx::Canvas* canvas) {
  constexpr float kHoverSize = 24.0f;
  constexpr float kHoverRadius = 6.0f;
  constexpr float kIconSize = 16.0f;
  if (GetState() == STATE_HOVERED || GetState() == STATE_PRESSED || HasFocus()) {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(dao::ControlCenterHoverBg(browser_));
    canvas->DrawRoundRect(
        gfx::RectF((width() - kHoverSize) / 2.0f,
                   (height() - kHoverSize) / 2.0f, kHoverSize, kHoverSize),
        kHoverRadius, flags);
  }
  DrawLucideIcon(
      canvas, LucideIcon::kMenu,
      gfx::RectF((width() - kIconSize) / 2.0f,
                 (height() - kIconSize) / 2.0f, kIconSize, kIconSize),
      dao::TextSecondary(browser_));
}

void DaoAppMenuButton::ButtonPressed(const ui::Event& event) {
  ShowMenu(ui::GetMenuSourceTypeForEvent(event));
}

void DaoAppMenuButton::OnMenuClosed() {
  GetViewAccessibility().SetIsCollapsed();
  SchedulePaint();
  // AppMenu still uses its model after this callback. Keep both alive until
  // the next opening, and let the sidebar defer collapse until teardown ends.
  if (on_menu_closed_) {
    on_menu_closed_.Run();
  }
}

BEGIN_METADATA(DaoAppMenuButton)
END_METADATA

}  // namespace dao
