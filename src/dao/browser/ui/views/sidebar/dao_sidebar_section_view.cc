// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_sidebar_section_view.h"

#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

BEGIN_METADATA(DaoSidebarSectionView)
END_METADATA

DaoSidebarSectionView::DaoSidebarSectionView(const std::u16string& title) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));

  if (!title.empty()) {
    header_label_ = AddChildView(std::make_unique<views::Label>(title));
    header_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    header_label_->SetFontList(gfx::FontList(
        {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 10,
        gfx::Font::Weight::SEMIBOLD));
    header_label_->SetEnabledColor(dao::kTextMuted);
    header_label_->SetSubpixelRenderingEnabled(false);
    header_label_->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(0, 6, 0, 6)));
  }

  content_container_ = AddChildView(std::make_unique<views::View>());
  content_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::VH(0, 6), 5));
}

DaoSidebarSectionView::~DaoSidebarSectionView() = default;

views::View* DaoSidebarSectionView::AddTabItem(
    std::unique_ptr<views::View> item) {
  return content_container_->AddChildView(std::move(item));
}

}  // namespace dao
