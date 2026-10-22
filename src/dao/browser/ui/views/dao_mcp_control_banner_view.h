// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_

#include <string>
#include <utility>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

class BrowserWindowInterface;

namespace views {
class Label;
class MdTextButton;
}  // namespace views

namespace dao {

struct DaoMcpConnectionInfo;

// Details bubble anchored to the MCP indicator in the address bar.
class DaoMcpControlBannerView final
    : public views::BubbleDialogDelegate,
      public views::View {
  METADATA_HEADER(DaoMcpControlBannerView, views::View)

 public:
  DaoMcpControlBannerView(views::View* anchor_view, BrowserWindowInterface* browser);
  ~DaoMcpControlBannerView() override;

  DaoMcpControlBannerView(const DaoMcpControlBannerView&) = delete;
  DaoMcpControlBannerView& operator=(const DaoMcpControlBannerView&) = delete;

  views::MdTextButton* stop_button_for_testing() { return stop_button_; }

  // Returns false when permission rows have changed and the bubble must close.
  bool UpdateConnections();

  // views::BubbleDialogDelegate:
  views::View* GetContentsView() override;

  // views::View:
  views::Widget* GetWidget() override;
  const views::Widget* GetWidget() const override;

 private:
  void OnStopPressed(std::string connection_id);

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::MdTextButton> stop_button_ = nullptr;
  std::vector<DaoMcpConnectionInfo> connections_;
  std::vector<raw_ptr<views::Label>> latest_tool_labels_;
  // Title and URL labels for each granted tab, in connection order.
  std::vector<std::pair<raw_ptr<views::Label>, raw_ptr<views::Label>>>
      tab_labels_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_
