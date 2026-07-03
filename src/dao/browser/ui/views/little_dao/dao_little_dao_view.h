// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace content {
class NavigationHandle;
}  // namespace content

namespace views {
class Button;
class Label;
class LabelButton;
}  // namespace views

namespace tabs {
class TabInterface;
}  // namespace tabs

namespace dao {

// Top bar for Little Dao windows. Shows an address display (clickable to open
// the command bar) and an "Open in Dao" button. The address display tracks
// the current page URL and clicking it opens the full command bar overlay.
class DaoLittleDaoView : public views::View,
                          public TabStripModelObserver,
                          public content::WebContentsObserver {
  METADATA_HEADER(DaoLittleDaoView, views::View)

 public:
  static constexpr int kBarHeight = 48;

  explicit DaoLittleDaoView(Browser* browser);
  DaoLittleDaoView(const DaoLittleDaoView&) = delete;
  DaoLittleDaoView& operator=(const DaoLittleDaoView&) = delete;
  ~DaoLittleDaoView() override;

  // Returns the bounds of the "Open in Dao" button in parent coordinates,
  // used for hit testing (so the button stays clickable in the title bar area).
  gfx::Rect open_in_dao_button_bounds() const;

  // Returns the bounds of the URL display button in parent coordinates.
  gfx::Rect url_display_bounds() const;

  // Returns the bounds of the site center button in parent coordinates.
  gfx::Rect site_center_button_bounds() const;

  views::View* site_center_button_for_testing() const;
  void ShowMiniDaoSiteCenterForTesting();

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;
  void OnTabChangedAt(tabs::TabInterface* tab,
                      int index,
                      TabChangeType change_type) override;

  // content::WebContentsObserver:
  void OnBackgroundColorChanged() override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;
  void AddedToWidget() override;
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

 private:
  void UpdateURLDisplay();
  void UpdateBackgroundColor();
  void ObserveActiveWebContents();
  void ShowCommandBar();
  void ShowMiniDaoSiteCenter();
  void OpenInDao();

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<views::View> url_container_ = nullptr;
  raw_ptr<views::LabelButton> url_text_button_ = nullptr;
  raw_ptr<views::Button> site_center_button_ = nullptr;
  raw_ptr<views::LabelButton> open_button_ = nullptr;
  raw_ptr<views::Label> shortcut_label_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_LITTLE_DAO_VIEW_H_
