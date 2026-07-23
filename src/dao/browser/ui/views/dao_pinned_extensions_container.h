// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_PINNED_EXTENSIONS_CONTAINER_H_
#define DAO_BROWSER_UI_VIEWS_DAO_PINNED_EXTENSIONS_CONTAINER_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

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

// Displays pinned extension icons in a horizontal row within the address bar,
// positioned between the URL pill and the chat button.
class DaoPinnedExtensionsContainer
    : public views::View,
      public ToolbarActionsModel::Observer,
      public TabStripModelObserver,
      public views::ContextMenuController,
      public extensions::ExtensionActionIconFactory::Observer {
  METADATA_HEADER(DaoPinnedExtensionsContainer, views::View)

 public:
  explicit DaoPinnedExtensionsContainer(Browser* browser);
  DaoPinnedExtensionsContainer(const DaoPinnedExtensionsContainer&) = delete;
  DaoPinnedExtensionsContainer& operator=(const DaoPinnedExtensionsContainer&) =
      delete;
  ~DaoPinnedExtensionsContainer() override;

  // Returns bounding rects of all visible icon buttons in local coordinates.
  std::vector<gfx::Rect> GetButtonRects() const;

  // Returns the button view for a given extension_id, or nullptr.
  views::View* GetButtonForExtension(const std::string& extension_id) const;

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;

  // views::ContextMenuController:
  void ShowContextMenuForViewImpl(
      views::View* source,
      const gfx::Point& point,
      ui::mojom::MenuSourceType source_type) override;

  // extensions::ExtensionActionIconFactory::Observer:
  void OnIconUpdated() override;

 private:
  static constexpr int kMaxVisiblePinned = 5;
  static constexpr int kPinnedButtonSize = 24;
  static constexpr int kPinnedButtonRadius = 6;

  void Rebuild();
  gfx::ImageSkia GetIconForExtension(const std::string& extension_id,
                                     const extensions::Extension& extension);
  bool UpdateButtonIcon(const std::string& extension_id);
  void UpdateAllButtonIcons();
  void OnExtensionClicked(const std::string& extension_id);
  std::string GetExtensionIdForView(views::View* view) const;
  void OnContextMenuClosed();

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_ = nullptr;
  raw_ptr<ToolbarActionsModel> model_ = nullptr;

  // Maps button pointers to extension IDs for context menu lookup.
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

#endif  // DAO_BROWSER_UI_VIEWS_DAO_PINNED_EXTENSIONS_CONTAINER_H_
