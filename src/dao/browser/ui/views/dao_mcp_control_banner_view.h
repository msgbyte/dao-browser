// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"

class Browser;

namespace content {
class WebContents;
}  // namespace content

namespace views {
class MdTextButton;
}  // namespace views

namespace dao {

// Details bubble anchored to the MCP indicator in the address bar.
class DaoMcpControlBannerView final
    : public views::BubbleDialogDelegate,
      public views::View {
  METADATA_HEADER(DaoMcpControlBannerView, views::View)

 public:
  DaoMcpControlBannerView(views::View* anchor_view,
                         Browser* browser,
                         content::WebContents* target);
  ~DaoMcpControlBannerView() override;

  DaoMcpControlBannerView(const DaoMcpControlBannerView&) = delete;
  DaoMcpControlBannerView& operator=(const DaoMcpControlBannerView&) = delete;

  views::MdTextButton* stop_button_for_testing() { return stop_button_; }

  // views::BubbleDialogDelegate:
  views::View* GetContentsView() override;

  // views::View:
  views::Widget* GetWidget() override;
  const views::Widget* GetWidget() const override;

 private:
  void OnStopPressed();

  raw_ptr<Browser> browser_;
  raw_ptr<views::MdTextButton> stop_button_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_MCP_CONTROL_BANNER_VIEW_H_
