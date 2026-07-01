// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_EXTENSIONS_SECTION_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_EXTENSIONS_SECTION_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class Browser;
class BrowserView;

namespace gfx {
class ImageSkia;
class Point;
}  // namespace gfx

namespace ui {
class MenuModel;
}

namespace ui::mojom {
enum class MenuSourceType;
}

namespace views {
class ImageButton;
class MenuModelAdapter;
class MenuRunner;
}  // namespace views

namespace dao {

// Displays installed extension icons in a wrapping grid.
// Includes "Add" and "Manage" shortcut buttons at the bottom.
class DaoControlCenterExtensionsSection
    : public views::View,
      public ToolbarActionsModel::Observer,
      public views::ContextMenuController,
      public extensions::ExtensionActionIconFactory::Observer {
  METADATA_HEADER(DaoControlCenterExtensionsSection, views::View)

 public:
  explicit DaoControlCenterExtensionsSection(
      Browser* browser,
      base::RepeatingClosure close_host_callback = base::RepeatingClosure(),
      base::RepeatingCallback<views::View*()> anchor_view_callback =
          base::RepeatingCallback<views::View*()>());
  DaoControlCenterExtensionsSection(const DaoControlCenterExtensionsSection&) =
      delete;
  DaoControlCenterExtensionsSection& operator=(
      const DaoControlCenterExtensionsSection&) = delete;
  ~DaoControlCenterExtensionsSection() override;

  // Rebuild the extensions grid from the current model.
  void Refresh();

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // extensions::ExtensionActionIconFactory::Observer:
  void OnIconUpdated() override;

 private:
  void RebuildGrid();
  gfx::ImageSkia GetIconForExtension(const std::string& extension_id,
                                     const extensions::Extension& extension);
  bool UpdateButtonIcon(const std::string& extension_id);
  void UpdateAllButtonIcons();
  void OnExtensionClicked(const std::string& extension_id);
  void OnAddClicked();
  void OnManageClicked();
  void CloseHostSurface();
  views::View* GetExtensionPopupAnchor(BrowserView* browser_view) const;
  void OnContextMenuClosed();

  // Returns the extension_id associated with a button view, or empty string.
  std::string GetExtensionIdForView(views::View* view) const;

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> grid_ = nullptr;
  raw_ptr<ToolbarActionsModel> model_ = nullptr;
  bool grid_dirty_ = true;
  base::RepeatingClosure close_host_callback_;
  base::RepeatingCallback<views::View*()> anchor_view_callback_;

  // Maps icon button pointers to extension IDs for context menu lookup.
  std::map<views::View*, std::string> button_to_extension_id_;

  // Cached action icon factories keyed by extension ID.
  std::map<std::string, std::unique_ptr<extensions::ExtensionActionIconFactory>>
      icon_factories_;

  // Maps extension ID to the corresponding button for async icon updates.
  std::map<std::string, raw_ptr<views::ImageButton>> id_to_button_;

  // Context menu state.
  std::unique_ptr<ui::MenuModel> context_menu_model_;
  std::unique_ptr<views::MenuModelAdapter> context_menu_adapter_;
  std::unique_ptr<views::MenuRunner> context_menu_runner_;

  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_EXTENSIONS_SECTION_H_
