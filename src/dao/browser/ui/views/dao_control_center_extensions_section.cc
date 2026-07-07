// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_extensions_section.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_context_menu_model.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "chrome/browser/ui/extensions/extension_side_panel_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_item_view.h"
#include "ui/views/controls/menu/menu_model_adapter.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

namespace {

constexpr int kExtIconSize = 20;
constexpr int kExtIconPadding = 8;
constexpr int kExtGridItemSize = kExtIconSize + kExtIconPadding * 2;
constexpr int kExtBtnRadius = 8;

// An ImageButton that shows a rounded-rect hover highlight.
class ExtIconButton : public views::ImageButton {
 public:
  ExtIconButton(PressedCallback callback) : ImageButton(std::move(callback)) {}

  void OnMouseEntered(const ui::MouseEvent& event) override {
    ImageButton::OnMouseEntered(event);
    SetBackground(
        views::CreateRoundedRectBackground(SuggestionHover(), kExtBtnRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    ImageButton::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }
};

// A grid action button that draws a Lucide icon and has hover highlight.
class GridActionButton : public views::Button {
 public:
  GridActionButton(LucideIcon icon, PressedCallback callback)
      : Button(std::move(callback)), icon_(icon) {
    SetPreferredSize(gfx::Size(kExtGridItemSize, kExtGridItemSize));
    SetInstallFocusRingOnFocus(false);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    Button::OnMouseEntered(event);
    SetBackground(
        views::CreateRoundedRectBackground(SuggestionHover(), kExtBtnRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    Button::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }

  void PaintButtonContents(gfx::Canvas* canvas) override {
    float icon_size = static_cast<float>(kExtIconSize);
    float ox = (width() - icon_size) / 2.0f;
    float oy = (height() - icon_size) / 2.0f;
    DrawLucideIcon(canvas, icon_, gfx::RectF(ox, oy, icon_size, icon_size),
                   ControlCenterIconMuted());
  }

 private:
  LucideIcon icon_;
};

}  // namespace

BEGIN_METADATA(DaoControlCenterExtensionsSection)
END_METADATA

DaoControlCenterExtensionsSection::DaoControlCenterExtensionsSection(
    Browser* browser,
    base::RepeatingClosure close_host_callback,
    base::RepeatingCallback<views::View*()> anchor_view_callback)
    : browser_(browser),
      close_host_callback_(std::move(close_host_callback)),
      anchor_view_callback_(std::move(anchor_view_callback)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Grid area for extension icons (uses FlexLayout for wrapping)
  grid_ = AddChildView(std::make_unique<views::View>());
  auto* grid_layout =
      grid_->SetLayoutManager(std::make_unique<views::FlexLayout>());
  grid_layout->SetOrientation(views::LayoutOrientation::kHorizontal);
  grid_layout->SetDefault(
      views::kFlexBehaviorKey,
      views::FlexSpecification(views::MinimumFlexSizeRule::kPreferred,
                               views::MaximumFlexSizeRule::kPreferred,
                               /*adjust_height_for_width=*/true));

  // Observe toolbar model for changes
  auto* profile = browser_->profile();
  model_ = ToolbarActionsModel::Get(profile);
  if (model_) {
    observation_.Observe(model_.get());
  }

  // Build the initial grid eagerly so it's ready when the popup opens.
  RebuildGrid();
}

DaoControlCenterExtensionsSection::~DaoControlCenterExtensionsSection() =
    default;

void DaoControlCenterExtensionsSection::Refresh() {
  if (grid_dirty_) {
    RebuildGrid();
    return;
  }
  UpdateAllButtonIcons();
}

void DaoControlCenterExtensionsSection::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  // Drop the cached icon factory: it holds raw pointers to the Extension and
  // ExtensionAction, which are about to be destroyed.
  icon_factories_.erase(id);
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  // The Extension/ExtensionAction backing the cached factory may have been
  // replaced (e.g. extension reload or permission change). Drop the stale
  // factory so GetIconForExtension rebuilds it against the current action,
  // avoiding a use-after-free on the old raw pointers.
  icon_factories_.erase(id);
  if (!UpdateButtonIcon(id)) {
    grid_dirty_ = true;
  }
}

void DaoControlCenterExtensionsSection::OnToolbarModelInitialized() {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarPinnedActionsChanged() {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::RebuildGrid() {
  grid_->RemoveAllChildViews();
  button_to_extension_id_.clear();
  id_to_button_.clear();
  icon_factories_.clear();
  grid_dirty_ = false;

  if (!model_ || !browser_) {
    return;
  }

  auto* profile = browser_->profile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }

  const auto& action_ids = model_->action_ids();
  for (const auto& id : action_ids) {
    const auto* extension = registry->enabled_extensions().GetByID(id);
    if (!extension) {
      continue;
    }

    gfx::ImageSkia icon = GetIconForExtension(id, *extension);

    std::u16string name = base::UTF8ToUTF16(extension->name());
    std::string ext_id = id;

    auto btn = std::make_unique<ExtIconButton>(base::BindRepeating(
        &DaoControlCenterExtensionsSection::OnExtensionClicked,
        base::Unretained(this), ext_id));
    btn->SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromImageSkia(icon));
    btn->SetImageHorizontalAlignment(views::ImageButton::ALIGN_CENTER);
    btn->SetImageVerticalAlignment(views::ImageButton::ALIGN_MIDDLE);
    btn->SetPreferredSize(gfx::Size(kExtGridItemSize, kExtGridItemSize));
    btn->SetInstallFocusRingOnFocus(false);
    btn->SetAccessibleName(name);
    btn->SetTooltipText(name);
    btn->set_context_menu_controller(this);

    views::View* btn_raw = btn.release();
    views::ImageButton* btn_ptr =
        static_cast<views::ImageButton*>(grid_->AddChildView(btn_raw));
    button_to_extension_id_[btn_ptr] = ext_id;
    id_to_button_[ext_id] = btn_ptr;
  }

  // "Add" action button — appended after extension icons.
  auto add_btn = std::make_unique<GridActionButton>(
      LucideIcon::kPlus,
      base::BindRepeating(&DaoControlCenterExtensionsSection::OnAddClicked,
                          base::Unretained(this)));
  std::u16string add_label =
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_ADD_EXTENSION);
  add_btn->SetAccessibleName(add_label);
  add_btn->SetTooltipText(add_label);
  grid_->AddChildView(static_cast<views::View*>(add_btn.release()));

  // "Manage" action button.
  auto manage_btn = std::make_unique<GridActionButton>(
      LucideIcon::kSettings,
      base::BindRepeating(&DaoControlCenterExtensionsSection::OnManageClicked,
                          base::Unretained(this)));
  std::u16string manage_label =
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_MANAGE_EXTENSIONS);
  manage_btn->SetAccessibleName(manage_label);
  manage_btn->SetTooltipText(manage_label);
  grid_->AddChildView(static_cast<views::View*>(manage_btn.release()));

  InvalidateLayout();
}

gfx::ImageSkia DaoControlCenterExtensionsSection::GetIconForExtension(
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

bool DaoControlCenterExtensionsSection::UpdateButtonIcon(
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

void DaoControlCenterExtensionsSection::UpdateAllButtonIcons() {
  for (const auto& entry : id_to_button_) {
    UpdateButtonIcon(entry.first);
  }
}

void DaoControlCenterExtensionsSection::OnIconUpdated() {
  UpdateAllButtonIcons();
}

void DaoControlCenterExtensionsSection::CloseHostSurface() {
  if (close_host_callback_) {
    close_host_callback_.Run();
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (!browser_view) {
    return;
  }
  auto* cc_popup = browser_view->dao_control_center_popup();
  if (cc_popup) {
    cc_popup->Hide();
  }
}

views::View* DaoControlCenterExtensionsSection::GetExtensionPopupAnchor(
    BrowserView* browser_view) const {
  if (anchor_view_callback_) {
    if (views::View* anchor = anchor_view_callback_.Run()) {
      return anchor;
    }
  }
  if (browser_view && browser_view->dao_address_bar()) {
    if (views::View* anchor =
            browser_view->dao_address_bar()->control_center_button()) {
      return anchor;
    }
    return browser_view->dao_address_bar();
  }
  return nullptr;
}

void DaoControlCenterExtensionsSection::OnExtensionClicked(
    const std::string& extension_id) {
  if (!browser_) {
    return;
  }

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  CloseHostSurface();

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

  // Use ExtensionActionRunner to trigger the action directly.
  extensions::ExtensionActionRunner* runner =
      extensions::ExtensionActionRunner::GetForWebContents(web_contents);
  if (!runner) {
    return;
  }

  extensions::ExtensionAction::ShowAction result =
      runner->RunAction(extension, /*grant_tab_permissions=*/true);

  if (result == extensions::ExtensionAction::ShowAction::kShowPopup) {
    // Show popup anchored to the CC button in the address bar.
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

    views::View* anchor = GetExtensionPopupAnchor(browser_view);
    if (!anchor) {
      return;
    }

    ExtensionPopup::ShowPopup(browser_, std::move(host), anchor,
                              views::BubbleBorder::BOTTOM_RIGHT,
                              PopupShowAction::kShow, base::DoNothing());
  } else if (result ==
             extensions::ExtensionAction::ShowAction::kToggleSidePanel) {
    extensions::side_panel_util::ToggleExtensionSidePanel(browser_,
                                                          extension->id());
  }
}

void DaoControlCenterExtensionsSection::OnAddClicked() {
  CloseHostSurface();
  NavigateParams params(browser_, GURL("https://chromewebstore.google.com/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void DaoControlCenterExtensionsSection::OnManageClicked() {
  CloseHostSurface();
  NavigateParams params(browser_, GURL("dao://extensions/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

std::string DaoControlCenterExtensionsSection::GetExtensionIdForView(
    views::View* view) const {
  auto it = button_to_extension_id_.find(view);
  if (it != button_to_extension_id_.end()) {
    return it->second;
  }
  return std::string();
}

void DaoControlCenterExtensionsSection::ShowContextMenuForViewImpl(
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
      base::BindRepeating(
          &DaoControlCenterExtensionsSection::OnContextMenuClosed,
          base::Unretained(this)));

  auto menu = context_menu_adapter_->CreateMenu();
  context_menu_runner_ = std::make_unique<views::MenuRunner>(
      std::move(menu),
      views::MenuRunner::HAS_MNEMONICS | views::MenuRunner::CONTEXT_MENU);

  context_menu_runner_->RunMenuAt(
      source->GetWidget(), nullptr, source->GetAnchorBoundsInScreen(),
      views::MenuAnchorPosition::kTopLeft, source_type);
}

void DaoControlCenterExtensionsSection::OnContextMenuClosed() {
  context_menu_runner_.reset();
  context_menu_adapter_.reset();
  context_menu_model_.reset();
}

}  // namespace dao
