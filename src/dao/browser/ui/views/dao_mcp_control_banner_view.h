// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_

#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/mcp/dao_mcp_service.h"
#include "ui/views/view.h"

class Browser;

namespace content {
class NavigationEntry;
class Page;
}  // namespace content

namespace views {
class ImageView;
class Label;
class MdTextButton;
}  // namespace views

namespace dao {

class DaoMcpControlBannerView final : public views::View,
                                      public content::WebContentsObserver,
                                      public TabStripModelObserver {
  METADATA_HEADER(DaoMcpControlBannerView, views::View)

 public:
  explicit DaoMcpControlBannerView(Browser* browser);
  ~DaoMcpControlBannerView() override;

  DaoMcpControlBannerView(const DaoMcpControlBannerView&) = delete;
  DaoMcpControlBannerView& operator=(const DaoMcpControlBannerView&) = delete;

  void SetConnection(std::string_view client_name,
                     std::u16string_view tab_title);
  void ClearConnection();

  // content::WebContentsObserver:
  void TitleWasSet(content::NavigationEntry* entry) override;
  void PrimaryPageChanged(content::Page& page) override;
  void WebContentsDestroyed() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // views::View:
  void OnThemeChanged() override;

  views::MdTextButton* stop_button_for_testing() { return stop_button_; }

 private:
  void OnServiceStatusChanged(const DaoMcpServiceStatus& status);
  void RefreshFromService();
  void UpdateColors();
  void OnStopPressed();

  raw_ptr<Browser> browser_;
  raw_ptr<views::ImageView> icon_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> target_label_ = nullptr;
  raw_ptr<views::MdTextButton> stop_button_ = nullptr;
  base::CallbackListSubscription service_subscription_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_
