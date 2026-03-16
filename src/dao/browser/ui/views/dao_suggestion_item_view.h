// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_SUGGESTION_ITEM_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_SUGGESTION_ITEM_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "ui/views/view.h"

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace dao {

class DaoSuggestionItemView : public views::View {
  METADATA_HEADER(DaoSuggestionItemView, views::View)

 public:
  using ClickCallback = base::RepeatingCallback<void(int index)>;

  explicit DaoSuggestionItemView(int index, ClickCallback click_callback);
  DaoSuggestionItemView(const DaoSuggestionItemView&) = delete;
  DaoSuggestionItemView& operator=(const DaoSuggestionItemView&) = delete;
  ~DaoSuggestionItemView() override;

  void SetMatch(const AutocompleteMatch& match, bool is_bookmark);
  void SetSelected(bool selected);

  // views::View:
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  void UpdateBackground();

  int index_;
  ClickCallback click_callback_;
  bool is_selected_ = false;
  bool is_hovered_ = false;

  raw_ptr<views::ImageView> icon_view_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::Label> description_label_ = nullptr;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_SUGGESTION_ITEM_VIEW_H_
