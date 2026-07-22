// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_pinned_extensions_container.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "chrome/browser/ui/extensions/extension_side_panel_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

namespace {

constexpr int kBtnRadius = 6;

// An ImageButton that shows a rounded-rect hover highlight for pinned
// extension icons in the address bar.
class PinnedExtIconButton : public views::ImageButton {
 public:
  explicit PinnedExtIconButton(PressedCallback callback)
      : ImageButton(std::move(callback)) {}

  void OnMouseEntered(const ui::MouseEvent& event) override {
    ImageButton::OnMouseEntered(event);
    SetBackground(
        views::CreateRoundedRectBackground(ControlCenterHoverBg(), kBtnRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    ImageButton::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }
};

}  // namespace

BEGIN_METADATA(DaoPinnedExtensionsContainer)
END_METADATA

DaoPinnedExtensionsContainer::DaoPinnedExtensionsContainer(Browser* browser)
    : browser_(browser) {
  auto* layout = SetLayoutManager(std::make_unique<views::FlexLayout>());
  layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  layout->SetCrossAxisAlignment(views::LayoutAlignment::kCenter);

  auto* profile = browser_->profile();
  model_ = ToolbarActionsModel::Get(profile);
  if (model_) {
    observation_.Observe(model_.get());
  }

  tab_strip_model_ = browser_->tab_strip_model();
  if (tab_strip_model_) {
    tab_strip_model_->AddObserver(this);
  }

  Rebuild();
}

DaoPinnedExtensionsContainer::~DaoPinnedExtensionsContainer() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoPinnedExtensionsContainer::Rebuild() {
  RemoveAllChildViews();
  button_to_extension_id_.clear();
  id_to_button_.clear();
  icon_factories_.clear();

  if (!model_ || !browser_) {
    SetVisible(false);
    return;
  }

  auto* profile = browser_->profile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    SetVisible(false);
    return;
  }

  const auto& pinned_ids = model_->pinned_action_ids();
  int count = 0;
  for (const auto& id : pinned_ids) {
    if (count >= kMaxVisiblePinned) {
      break;
    }

    const auto* extension = registry->enabled_extensions().GetByID(id);
    if (!extension) {
      continue;
    }

    gfx::ImageSkia icon = GetIconForExtension(id, *extension);
    std::u16string name = base::UTF8ToUTF16(extension->name());
    std::string ext_id = id;

    auto btn = std::make_unique<PinnedExtIconButton>(
        base::BindRepeating(&DaoPinnedExtensionsContainer::OnExtensionClicked,
                            base::Unretained(this), ext_id));
    btn->SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromImageSkia(icon));
    btn->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    btn->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    btn->SetPreferredSize(gfx::Size(kPinnedButtonSize, kPinnedButtonSize));
    btn->SetInstallFocusRingOnFocus(false);
    btn->SetAccessibleName(name);
    btn->SetTooltipText(name);
    btn->set_context_menu_controller(this);

    views::ImageButton* btn_raw = btn.release();
    views::ImageButton* btn_ptr =
        static_cast<views::ImageButton*>(AddChildView(btn_raw));
    button_to_extension_id_[btn_ptr] = ext_id;
    id_to_button_[ext_id] = btn_ptr;
    ++count;
  }

  SetVisible(count > 0);
  InvalidateLayout();
}

gfx::ImageSkia DaoPinnedExtensionsContainer::GetIconForExtension(
    const std::string& extension_id,
    const extensions::Extension& extension) {
  if (!browser_) {
    return gfx::ImageSkia();
  }

  auto* action_manager =
      extensions::ExtensionActionManager::Get(browser_->profile());
  if (!action_manager) {
    return gfx::ImageSkia();
  }
  auto* action = action_manager->GetExtensionAction(extension);
  if (!action) {
    return gfx::ImageSkia();
  }

  auto& icon_factory = icon_factories_[extension_id];
  if (!icon_factory) {
    icon_factory = std::make_unique<extensions::ExtensionActionIconFactory>(
        &extension, action, this);
  }

  int tab_id = extensions::ExtensionAction::kDefaultTabId;
  if (auto* tab_strip_model = browser_->tab_strip_model()) {
    if (auto* web_contents = tab_strip_model->GetActiveWebContents()) {
      tab_id = extensions::ExtensionTabUtil::GetTabId(web_contents);
    }
  }

  gfx::Image icon = icon_factory->GetIcon(tab_id);
  return icon.AsImageSkia();
}

bool DaoPinnedExtensionsContainer::UpdateButtonIcon(
    const std::string& extension_id) {
  auto button_it = id_to_button_.find(extension_id);
  if (button_it == id_to_button_.end() || !button_it->second || !browser_) {
    return false;
  }

  auto* registry = extensions::ExtensionRegistry::Get(browser_->profile());
  if (!registry) {
    return false;
  }
  const auto* extension = registry->enabled_extensions().GetByID(extension_id);
  if (!extension) {
    return false;
  }

  button_it->second->SetImageModel(
      views::Button::STATE_NORMAL,
      ui::ImageModel::FromImageSkia(
          GetIconForExtension(extension_id, *extension)));
  return true;
}

void DaoPinnedExtensionsContainer::UpdateAllButtonIcons() {
  for (const auto& entry : id_to_button_) {
    UpdateButtonIcon(entry.first);
  }
}

void DaoPinnedExtensionsContainer::OnIconUpdated() {
  UpdateAllButtonIcons();
}

void DaoPinnedExtensionsContainer::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  Rebuild();
}

void DaoPinnedExtensionsContainer::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  Rebuild();
}

void DaoPinnedExtensionsContainer::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  if (!UpdateButtonIcon(id)) {
    Rebuild();
  }
}

void DaoPinnedExtensionsContainer::OnToolbarModelInitialized() {
  Rebuild();
}

void DaoPinnedExtensionsContainer::OnToolbarPinnedActionsChanged() {
  Rebuild();
}

void DaoPinnedExtensionsContainer::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    UpdateAllButtonIcons();
  }
}

void DaoPinnedExtensionsContainer::OnTabChangedAt(tabs::TabInterface* tab,
                                                  int index,
                                                  TabChangeType change_type) {
  UpdateAllButtonIcons();
}

void DaoPinnedExtensionsContainer::OnExtensionClicked(
    const std::string& extension_id) {
  if (!browser_) {
    return;
  }

  auto* web_contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }

  auto* profile = browser_->profile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }
  const auto* extension = registry->enabled_extensions().GetByID(extension_id);
  if (!extension) {
    return;
  }

  extensions::ExtensionActionRunner* runner =
      extensions::ExtensionActionRunner::GetForWebContents(web_contents);
  if (!runner) {
    return;
  }

  extensions::ExtensionAction::ShowAction result =
      runner->RunAction(extension, /*grant_tab_permissions=*/true);

  if (result == extensions::ExtensionAction::ShowAction::kShowPopup) {
    auto* action_manager = extensions::ExtensionActionManager::Get(profile);
    auto* action = action_manager->GetExtensionAction(*extension);
    if (!action) {
      return;
    }
    int tab_id = extensions::ExtensionTabUtil::GetTabId(web_contents);
    GURL popup_url = action->GetPopupUrl(tab_id);
    if (!popup_url.is_valid()) {
      return;
    }

    auto host = extensions::ExtensionViewHostFactory::CreatePopupHost(
        *extension, popup_url, browser_);
    if (!host) {
      return;
    }

    // Anchor the popup to the clicked extension icon button.
    views::View* anchor = GetButtonForExtension(extension_id);
    if (!anchor) {
      // Fallback to this container.
      anchor = this;
    }

    ExtensionPopup::ShowPopup(browser_, std::move(host),
                              views::BubbleAnchor(anchor),
                              views::BubbleBorder::BOTTOM_RIGHT,
                              PopupShowAction::kShow, base::DoNothing());
  } else if (result ==
             extensions::ExtensionAction::ShowAction::kToggleSidePanel) {
    extensions::side_panel_util::ToggleExtensionSidePanel(browser_,
                                                          extension->id());
  }
}

std::vector<gfx::Rect> DaoPinnedExtensionsContainer::GetButtonRects() const {
  std::vector<gfx::Rect> rects;
  for (const auto& child : children()) {
    if (child->GetVisible()) {
      rects.push_back(child->GetMirroredBounds());
    }
  }
  return rects;
}

views::View* DaoPinnedExtensionsContainer::GetButtonForExtension(
    const std::string& extension_id) const {
  auto it = id_to_button_.find(extension_id);
  if (it != id_to_button_.end()) {
    return it->second;
  }
  return nullptr;
}

std::string DaoPinnedExtensionsContainer::GetExtensionIdForView(
    views::View* view) const {
  auto it = button_to_extension_id_.find(view);
  if (it != button_to_extension_id_.end()) {
    return it->second;
  }
  return std::string();
}

void DaoPinnedExtensionsContainer::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  std::string extension_id = GetExtensionIdForView(source);
  if (extension_id.empty() || !browser_) {
    return;
  }

  auto* profile = browser_->profile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }
  const auto* extension = registry->enabled_extensions().GetByID(extension_id);
  if (!extension) {
    return;
  }

  bool is_pinned = model_ && model_->IsActionPinned(extension_id);
  context_menu_model_ = std::make_unique<extensions::ExtensionContextMenuModel>(
      extension, browser_, is_pinned, /*delegate=*/nullptr,
      /*can_show_icon_in_toolbar=*/true,
      extensions::ExtensionContextMenuModel::ContextMenuSource::kToolbarAction);

  context_menu_adapter_ = std::make_unique<views::MenuModelAdapter>(
      context_menu_model_.get(),
      base::BindRepeating(&DaoPinnedExtensionsContainer::OnContextMenuClosed,
                          base::Unretained(this)));

  auto menu = context_menu_adapter_->CreateMenu();
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      std::move(menu),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);

  context_menu_runner_->RunMenuAt(
      source->GetWidget(), nullptr, source->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

void DaoPinnedExtensionsContainer::OnContextMenuClosed() {
  context_menu_runner_.reset();
  context_menu_adapter_.reset();
  context_menu_model_.reset();
}

}  // namespace dao
