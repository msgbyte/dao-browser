// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_SUGGESTION_ITEM_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_SUGGESTION_ITEM_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "ui/views/view.h"

class Profile;

namespace favicon {
class FaviconService;
}

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace dao {

class DaoSuggestionItemView : public views::View {
  METADATA_HEADER(DaoSuggestionItemView, views::View)

 public:
  using ClickCallback = base::RepeatingCallback<void(int index)>;

  DaoSuggestionItemView(int index,
                         ClickCallback click_callback,
                         Profile* profile);
  DaoSuggestionItemView(const DaoSuggestionItemView&) = delete;
  DaoSuggestionItemView& operator=(const DaoSuggestionItemView&) = delete;
  ~DaoSuggestionItemView() override;

  void SetMatch(const AutocompleteMatch& match, bool is_bookmark);

  // Render this row as an "Ask AI" entry for |prompt| instead of a normal
  // autocomplete match. Cancels any pending favicon load and paints a
  // sparkle icon on the left.
  void SetAskAiPrompt(const std::u16string& prompt);

  void SetSelected(bool selected);

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  void UpdateBackground();
  void OnFaviconFetched(const GURL& page_url,
                        const favicon_base::FaviconImageResult& result);

  int index_;
  ClickCallback click_callback_;
  bool is_selected_ = false;
  bool is_hovered_ = false;

  raw_ptr<Profile> profile_;
  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> description_label_ = nullptr;

  // Tracks the URL for the current favicon request so stale callbacks are
  // ignored.
  GURL pending_favicon_url_;
  base::CancelableTaskTracker favicon_tracker_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_SUGGESTION_ITEM_VIEW_H_
