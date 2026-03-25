// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_ADDRESS_BAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_ADDRESS_BAR_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"

class Browser;
class TabStripModel;

namespace content {
class NavigationHandle;
}

namespace views {
class Button;
class Label;
class LabelButton;
}

namespace dao {
class DaoControlCenterButton;
}

namespace dao {

// A minimal address bar displayed at the top of the content area, showing
// the current page URL with host and path in different colors.
class DaoAddressBarView : public views::View,
                          public TabStripModelObserver,
                          public content::WebContentsObserver {
  METADATA_HEADER(DaoAddressBarView, views::View)

 public:
  static constexpr int kBarHeight = 30;

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
  void DidStartLoading() override;
  void DidStopLoading() override;
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  // Called by the sidebar to update toggle button visibility.
  void SetSidebarCollapsed(bool collapsed);

  // Set the callback invoked when the toggle button is clicked.
  void set_toggle_callback(base::RepeatingClosure callback) {
    toggle_callback_ = std::move(callback);
  }

  // Returns the control center button for popup anchoring.
  views::View* control_center_button() const;

  // views::View:
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  gfx::Size CalculatePreferredSize(const views::SizeBounds& available_size) const override;

 private:
  void UpdateURL();
  void UpdateBackgroundColor();
  void UpdateToggleButtonColor();
  void UpdateNavButtonColors();
  void UpdateNavButtonEnabled();
  void UpdateStopRefreshButton();
  void UpdateUrlContainerHover(bool hovered);
  void ObserveActiveWebContents();
  void OnToggleButtonPressed();
  void OnBackButtonPressed();
  void OnForwardButtonPressed();
  void OnStopRefreshButtonPressed();
  void OnChatButtonPressed();

  raw_ptr<Browser> browser_;
  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<views::View> traffic_light_spacer_ = nullptr;
  raw_ptr<views::View> left_spacer_ = nullptr;
  raw_ptr<views::LabelButton> sidebar_toggle_button_ = nullptr;
  raw_ptr<views::Button> back_button_ = nullptr;
  raw_ptr<views::Button> forward_button_ = nullptr;
  raw_ptr<views::Button> stop_refresh_button_ = nullptr;
  raw_ptr<views::Button> chat_button_ = nullptr;
  raw_ptr<views::View> url_container_ = nullptr;
  raw_ptr<views::Label> host_label_ = nullptr;
  raw_ptr<views::Label> path_label_ = nullptr;
  raw_ptr<DaoControlCenterButton> control_center_button_ = nullptr;
  base::RepeatingClosure toggle_callback_;
  bool sidebar_collapsed_ = false;
  bool url_hovered_ = false;
  bool is_loading_ = false;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_ADDRESS_BAR_VIEW_H_
