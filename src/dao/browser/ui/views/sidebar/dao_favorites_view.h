// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_FAVORITES_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_FAVORITES_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace dao {

// Horizontal row of small favicon-only buttons for pinned tabs.
class DaoFavoritesView : public views::View,
                         public TabStripModelObserver {
  METADATA_HEADER(DaoFavoritesView, views::View)

 public:
  explicit DaoFavoritesView(Browser* browser);
  DaoFavoritesView(const DaoFavoritesView&) = delete;
  DaoFavoritesView& operator=(const DaoFavoritesView&) = delete;
  ~DaoFavoritesView() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void TabChangedAt(content::WebContents* contents,
                    int index,
                    TabChangeType change_type) override;

 private:
  void RebuildFavorites();
  void OnFavoriteClicked(int index);

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_FAVORITES_VIEW_H_
