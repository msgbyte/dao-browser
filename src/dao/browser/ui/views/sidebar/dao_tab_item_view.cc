// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_tab_item_view.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "url/gurl.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kItemHeight = 40;
constexpr int kCornerRadius = 12;
}  // namespace

BEGIN_METADATA(DaoTabItemView)
END_METADATA

DaoTabItemView::DaoTabItemView(content::WebContents* contents,
                               int model_index,
                               bool is_active,
                               base::RepeatingClosure on_click,
                               base::RepeatingClosure on_close)
    : Button(std::move(on_click)),
      model_index_(model_index),
      close_callback_(std::move(on_close)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, 10), 10));

  favicon_ = AddChildView(std::make_unique<views::ImageView>());
  favicon_->SetImageSize(gfx::Size(16, 16));
  UpdateFavicon(contents);

  // Vertical container for title (and URL on active tab)
  auto text_container = std::make_unique<views::View>();
  text_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 1));

  title_label_ = text_container->AddChildView(std::make_unique<views::Label>(
      contents ? contents->GetTitle() : u"New Tab"));
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  title_label_->SetSubpixelRenderingEnabled(false);
  title_label_->SetEnabledColor(
      is_active ? dao::kTextPrimary : dao::kTextSecondary);
  title_label_->SetFontList(gfx::FontList(
      {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 13,
      gfx::Font::Weight::SEMIBOLD));

  // Active tab: show hostname below title
  if (is_active && contents) {
    GURL url = contents->GetVisibleURL();
    std::u16string host = base::UTF8ToUTF16(url.host());
    if (!host.empty()) {
      url_label_ = text_container->AddChildView(
          std::make_unique<views::Label>(host));
      url_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      url_label_->SetElideBehavior(gfx::ELIDE_TAIL);
      url_label_->SetSubpixelRenderingEnabled(false);
      url_label_->SetEnabledColor(dao::kTextMuted);
      url_label_->SetFontList(gfx::FontList(
          {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 10,
          gfx::Font::Weight::NORMAL));
    }
  }

  auto* text_container_ptr = AddChildView(std::move(text_container));
  layout->SetFlexForView(text_container_ptr, 1);

  // Close button, hidden by default
  auto close_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoTabItemView::OnCloseClicked,
                          base::Unretained(this)),
      u"\u00D7");
  close_btn->SetEnabledTextColors(dao::kTextMuted);
  close_btn->SetTextSubpixelRenderingEnabled(false);
  close_btn->SetPreferredSize(gfx::Size(18, 18));
  close_btn->SetInstallFocusRingOnFocus(false);
  close_button_ = AddChildView(std::move(close_btn));
  close_button_->SetVisible(false);

  if (is_active) {
    SetBackground(views::CreateRoundedRectBackground(
        dao::kActiveTabBackground, kCornerRadius));
  }

  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(this)->SetBaseColor(dao::kInkDropBase);
  views::InkDrop::Get(this)->SetVisibleOpacity(dao::kInkDropOpacity);
  SetInstallFocusRingOnFocus(false);

  SetPreferredSize(gfx::Size(0, kItemHeight));
  std::u16string accessible_title =
      contents ? contents->GetTitle() : u"New Tab";
  if (accessible_title.empty()) {
    accessible_title = u"Tab";
  }
  SetAccessibleName(accessible_title);
}

DaoTabItemView::~DaoTabItemView() = default;

void DaoTabItemView::OnMouseEntered(const ui::MouseEvent& event) {
  Button::OnMouseEntered(event);
  if (close_button_) {
    close_button_->SetVisible(true);
  }
}

void DaoTabItemView::OnMouseExited(const ui::MouseEvent& event) {
  Button::OnMouseExited(event);
  if (close_button_) {
    close_button_->SetVisible(false);
  }
}

void DaoTabItemView::OnCloseClicked() {
  if (close_callback_) {
    close_callback_.Run();
  }
}

void DaoTabItemView::UpdateFavicon(content::WebContents* contents) {
  if (!contents) {
    return;
  }
  gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
  if (!favicon.IsEmpty()) {
    gfx::ImageSkia resized = gfx::ImageSkiaOperations::CreateResizedImage(
        *favicon.ToImageSkia(), skia::ImageOperations::RESIZE_BEST,
        gfx::Size(16, 16));
    favicon_->SetImage(resized);
  }
}

}  // namespace dao
