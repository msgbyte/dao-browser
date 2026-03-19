// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_extensions_section.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/extensions/extension_action_view_controller.h"
#include "chrome/browser/ui/extensions/extension_side_panel_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/ui/views/extensions/extensions_toolbar_container.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_action_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/flex_layout.h"
#include "ui/views/layout/flex_layout_types.h"

namespace dao {

namespace {

constexpr int kExtIconSize = 28;
constexpr int kExtIconPadding = 4;
constexpr int kExtGridItemSize = kExtIconSize + kExtIconPadding * 2;

}  // namespace

BEGIN_METADATA(DaoControlCenterExtensionsSection)
END_METADATA

DaoControlCenterExtensionsSection::DaoControlCenterExtensionsSection(
    Browser* browser)
    : browser_(browser) {
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

  // "Add" + "Manage" buttons row
  buttons_row_ = AddChildView(std::make_unique<views::View>());
  auto* btn_layout =
      buttons_row_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  btn_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kStart);

  auto add_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(
          &DaoControlCenterExtensionsSection::OnAddClicked,
          base::Unretained(this)),
      u"Add");
  add_btn->SetInstallFocusRingOnFocus(false);
  add_btn->SetAccessibleName(u"Add");
  add_btn->SetEnabledTextColors(SkColorSetRGB(100, 100, 100));
  add_btn->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 8)));
  buttons_row_->AddChildView(std::move(add_btn));

  auto manage_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(
          &DaoControlCenterExtensionsSection::OnManageClicked,
          base::Unretained(this)),
      u"Manage");
  manage_btn->SetInstallFocusRingOnFocus(false);
  manage_btn->SetAccessibleName(u"Manage");
  manage_btn->SetEnabledTextColors(SkColorSetRGB(100, 100, 100));
  manage_btn->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 8)));
  buttons_row_->AddChildView(std::move(manage_btn));

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
  }
}

void DaoControlCenterExtensionsSection::OnToolbarActionAdded(
    const ToolbarActionsModel::ActionId& id) {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarActionRemoved(
    const ToolbarActionsModel::ActionId& id) {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarActionUpdated(
    const ToolbarActionsModel::ActionId& id) {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarModelInitialized() {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::OnToolbarPinnedActionsChanged() {
  grid_dirty_ = true;
}

void DaoControlCenterExtensionsSection::RebuildGrid() {
  grid_->RemoveAllChildViews();
  grid_dirty_ = false;

  if (!model_ || !browser_) {
    return;
  }

  auto* profile = browser_->profile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  ExtensionsToolbarContainer* container = nullptr;
  if (browser_view && browser_view->toolbar() &&
      browser_view->toolbar()->extensions_container()) {
    container = browser_view->toolbar()->extensions_container();
  }

  const auto& action_ids = model_->action_ids();
  for (const auto& id : action_ids) {
    const auto* extension = registry->enabled_extensions().GetByID(id);
    if (!extension) {
      continue;
    }

    // Get the extension icon from the toolbar container
    gfx::ImageSkia icon;
    if (container) {
      auto* action_view = container->GetViewForId(id);
      if (action_view) {
        icon = action_view->GetIconForTest();
      }
    }

    std::u16string name = base::UTF8ToUTF16(extension->name());
    std::string ext_id = id;
    auto btn = std::make_unique<views::ImageButton>(
        base::BindRepeating(
            &DaoControlCenterExtensionsSection::OnExtensionClicked,
            base::Unretained(this), ext_id));
    btn->SetImageModel(views::Button::STATE_NORMAL,
                       ui::ImageModel::FromImageSkia(icon));
    btn->SetPreferredSize(gfx::Size(kExtGridItemSize, kExtGridItemSize));
    btn->SetInstallFocusRingOnFocus(false);
    btn->SetAccessibleName(name);
    btn->SetTooltipText(name);
    grid_->AddChildView(std::move(btn));
  }

  // Show empty state if no extensions
  if (action_ids.empty()) {
    auto* empty_label =
        grid_->AddChildView(std::make_unique<views::Label>(u"No extensions"));
    empty_label->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL,
                                            12, gfx::Font::Weight::NORMAL));
    empty_label->SetEnabledColor(SkColorSetRGB(160, 160, 160));
    empty_label->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 4)));
  }

  InvalidateLayout();
}

void DaoControlCenterExtensionsSection::OnExtensionClicked(
    const std::string& extension_id) {
  if (!browser_) {
    return;
  }

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);

  // Close the control center popup first (unblocks web content events).
  if (browser_view) {
    auto* cc_popup = browser_view->dao_control_center_popup();
    if (cc_popup) {
      cc_popup->Hide();
    }
  }

  auto* web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }

  auto* profile = browser_->profile();
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  if (!registry) {
    return;
  }
  const auto* extension =
      registry->enabled_extensions().GetByID(extension_id);
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
    auto* action_manager =
        extensions::ExtensionActionManager::Get(profile);
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
        popup_url, browser_);
    if (!host) {
      return;
    }

    // Find a visible anchor: prefer the CC button in the address bar.
    views::View* anchor = nullptr;
    if (browser_view && browser_view->dao_address_bar()) {
      anchor = browser_view->dao_address_bar()->control_center_button();
    }
    if (!anchor && browser_view) {
      anchor = browser_view->dao_address_bar();
    }
    if (!anchor) {
      return;
    }

    ExtensionPopup::ShowPopup(std::move(host), anchor,
                              views::BubbleBorder::BOTTOM_RIGHT,
                              PopupShowAction::kShow,
                              base::DoNothing());
  } else if (result ==
             extensions::ExtensionAction::ShowAction::kToggleSidePanel) {
    extensions::side_panel_util::ToggleExtensionSidePanel(
        browser_, extension->id());
  }
}

void DaoControlCenterExtensionsSection::OnAddClicked() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view) {
    auto* cc_popup = browser_view->dao_control_center_popup();
    if (cc_popup) {
      cc_popup->Hide();
    }
  }
  NavigateParams params(browser_, GURL("https://chromewebstore.google.com/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

void DaoControlCenterExtensionsSection::OnManageClicked() {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view) {
    auto* cc_popup = browser_view->dao_control_center_popup();
    if (cc_popup) {
      cc_popup->Hide();
    }
  }
  NavigateParams params(browser_, GURL("dao://extensions/"),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

}  // namespace dao
