// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_SECTION_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_SECTION_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "ui/views/view.h"

namespace views {
class Label;
}

namespace dao {

class DaoSidebarSectionView : public views::View {
  METADATA_HEADER(DaoSidebarSectionView, views::View)

 public:
  explicit DaoSidebarSectionView(const std::u16string& title);
  DaoSidebarSectionView(const DaoSidebarSectionView&) = delete;
  DaoSidebarSectionView& operator=(const DaoSidebarSectionView&) = delete;
  ~DaoSidebarSectionView() override;

  views::View* AddTabItem(std::unique_ptr<views::View> item);
  views::View* content_container() { return content_container_; }

 private:
  raw_ptr<views::Label> header_label_ = nullptr;
  raw_ptr<views::View> content_container_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_SIDEBAR_SECTION_VIEW_H_
