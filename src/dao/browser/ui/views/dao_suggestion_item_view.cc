// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_suggestion_item_view.h"

#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/core/favicon_service.h"
#include "components/favicon_base/favicon_callback.h"
#include "components/keyed_service/core/service_access_type.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
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

  auto intent = std::make_unique<views::Label>();
  intent->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 10,
                                    gfx::Font::Weight::MEDIUM));
  intent->SetEnabledColor(TextMuted());
  intent->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  intent->SetElideBehavior(gfx::ELIDE_TAIL);
  intent->SetSubpixelRenderingEnabled(false);
  intent->SetBackgroundColor(SK_ColorTRANSPARENT);
  intent->SetVisible(false);
  intent_label_ = AddChildView(std::move(intent));
}

DaoSuggestionItemView::~DaoSuggestionItemView() = default;

void DaoSuggestionItemView::SetMatch(const AutocompleteMatch& match,
                                     bool is_bookmark,
                                     const std::u16string& intent_label) {
  // Async autocomplete ticks frequently re-emit the same match for the
  // same row. Bail out early when the visible content is unchanged to
  // avoid redundant text relayout, vector icon rasterization, and
  // favicon fetches that would otherwise fire on every tick.
  if (!last_was_ask_ai_ && last_match_url_ == match.destination_url &&
      last_match_contents_ == match.contents &&
      last_match_description_ == match.description &&
      last_match_intent_label_ == intent_label &&
      last_match_is_bookmark_ == is_bookmark) {
    return;
  }
  last_was_ask_ai_ = false;
  last_match_url_ = match.destination_url;
  last_match_contents_ = match.contents;
  last_match_description_ = match.description;
  last_match_intent_label_ = intent_label;
  last_match_is_bookmark_ = is_bookmark;
  last_ask_ai_prompt_.clear();
  last_ask_ai_intent_label_.clear();

  // Always set the vector icon first as immediate fallback
  const gfx::VectorIcon& vector_icon = match.GetVectorIcon(is_bookmark);
  icon_view_->SetImage(ui::ImageModel::FromImageSkia(
      gfx::CreateVectorIcon(vector_icon, 18, SuggestionIconColor())));
  icon_mode_ = IconMode::kVectorMatch;
  current_vector_icon_ = &vector_icon;
  has_favicon_ = false;

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
  intent_label_->SetText(intent_label);
  intent_label_->SetVisible(!intent_label.empty());
}

void DaoSuggestionItemView::SetAskAiPrompt(
    const std::u16string& prompt,
    const std::u16string& intent_label) {
  // Cheap no-op when the rendered Ask-AI prompt is unchanged across ticks.
  if (last_was_ask_ai_ && last_ask_ai_prompt_ == prompt &&
      last_ask_ai_intent_label_ == intent_label) {
    return;
  }
  last_was_ask_ai_ = true;
  last_ask_ai_prompt_ = prompt;
  last_ask_ai_intent_label_ = intent_label;
  last_match_url_ = GURL();
  last_match_contents_.clear();
  last_match_description_.clear();
  last_match_intent_label_.clear();
  last_match_is_bookmark_ = false;

  favicon_tracker_.TryCancelAll();
  pending_favicon_url_ = GURL();

  icon_view_->SetImage(ui::ImageModel::FromImageSkia(
      CreateLucideImageSkia(LucideIcon::kSparkles, 18, SuggestionIconColor())));
  icon_mode_ = IconMode::kAskAi;
  current_vector_icon_ = nullptr;
  has_favicon_ = false;

  title_label_->SetText(
      l10n_util::GetStringFUTF16(IDS_DAO_SUGGESTION_ASK_AI, prompt));
  description_label_->SetVisible(false);
  intent_label_->SetText(intent_label);
  intent_label_->SetVisible(!intent_label.empty());
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
  icon_view_->SetImage(ui::ImageModel::FromImageSkia(resized));
  has_favicon_ = true;
}

void DaoSuggestionItemView::RefreshTheme() {
  // Labels cache their color via SetEnabledColor; reapply with the current
  // theme.
  if (title_label_) {
    title_label_->SetEnabledColor(SuggestionTitleColor());
  }
  if (description_label_) {
    description_label_->SetEnabledColor(TextMuted());
  }
  if (intent_label_) {
    intent_label_->SetEnabledColor(TextMuted());
  }

  // Vector icons are rasterized at SetMatch / SetAskAiPrompt time with the
  // theme color baked in; re-rasterize with the new color. Favicons are
  // real site images and intentionally stay theme-independent.
  if (!icon_view_ || has_favicon_) {
    return;
  }
  const SkColor icon_color = SuggestionIconColor();
  if (icon_mode_ == IconMode::kVectorMatch && current_vector_icon_) {
    icon_view_->SetImage(ui::ImageModel::FromImageSkia(
        gfx::CreateVectorIcon(*current_vector_icon_, 18, icon_color)));
  } else if (icon_mode_ == IconMode::kAskAi) {
    icon_view_->SetImage(ui::ImageModel::FromImageSkia(
        CreateLucideImageSkia(LucideIcon::kSparkles, 18, icon_color)));
  }
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
