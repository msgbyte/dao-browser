// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_suggestion_item_view.h"

#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/keyed_service/core/service_access_type.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

BEGIN_METADATA(DaoSuggestionItemView)
END_METADATA

DaoSuggestionItemView::DaoSuggestionItemView(int index,
                                               ClickCallback click_callback,
                                               Profile* profile)
    : index_(index),
      click_callback_(std::move(click_callback)),
      profile_(profile) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, 12, 0, 12), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  SetPreferredSize(gfx::Size(0, 40));

  // Icon
  auto icon = std::make_unique<views::ImageView>();
  icon->SetPreferredSize(gfx::Size(24, 24));
  icon_view_ = AddChildView(std::move(icon));

  // Text container (title + description in a row)
  auto* text_container = AddChildView(std::make_unique<views::View>());
  auto* text_layout =
      text_container->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  text_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  layout->SetFlexForView(text_container, 1);

  // Title label
  auto title = std::make_unique<views::Label>();
  title->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 13,
                                    gfx::Font::Weight::NORMAL));
  title->SetEnabledColor(SuggestionTitleColor());
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  title->SetSubpixelRenderingEnabled(false);
  title->SetBackgroundColor(SK_ColorTRANSPARENT);
  title_label_ = text_container->AddChildView(std::move(title));
  text_layout->SetFlexForView(title_label_, 1);

  // Description label
  auto desc = std::make_unique<views::Label>();
  desc->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 11,
                                   gfx::Font::Weight::NORMAL));
  desc->SetEnabledColor(TextMuted());
  desc->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  desc->SetElideBehavior(gfx::ELIDE_TAIL);
  desc->SetSubpixelRenderingEnabled(false);
  desc->SetBackgroundColor(SK_ColorTRANSPARENT);
  description_label_ = text_container->AddChildView(std::move(desc));
  text_layout->SetFlexForView(description_label_, 1);
}

DaoSuggestionItemView::~DaoSuggestionItemView() = default;

void DaoSuggestionItemView::SetMatch(const AutocompleteMatch& match,
                                      bool is_bookmark) {
  // Always set the vector icon first as immediate fallback
  const gfx::VectorIcon& vector_icon = match.GetVectorIcon(is_bookmark);
  icon_view_->SetImage(
      gfx::CreateVectorIcon(vector_icon, 18, SuggestionIconColor()));

  // Cancel any pending favicon request
  favicon_tracker_.TryCancelAll();
  pending_favicon_url_ = GURL();

  // For URL-type matches, try to load the site's favicon
  if (!AutocompleteMatch::IsSearchType(match.type) &&
      match.destination_url.is_valid() &&
      match.destination_url.SchemeIsHTTPOrHTTPS()) {
    favicon::FaviconService* favicon_service =
        FaviconServiceFactory::GetForProfile(
            profile_, ServiceAccessType::EXPLICIT_ACCESS);
    if (favicon_service) {
      pending_favicon_url_ = match.destination_url;
      favicon_service->GetFaviconImageForPageURL(
          match.destination_url,
          base::BindOnce(&DaoSuggestionItemView::OnFaviconFetched,
                         base::Unretained(this), match.destination_url),
          &favicon_tracker_);
    }
  }

  // Set text
  title_label_->SetText(match.contents);

  if (match.description.empty()) {
    description_label_->SetVisible(false);
  } else {
    description_label_->SetVisible(true);
    description_label_->SetText(match.description);
  }
}

void DaoSuggestionItemView::OnFaviconFetched(
    const GURL& page_url,
    const favicon_base::FaviconImageResult& result) {
  // Ignore stale callbacks (match has changed since request was made)
  if (page_url != pending_favicon_url_) {
    return;
  }
  pending_favicon_url_ = GURL();

  if (result.image.IsEmpty()) {
    return;  // Keep the vector icon fallback
  }

  // Resize favicon to fit the icon area (18x18 to match vector icon size)
  gfx::ImageSkia favicon = result.image.AsImageSkia();
  gfx::ImageSkia resized =
      gfx::ImageSkiaOperations::CreateResizedImage(
          favicon, skia::ImageOperations::RESIZE_BEST, gfx::Size(18, 18));
  icon_view_->SetImage(resized);
}

void DaoSuggestionItemView::SetSelected(bool selected) {
  if (is_selected_ == selected) {
    return;
  }
  is_selected_ = selected;
  UpdateBackground();
}

void DaoSuggestionItemView::OnMouseEntered(const ui::MouseEvent& event) {
  is_hovered_ = true;
  UpdateBackground();
}

void DaoSuggestionItemView::OnMouseExited(const ui::MouseEvent& event) {
  is_hovered_ = false;
  UpdateBackground();
}

bool DaoSuggestionItemView::OnMousePressed(const ui::MouseEvent& event) {
  if (click_callback_) {
    click_callback_.Run(index_);
  }
  return true;
}

void DaoSuggestionItemView::UpdateBackground() {
  if (is_selected_) {
    SetBackground(views::CreateSolidBackground(SuggestionSelected()));
  } else if (is_hovered_) {
    SetBackground(views::CreateSolidBackground(SuggestionHover()));
  } else {
    SetBackground(nullptr);
  }
  SchedulePaint();
}

}  // namespace dao
