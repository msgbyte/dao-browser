// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_ITEM_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_ITEM_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/views/controls/button/button.h"

namespace content {
class WebContents;
}

namespace views {
class ImageView;
class Label;
class LabelButton;
}  // namespace views

namespace dao {

class DaoTabItemView : public views::Button {
  METADATA_HEADER(DaoTabItemView, views::Button)

 public:
  DaoTabItemView(content::WebContents* contents,
                 int model_index,
                 bool is_active,
                 base::RepeatingClosure on_click,
                 base::RepeatingClosure on_close);
  DaoTabItemView(const DaoTabItemView&) = delete;
  DaoTabItemView& operator=(const DaoTabItemView&) = delete;
  ~DaoTabItemView() override;

  int model_index() const { return model_index_; }

 protected:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;

 private:
  void UpdateFavicon(content::WebContents* contents);
  void OnCloseClicked();

  int model_index_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> url_label_ = nullptr;
  raw_ptr<views::LabelButton> close_button_ = nullptr;
  base::RepeatingClosure close_callback_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_ITEM_VIEW_H_
