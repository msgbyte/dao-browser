// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Label;
class Textfield;
}

namespace dao {

class DaoSuggestionItemView;

class DaoCommandBarView : public views::View,
                          public views::TextfieldController,
                          public AutocompleteController::Observer,
                          public views::FocusChangeListener,
                          public ui::NativeThemeObserver {
  METADATA_HEADER(DaoCommandBarView, views::View)

 public:
  explicit DaoCommandBarView(Browser* browser);
  DaoCommandBarView(const DaoCommandBarView&) = delete;
  DaoCommandBarView& operator=(const DaoCommandBarView&) = delete;
  ~DaoCommandBarView() override;

  // Show for Cmd+L: pre-fills current URL, Esc just hides.
  void Show();
  // Show for Cmd+T / new tab button: empty textfield, highlights the
  // new-tab button.  Esc dismisses without creating a tab.  Enter creates
  // a new tab and navigates.
  void ShowForNewTab();
  void Hide();

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void AddedToWidget() override;
  void RemovedFromWidget() override;

  // views::FocusChangeListener:
  void OnWillChangeFocus(views::View* focused_before,
                         views::View* focused_now) override;
  void OnDidChangeFocus(views::View* focused_before,
                        views::View* focused_now) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;
  void ContentsChanged(views::Textfield* sender,
                       const std::u16string& new_contents) override;

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // URL detection heuristic (public for testing).
  static bool LooksLikeURL(const std::u16string& text);

 private:
  static constexpr int kMaxSuggestions = 5;

  void Navigate(const std::u16string& text);
  void NavigateToMatch(const AutocompleteMatch& match);
  void SubmitAskAi(const std::u16string& prompt);
  void DeferredRequestFocus();
  void Dismiss();
  void ApplyTheme();

  void CancelNewTab();
  void SetNewTabButtonHighlight(bool highlighted);
  void SetWebContentEventProcessing(bool enabled);

  void InitAutocompleteController();
  void StartAutocomplete(const std::u16string& text);
  void StopAutocomplete();
  void UpdateSuggestions();
  void UpdateGhostText();
  void PositionGhostText();
  void UpdateInputIcon();
  void OnInputFaviconFetched(const GURL& page_url,
                             const favicon_base::FaviconImageResult& result);
  void ApplySelectedSuggestion();
  void SetSelectedIndex(int index);
  void OnSuggestionClicked(int index);

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> shadow_view_ = nullptr;
  raw_ptr<views::View> glass_container_ = nullptr;
  raw_ptr<views::View> card_container_ = nullptr;
  raw_ptr<views::ImageView> favicon_icon_ = nullptr;
  raw_ptr<views::Textfield> textfield_ = nullptr;
  raw_ptr<views::Label> ghost_text_label_ = nullptr;
  raw_ptr<views::View> dropdown_container_ = nullptr;

  std::vector<raw_ptr<DaoSuggestionItemView>> suggestion_views_;

  std::unique_ptr<AutocompleteController> autocomplete_controller_;
  std::unique_ptr<ChromeAutocompleteSchemeClassifier> scheme_classifier_;

  int selected_index_ = -1;
  std::u16string user_input_text_;
  std::u16string inline_autocompletion_;
  bool updating_textfield_ = false;
  int visible_suggestion_count_ = 0;

  // Index inside suggestion_views_ of the synthetic "Ask AI" row, or -1
  // when the current input does not qualify (empty / looks like a URL) or
  // there is no room for it below the autocomplete matches.  The row
  // occupies one of the kMaxSuggestions slots; Enter / click on it routes
  // to SubmitAskAi instead of NavigateToMatch.
  int ask_ai_row_index_ = -1;

  // When true, we are in "pre-new-tab" mode: no tab has been created yet.
  bool is_new_tab_mode_ = false;

  // Tracks the URL for the current input icon favicon request.
  GURL pending_icon_favicon_url_;
  base::CancelableTaskTracker icon_favicon_tracker_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};

  base::WeakPtrFactory<DaoCommandBarView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_
