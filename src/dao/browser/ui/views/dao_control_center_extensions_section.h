// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_EXTENSIONS_SECTION_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_EXTENSIONS_SECTION_H_

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "ui/views/view.h"

class Browser;

namespace dao {

// Displays installed extension icons in a wrapping grid.
// Includes "Add" and "Manage" shortcut buttons at the bottom.
class DaoControlCenterExtensionsSection
    : public views::View,
      public ToolbarActionsModel::Observer {
  METADATA_HEADER(DaoControlCenterExtensionsSection, views::View)

 public:
  explicit DaoControlCenterExtensionsSection(Browser* browser);
  DaoControlCenterExtensionsSection(
      const DaoControlCenterExtensionsSection&) = delete;
  DaoControlCenterExtensionsSection& operator=(
      const DaoControlCenterExtensionsSection&) = delete;
  ~DaoControlCenterExtensionsSection() override;

  // Rebuild the extensions grid from the current model.
  void Refresh();

  // ToolbarActionsModel::Observer:
  void OnToolbarActionAdded(const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionRemoved(
      const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarActionUpdated(
      const ToolbarActionsModel::ActionId& id) override;
  void OnToolbarModelInitialized() override;
  void OnToolbarPinnedActionsChanged() override;

 private:
  void RebuildGrid();
  void OnExtensionClicked(const std::string& extension_id);
  void OnAddClicked();
  void OnManageClicked();

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> grid_ = nullptr;
  raw_ptr<views::View> buttons_row_ = nullptr;
  raw_ptr<ToolbarActionsModel> model_ = nullptr;
  bool grid_dirty_ = true;

  base::ScopedObservation<ToolbarActionsModel, ToolbarActionsModel::Observer>
      observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_EXTENSIONS_SECTION_H_
