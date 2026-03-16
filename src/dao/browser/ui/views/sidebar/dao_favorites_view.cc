// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_favorites_view.h"

#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kIconSize = 20;
constexpr int kButtonSize = 30;
constexpr int kCornerRadius = 8;
}  // namespace

BEGIN_METADATA(DaoFavoritesView)
END_METADATA

DaoFavoritesView::DaoFavoritesView(Browser* browser)
    : browser_(browser),
      tab_strip_model_(browser->tab_strip_model()) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(8, 10), 4));

  tab_strip_model_->AddObserver(this);
  RebuildFavorites();
}

DaoFavoritesView::~DaoFavoritesView() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
}

void DaoFavoritesView::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  RebuildFavorites();
}

void DaoFavoritesView::TabChangedAt(content::WebContents* contents,
                                    int index,
                                    TabChangeType change_type) {
  if (tab_strip_model_->IsTabPinned(index)) {
    RebuildFavorites();
  }
}

void DaoFavoritesView::RebuildFavorites() {
  RemoveAllChildViews();

  const int count = tab_strip_model_->count();
  bool has_pinned = false;

  for (int i = 0; i < count; ++i) {
    if (!tab_strip_model_->IsTabPinned(i)) {
      continue;
    }
    has_pinned = true;

    auto* contents = tab_strip_model_->GetWebContentsAt(i);
    auto icon_btn = std::make_unique<views::ImageButton>(
        base::BindRepeating(&DaoFavoritesView::OnFavoriteClicked,
                            base::Unretained(this), i));

    if (contents) {
      gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
      if (!favicon.IsEmpty()) {
        gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
            *favicon.ToImageSkia(), skia::ImageOperations::RESIZE_BEST,
            gfx::Size(kIconSize, kIconSize));
        icon_btn->SetImageModel(
            views::Button::STATE_NORMAL,
            ui::ImageModel::FromImageSkia(resized));
      }
    }

    icon_btn->SetPreferredSize(gfx::Size(kButtonSize, kButtonSize));
    icon_btn->SetBackground(views::CreateRoundedRectBackground(
        dao::kActiveTabBackground, kCornerRadius));
    icon_btn->SetInstallFocusRingOnFocus(false);

    views::InkDrop::Get(icon_btn.get())
        ->SetMode(views::InkDropHost::InkDropMode::ON);
    views::InkDrop::Get(icon_btn.get())->SetBaseColor(dao::kInkDropBase);
    views::InkDrop::Get(icon_btn.get())
        ->SetVisibleOpacity(dao::kInkDropOpacity);

    icon_btn->SetAccessibleName(
        contents ? contents->GetTitle() : u"Pinned Tab");
    AddChildView(std::move(icon_btn));
  }

  SetVisible(has_pinned);
  InvalidateLayout();
}

void DaoFavoritesView::OnFavoriteClicked(int index) {
  if (tab_strip_model_ && index >= 0 && index < tab_strip_model_->count()) {
    tab_strip_model_->ActivateTabAt(index);
  }
}

}  // namespace dao
