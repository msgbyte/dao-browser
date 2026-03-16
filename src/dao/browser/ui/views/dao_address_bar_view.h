// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_ADDRESS_BAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_ADDRESS_BAR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace views {
class Label;
}

namespace dao {

// A minimal address bar displayed at the top of the content area, showing
// the current page URL with host and path in different colors.
class DaoAddressBarView : public views::View,
                          public TabStripModelObserver,
                          public content::WebContentsObserver {
  METADATA_HEADER(DaoAddressBarView, views::View)

 public:
  static constexpr int kBarHeight = 28;

  explicit DaoAddressBarView(Browser* browser);
  DaoAddressBarView(const DaoAddressBarView&) = delete;
  DaoAddressBarView& operator=(const DaoAddressBarView&) = delete;
  ~DaoAddressBarView() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void TabChangedAt(content::WebContents* contents,
                    int index,
                    TabChangeType change_type) override;

  // content::WebContentsObserver:
  void OnBackgroundColorChanged() override;

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available_size) const override;

 private:
  void UpdateURL();
  void UpdateBackgroundColor();
  void ObserveActiveWebContents();

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<views::Label> host_label_ = nullptr;
  raw_ptr<views::Label> path_label_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_ADDRESS_BAR_VIEW_H_
