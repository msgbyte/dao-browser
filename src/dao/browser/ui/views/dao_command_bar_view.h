// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "components/favicon_base/favicon_types.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class ScrollView;
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

  void SetUserInputAndInlineAutocompletionForTesting(
      const std::u16string& user_input,
      const std::u16string& inline_autocompletion);
  void SetAutocompleteMatchesForTesting(const ACMatches& matches);
  void SetAutocompleteMatchesForTesting(const ACMatches& matches,
                                        bool autocomplete_done);
  void SetAutocompleteMatchesForTesting(
      const ACMatches& matches,
      AutocompleteController::UpdateType update_type);
  const std::u16string& GetInlineAutocompletionForTesting() const {
    return inline_autocompletion_;
  }
  const std::u16string& GetUserInputTextForTesting() const {
    return user_input_text_;
  }
  const std::u16string& GetSelectionPreviewTextForTesting() const {
    return selection_preview_text_;
  }
  bool IsSelectionPreviewActiveForTesting() const {
    return selection_preview_active_;
  }
  int GetAutocompleteStartCountForTesting() const {
    return autocomplete_start_count_for_testing_;
  }
  int GetAutocompleteProviderTypesForTesting() const {
    return GetAutocompleteProviderTypesForCurrentMode();
  }
  int GetAskAiRowIndexForTesting() const { return ask_ai_row_index_; }
  int GetSelectedIndexForTesting() const { return selected_index_; }
  int GetVisibleSuggestionCountForTesting() const {
    return visible_suggestion_count_;
  }

 private:
  static constexpr int kVisibleSuggestionRows = 5;

  void Navigate(const std::u16string& text);
  void NavigateToMatch(const AutocompleteMatch& match);
  // Feeds the accepted input -> match association into the shortcuts
  // database so ShortcutsProvider ranks this destination higher for the
  // same prefix next time (mirrors ChromeOmniboxClient's behavior).
  void RecordShortcut(const AutocompleteMatch& match);
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
  void ClearSuggestions();
  void UpdateSuggestions();
  void UpdateSelectionPreview();
  void ClearSelectionPreview(bool restore_user_input);
  void AcceptSelectionPreview();
  std::u16string GetSelectionPreviewText() const;
  void UpdateInlineAutocompletion();
  void ClearInlineAutocompletion(bool restore_user_input);
  void UpdateInputIcon();
  void FillInput(const std::u16string& text);

  // Derives inline completion from the current default action when no
  // selection preview is active. Returns the empty string when nothing applies
  // or suggested text is suppressed for the current query.
  std::u16string GetInlineAutocompletionForResult() const;
  bool IsAutocompleteResultStableForInlineAutocompletion() const;
  bool HasSubmittableInlineAutocompletion() const;
  std::u16string GetInlineAutocompletedInputText() const;
  const AutocompleteMatch* GetVisibleInlineAutocompletionMatch() const;
  void OnInputFaviconFetched(const GURL& page_url,
                             const favicon_base::FaviconImageResult& result);
  void ApplySelectedSuggestion();
  void SetSelectedIndex(int index, bool user_initiated);
  void OnSuggestionClicked(int index);
  bool EnhancedSuggestionsEnabled() const;
  bool AskAiEnabled() const;
  bool ShouldShowAskAiSuggestion() const;
  int GetAutocompleteProviderTypesForCurrentMode() const;
  const AutocompleteMatch* GetSelectedVisibleAutocompleteMatch() const;
  std::u16string GetIntentLabelForMatch(const AutocompleteMatch& match) const;
  GURL GetSearchUrl(const std::u16string& search_terms) const;
  AutocompleteMatch CreateExactSearchMatch(
      const std::u16string& search_terms) const;
  bool IsExactSearchMatch(const AutocompleteMatch& match,
                          const std::u16string& search_terms) const;

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> shadow_view_ = nullptr;
  raw_ptr<views::View> glass_container_ = nullptr;
  raw_ptr<views::View> card_container_ = nullptr;
  raw_ptr<views::ImageView> favicon_icon_ = nullptr;
  raw_ptr<views::Textfield> textfield_ = nullptr;
  raw_ptr<views::ScrollView> dropdown_scroll_view_ = nullptr;
  raw_ptr<views::View> dropdown_container_ = nullptr;

  std::vector<raw_ptr<DaoSuggestionItemView>> suggestion_views_;
  std::vector<AutocompleteMatch> visible_matches_;

  std::unique_ptr<AutocompleteController> autocomplete_controller_;
  std::unique_ptr<ChromeAutocompleteSchemeClassifier> scheme_classifier_;
  bool autocomplete_controller_uses_enhanced_suggestions_ = false;

  int selected_index_ = -1;
  // Tracks whether the highlighted row was reached through explicit keyboard
  // or pointer interaction. Automatic highlighting leaves this false so it
  // cannot write a preview into the textfield.
  bool selection_explicitly_changed_ = false;
  std::u16string user_input_text_;
  std::u16string selection_preview_text_;
  // The last explicitly previewed value rejected with Backspace. Keep it
  // across subsequent edits so a late result cannot restore that exact value.
  std::u16string rejected_selection_preview_text_;
  bool selection_preview_active_ = false;
  std::u16string inline_autocompletion_;
  bool updating_textfield_ = false;
  int visible_suggestion_count_ = 0;
  int autocomplete_start_count_for_testing_ = 0;
  std::optional<AutocompleteController::UpdateType>
      autocomplete_update_type_for_testing_;

  // True only for the unchanged query after deleting or rejecting suggested
  // text. It scopes Chromium's prevent-inline-autocomplete input flag to the
  // deletion request; a later insertion clears it so provider ranking remains
  // unchanged for the new query.
  bool suppress_ghost_for_current_query_ = false;

  // Length of |user_input_text_| at the previous ContentsChanged tick. Used
  // to distinguish growth (typing) from shrinkage (deletion) without having
  // to inspect the textfield state directly.
  size_t last_text_length_ = 0;

  // Index inside suggestion_views_ of the synthetic "Ask AI" row, or -1
  // when the current input does not qualify (empty / looks like a URL). The
  // row is inserted after the top autocomplete match; Enter / click on it
  // routes to SubmitAskAi instead of NavigateToMatch.
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
