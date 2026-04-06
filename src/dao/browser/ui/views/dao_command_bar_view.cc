// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_command_bar_view.h"

#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/favicon/core/favicon_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "content/public/browser/web_contents.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/dao_suggestion_item_view.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/background.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

// Custom Textfield that prevents FocusManager from intercepting Tab for focus
// traversal.  Without this, Tab events never reach HandleKeyEvent() because
// FocusManager consumes them in its pre-target handler for AdvanceFocus().
class CommandBarTextfield : public views::Textfield {
  METADATA_HEADER(CommandBarTextfield, views::Textfield)

 public:
  CommandBarTextfield() = default;

  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& event) override {
    if (event.key_code() == ui::VKEY_TAB) {
      return true;
    }
    return Textfield::SkipDefaultKeyEventProcessing(event);
  }
};

BEGIN_METADATA(CommandBarTextfield)
END_METADATA

BEGIN_METADATA(DaoCommandBarView)
END_METADATA

DaoCommandBarView::DaoCommandBarView(Browser* browser) : browser_(browser) {
  SetVisible(false);
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);

  // Card container: centered panel with rounded corners
  card_container_ = AddChildView(std::make_unique<views::View>());
  card_container_->SetPaintToLayer();
  card_container_->layer()->SetFillsBoundsOpaquely(false);
  card_container_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(16));
  card_container_->layer()->SetIsFastRoundedCorner(true);
  card_container_->SetBackground(
      views::CreateSolidBackground(kCommandBarBackground));

  auto* card_layout =
      card_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(8, 12),
          10));

  // Intent icon inside the card (before textfield): shows search/URL/favicon
  auto favicon_view = std::make_unique<views::ImageView>();
  favicon_view->SetImageSize(gfx::Size(18, 18));
  favicon_view->SetPreferredSize(gfx::Size(24, 24));
  favicon_icon_ = card_container_->AddChildView(std::move(favicon_view));

  // Textfield inside the card
  auto textfield = std::make_unique<CommandBarTextfield>();
  textfield->SetPlaceholderText(u"Type a URL or search...");
  textfield->set_controller(this);
  textfield->SetBorder(nullptr);
  textfield->SetBackgroundColor(SK_ColorTRANSPARENT);
  textfield->SetTextColor(SkColorSetRGB(30, 20, 40));
  textfield->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 16,
                                        gfx::Font::Weight::NORMAL));
  textfield_ = card_container_->AddChildView(std::move(textfield));

  // Make the textfield fill the card width
  card_layout->SetFlexForView(textfield_, 1);

  // Ghost text label: overlaid on the textfield area, shows inline
  // autocompletion in a muted color.  Added as a direct child of |this|
  // (not card_container_) so it is not managed by the card's BoxLayout.
  auto ghost_label = std::make_unique<views::Label>();
  ghost_label->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 16,
                                          gfx::Font::Weight::NORMAL));
  ghost_label->SetEnabledColor(kGhostTextColor);
  ghost_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  ghost_label->SetSubpixelRenderingEnabled(false);
  ghost_label->SetVisible(false);
  ghost_label->SetCanProcessEventsWithinSubtree(false);
  ghost_label->SetPaintToLayer();
  ghost_label->layer()->SetFillsBoundsOpaquely(false);
  ghost_text_label_ = AddChildView(std::move(ghost_label));

  // Dropdown container: below the card, same kCommandBarBackground
  dropdown_container_ = AddChildView(std::make_unique<views::View>());
  dropdown_container_->SetPaintToLayer();
  dropdown_container_->layer()->SetFillsBoundsOpaquely(false);
  dropdown_container_->layer()->SetRoundedCornerRadius(
      gfx::RoundedCornersF(0, 0, 16, 16));
  dropdown_container_->layer()->SetIsFastRoundedCorner(true);
  dropdown_container_->SetBackground(
      views::CreateSolidBackground(kCommandBarBackground));
  dropdown_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0)));
  dropdown_container_->SetVisible(false);

  // Pre-create suggestion items
  for (int i = 0; i < kMaxSuggestions; ++i) {
    auto* item = dropdown_container_->AddChildView(
        std::make_unique<DaoSuggestionItemView>(
            i, base::BindRepeating(&DaoCommandBarView::OnSuggestionClicked,
                                   base::Unretained(this)),
            browser_->profile()));
    item->SetVisible(false);
    suggestion_views_.push_back(item);
  }
}

DaoCommandBarView::~DaoCommandBarView() {
  if (autocomplete_controller_) {
    autocomplete_controller_->RemoveObserver(this);
  }
}

void DaoCommandBarView::InitAutocompleteController() {
  if (autocomplete_controller_) {
    return;
  }

  Profile* profile = browser_->profile();
  scheme_classifier_ =
      std::make_unique<ChromeAutocompleteSchemeClassifier>(profile);

  int provider_types = AutocompleteProvider::TYPE_HISTORY_QUICK |
                       AutocompleteProvider::TYPE_HISTORY_URL |
                       AutocompleteProvider::TYPE_BOOKMARK |
                       AutocompleteProvider::TYPE_SEARCH |
                       AutocompleteProvider::TYPE_OPEN_TAB;

  autocomplete_controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile),
      provider_types);
  autocomplete_controller_->AddObserver(this);
}

void DaoCommandBarView::Show() {
  if (GetVisible()) {
    return;
  }

  is_new_tab_mode_ = false;
  selected_index_ = -1;
  inline_autocompletion_.clear();
  ghost_text_label_->SetVisible(false);

  InitAutocompleteController();

  // Ensure command bar is on top of all sibling views (both view order and
  // layer order).  Also stack all ancestor layers above their siblings so
  // compositor hit-testing routes events here instead of to
  // contents_container_ or the address bar.
  if (parent()) {
    parent()->ReorderChildView(this, parent()->children().size());
  }

  SetVisible(true);

  for (ui::Layer* l = layer(); l && l->parent(); l = l->parent()) {
    l->parent()->StackAtTop(l);
  }

  // Prevent web content's native view from stealing events
  SetWebContentEventProcessing(false);

  // Pre-fill with current tab URL and set favicon
  auto* contents = browser_->tab_strip_model()->GetActiveWebContents();
  if (contents) {
    GURL url = contents->GetVisibleURL();
    if (url.is_valid() && !url.IsAboutBlank()) {
      std::u16string url_text = base::UTF8ToUTF16(url.spec());
      updating_textfield_ = true;
      textfield_->SetText(url_text);
      textfield_->SelectAll(false);
      updating_textfield_ = false;
      user_input_text_ = url_text;
      StartAutocomplete(url_text);

      // Show favicon for the current page
      gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
      if (!favicon.IsEmpty()) {
        favicon_icon_->SetImage(
            gfx::ImageSkiaOperations::CreateResizedImage(
                *favicon.ToImageSkia(),
                skia::ImageOperations::RESIZE_BEST,
                gfx::Size(18, 18)));
        favicon_icon_->SetVisible(true);
      } else {
        // No favicon — show URL intent icon
        UpdateInputIcon();
      }
    } else {
      textfield_->SetText(u"");
      user_input_text_.clear();
      UpdateInputIcon();
    }
  } else {
    textfield_->SetText(u"");
    user_input_text_.clear();
    UpdateInputIcon();
  }

  // Defer focus request to avoid being overridden by Chromium's focus
  // management during new-tab creation flow.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DaoCommandBarView::DeferredRequestFocus,
                                weak_factory_.GetWeakPtr()));
}

void DaoCommandBarView::ShowForNewTab() {
  if (GetVisible()) {
    return;
  }

  is_new_tab_mode_ = true;
  selected_index_ = -1;
  inline_autocompletion_.clear();
  ghost_text_label_->SetVisible(false);
  user_input_text_.clear();

  InitAutocompleteController();

  // Highlight the new-tab button in the sidebar
  SetNewTabButtonHighlight(true);

  // Ensure command bar is on top of all sibling views.
  if (parent()) {
    parent()->ReorderChildView(this, parent()->children().size());
  }

  SetVisible(true);

  for (ui::Layer* l = layer(); l && l->parent(); l = l->parent()) {
    l->parent()->StackAtTop(l);
  }

  // Prevent web content's native view from stealing events
  SetWebContentEventProcessing(false);

  // New tab mode: empty textfield for fresh input
  updating_textfield_ = true;
  textfield_->SetText(u"");
  updating_textfield_ = false;

  // Show search icon for new tab mode (empty input)
  UpdateInputIcon();

  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&DaoCommandBarView::DeferredRequestFocus,
                                weak_factory_.GetWeakPtr()));
}

void DaoCommandBarView::DeferredRequestFocus() {
  if (GetVisible() && textfield_) {
    textfield_->RequestFocus();
  }
}

void DaoCommandBarView::Hide() {
  if (!GetVisible()) {
    return;
  }

  StopAutocomplete();
  dropdown_container_->SetVisible(false);
  visible_suggestion_count_ = 0;

  SetVisible(false);

  // Re-enable web content event processing
  SetWebContentEventProcessing(true);

  // Clear highlight
  if (is_new_tab_mode_) {
    SetNewTabButtonHighlight(false);
  }
  is_new_tab_mode_ = false;

  // Return focus to web contents
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->GetContentsWebView()) {
    browser_view->GetContentsWebView()->RequestFocus();
  }
}

void DaoCommandBarView::Layout(PassKey) {
  // Position card centered horizontally, ~38% from top
  const int kCardWidth = 600;
  const int kCardHeight = 52;

  int card_width = std::min(kCardWidth, width() - 40);
  int card_x = (width() - card_width) / 2;
  int card_y = static_cast<int>(height() * 0.38) - kCardHeight / 2;
  card_y = std::max(20, card_y);

  if (card_container_) {
    card_container_->SetBounds(card_x, card_y, card_width, kCardHeight);
  }

  // Position dropdown directly below the card
  if (dropdown_container_ && dropdown_container_->GetVisible()) {
    int dropdown_height = visible_suggestion_count_ * 40 + 8;
    dropdown_container_->SetBounds(card_x, card_y + kCardHeight, card_width,
                                   dropdown_height);

    // When dropdown is visible, make card have no bottom rounding
    card_container_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(16, 16, 0, 0));
  } else {
    // Restore full rounding when dropdown is hidden
    card_container_->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(16));
  }

  PositionGhostText();
}

void DaoCommandBarView::OnPaint(gfx::Canvas* canvas) {
  // Draw a real blurred shadow behind the card (and dropdown if visible)
  // using Chromium's DrawLooper-based shadow infrastructure.
  if (!card_container_) {
    return;
  }

  gfx::Rect card_rect = card_container_->bounds();
  if (dropdown_container_ && dropdown_container_->GetVisible()) {
    card_rect.Union(dropdown_container_->bounds());
  }

  std::vector<gfx::ShadowValue> shadows;
  shadows.emplace_back(gfx::Vector2d(0, 20), 160,
                       SkColorSetARGB(130, 0, 0, 0));
  shadows.emplace_back(gfx::Vector2d(0, 6), 40,
                       SkColorSetARGB(80, 0, 0, 0));

  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kFill_Style);
  flags.setColor(SK_ColorTRANSPARENT);
  flags.setLooper(gfx::CreateShadowDrawLooper(shadows));

  gfx::RectF card_f(card_rect);
  canvas->DrawRoundRect(card_f, 16.0f, flags);
}

bool DaoCommandBarView::OnMousePressed(const ui::MouseEvent& event) {
  gfx::Point loc = event.location();

  // Click inside card = normal (textfield interaction)
  if (card_container_ && card_container_->bounds().Contains(loc)) {
    return false;
  }

  // Click inside dropdown = handled by suggestion item views
  if (dropdown_container_ && dropdown_container_->GetVisible() &&
      dropdown_container_->bounds().Contains(loc)) {
    return false;
  }

  // Click outside = dismiss
  if (is_new_tab_mode_) {
    CancelNewTab();
  } else {
    Hide();
  }
  return true;
}

void DaoCommandBarView::AddedToWidget() {
  if (auto* fm = GetFocusManager()) {
    fm->AddFocusChangeListener(this);
  }
}

void DaoCommandBarView::RemovedFromWidget() {
  if (auto* fm = GetFocusManager()) {
    fm->RemoveFocusChangeListener(this);
  }
}

void DaoCommandBarView::OnWillChangeFocus(views::View* focused_before,
                                           views::View* focused_now) {}

void DaoCommandBarView::OnDidChangeFocus(views::View* focused_before,
                                          views::View* focused_now) {
  if (!GetVisible()) {
    return;
  }
  // If focus moved to a view outside the command bar, auto-dismiss.
  if (focused_now && !Contains(focused_now)) {
    // Post a task to avoid re-entrant state during focus change.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&DaoCommandBarView::Dismiss,
                                  weak_factory_.GetWeakPtr()));
  }
}

void DaoCommandBarView::Dismiss() {
  if (!GetVisible()) {
    return;
  }
  if (is_new_tab_mode_) {
    CancelNewTab();
  } else {
    Hide();
  }
}

void DaoCommandBarView::ContentsChanged(views::Textfield* sender,
                                        const std::u16string& new_contents) {
  if (updating_textfield_) {
    return;
  }

  user_input_text_ = new_contents;
  inline_autocompletion_.clear();
  ghost_text_label_->SetVisible(false);
  selected_index_ = -1;

  // Update the intent icon based on input
  UpdateInputIcon();

  if (new_contents.empty()) {
    StopAutocomplete();
    dropdown_container_->SetVisible(false);
    visible_suggestion_count_ = 0;
    InvalidateLayout();
    return;
  }

  StartAutocomplete(new_contents);
}

bool DaoCommandBarView::HandleKeyEvent(views::Textfield* sender,
                                       const ui::KeyEvent& key_event) {
  if (key_event.type() != ui::EventType::kKeyPressed) {
    return false;
  }

  if (key_event.key_code() == ui::VKEY_RETURN) {
    ApplySelectedSuggestion();
    return true;
  }

  if (key_event.key_code() == ui::VKEY_ESCAPE) {
    if (is_new_tab_mode_) {
      CancelNewTab();
    } else {
      Hide();
    }
    return true;
  }

  if (key_event.key_code() == ui::VKEY_DOWN) {
    if (visible_suggestion_count_ > 0) {
      int next = selected_index_ + 1;
      if (next >= visible_suggestion_count_) {
        next = 0;
      }
      SetSelectedIndex(next);
    }
    return true;
  }

  if (key_event.key_code() == ui::VKEY_UP) {
    if (visible_suggestion_count_ > 0) {
      int prev = selected_index_ - 1;
      if (prev < 0) {
        prev = visible_suggestion_count_ - 1;
      }
      SetSelectedIndex(prev);
    }
    return true;
  }

  // Right arrow: accept ghost text when cursor is at end of user input
  if (key_event.key_code() == ui::VKEY_RIGHT) {
    if (!inline_autocompletion_.empty()) {
      size_t cursor_pos = sender->GetCursorPosition();
      if (cursor_pos == user_input_text_.length()) {
        // Accept the inline autocompletion into the textfield
        user_input_text_ = user_input_text_ + inline_autocompletion_;
        inline_autocompletion_.clear();
        ghost_text_label_->SetVisible(false);
        updating_textfield_ = true;
        sender->SetText(user_input_text_);
        sender->SetSelectedRange(gfx::Range(user_input_text_.length()));
        updating_textfield_ = false;
        StartAutocomplete(user_input_text_);
        return true;
      }
    }
  }

  if (key_event.key_code() == ui::VKEY_TAB) {
    return true;
  }

  return false;
}

void DaoCommandBarView::OnResultChanged(AutocompleteController* controller,
                                        bool default_match_changed) {
  UpdateSuggestions();
  UpdateGhostText();
  UpdateInputIcon();
}

void DaoCommandBarView::StartAutocomplete(const std::u16string& text) {
  if (!autocomplete_controller_ || !scheme_classifier_) {
    return;
  }

  AutocompleteInput input(
      text,
      metrics::OmniboxEventProto::INSTANT_NTP_WITH_OMNIBOX_AS_STARTING_FOCUS,
      *scheme_classifier_);
  autocomplete_controller_->Start(input);
}

void DaoCommandBarView::StopAutocomplete() {
  if (autocomplete_controller_) {
    autocomplete_controller_->Stop(true);
  }
  inline_autocompletion_.clear();
  ghost_text_label_->SetVisible(false);
  selected_index_ = -1;
}

void DaoCommandBarView::UpdateSuggestions() {
  if (!autocomplete_controller_) {
    return;
  }

  const AutocompleteResult& result = autocomplete_controller_->result();
  int count = std::min(static_cast<int>(result.size()), kMaxSuggestions);

  // Check if we have a bookmark model for icon determination
  bookmarks::BookmarkModel* bookmark_model =
      BookmarkModelFactory::GetForBrowserContext(browser_->profile());

  for (int i = 0; i < kMaxSuggestions; ++i) {
    if (i < count) {
      const AutocompleteMatch& match = result.match_at(i);
      bool is_bookmark =
          bookmark_model &&
          bookmark_model->IsBookmarked(match.destination_url);
      suggestion_views_[i]->SetMatch(match, is_bookmark);
      suggestion_views_[i]->SetVisible(true);
      suggestion_views_[i]->SetSelected(i == selected_index_);
    } else {
      suggestion_views_[i]->SetVisible(false);
      suggestion_views_[i]->SetSelected(false);
    }
  }

  visible_suggestion_count_ = count;

  if (count > 0) {
    dropdown_container_->SetVisible(true);
    // Auto-select first item if nothing is selected
    if (selected_index_ < 0) {
      SetSelectedIndex(0);
    }
  } else {
    dropdown_container_->SetVisible(false);
  }

  InvalidateLayout();
}

void DaoCommandBarView::UpdateGhostText() {
  if (!autocomplete_controller_) {
    ghost_text_label_->SetVisible(false);
    inline_autocompletion_.clear();
    return;
  }

  const AutocompleteResult& result = autocomplete_controller_->result();
  const AutocompleteMatch* default_match = result.default_match();

  if (default_match && !default_match->inline_autocompletion.empty()) {
    inline_autocompletion_ = default_match->inline_autocompletion;
    ghost_text_label_->SetText(inline_autocompletion_);
    ghost_text_label_->SetVisible(true);
    PositionGhostText();
  } else {
    inline_autocompletion_.clear();
    ghost_text_label_->SetVisible(false);
  }
}

void DaoCommandBarView::PositionGhostText() {
  if (!ghost_text_label_ || !ghost_text_label_->GetVisible() || !textfield_) {
    return;
  }

  // Measure the width of user's input text using the textfield's font
  int text_width =
      gfx::GetStringWidth(user_input_text_, textfield_->GetFontList());

  // Convert textfield origin to DaoCommandBarView coordinate space
  gfx::Point tf_origin;
  views::View::ConvertPointToTarget(textfield_, this, &tf_origin);

  int ghost_x = tf_origin.x() + text_width;
  int ghost_y = tf_origin.y();
  int max_width = card_container_->bounds().right() - ghost_x - 16;

  if (max_width > 0) {
    ghost_text_label_->SetBounds(ghost_x, ghost_y, max_width,
                                  textfield_->height());
  } else {
    ghost_text_label_->SetVisible(false);
  }
}

void DaoCommandBarView::UpdateInputIcon() {
  if (!favicon_icon_) {
    return;
  }

  // Cancel any pending favicon request for the input icon
  icon_favicon_tracker_.TryCancelAll();
  pending_icon_favicon_url_ = GURL();

  // If there's a selected autocomplete match, use its type
  if (autocomplete_controller_ && selected_index_ >= 0) {
    const AutocompleteResult& result = autocomplete_controller_->result();
    if (selected_index_ < static_cast<int>(result.size())) {
      const AutocompleteMatch& match = result.match_at(selected_index_);
      bool is_search = AutocompleteMatch::IsSearchType(match.type);
      if (is_search) {
        favicon_icon_->SetImage(gfx::CreateVectorIcon(
            vector_icons::kSearchChromeRefreshIcon, 18, kSuggestionIconColor));
      } else {
        // Set page icon as immediate fallback, then try loading favicon
        favicon_icon_->SetImage(gfx::CreateVectorIcon(
            omnibox::kPageChromeRefreshIcon, 18, kSuggestionIconColor));

        if (match.destination_url.is_valid() &&
            match.destination_url.SchemeIsHTTPOrHTTPS()) {
          favicon::FaviconService* favicon_service =
              FaviconServiceFactory::GetForProfile(
                  browser_->profile(), ServiceAccessType::EXPLICIT_ACCESS);
          if (favicon_service) {
            pending_icon_favicon_url_ = match.destination_url;
            favicon_service->GetFaviconImageForPageURL(
                match.destination_url,
                base::BindOnce(&DaoCommandBarView::OnInputFaviconFetched,
                               base::Unretained(this),
                               match.destination_url),
                &icon_favicon_tracker_);
          }
        }
      }
      favicon_icon_->SetVisible(true);
      return;
    }
  }

  // Fallback: determine icon from input text
  if (user_input_text_.empty()) {
    favicon_icon_->SetImage(gfx::CreateVectorIcon(
        vector_icons::kSearchChromeRefreshIcon, 18, kSuggestionIconColor));
  } else if (LooksLikeURL(user_input_text_)) {
    favicon_icon_->SetImage(gfx::CreateVectorIcon(
        omnibox::kPageChromeRefreshIcon, 18, kSuggestionIconColor));
  } else {
    favicon_icon_->SetImage(gfx::CreateVectorIcon(
        vector_icons::kSearchChromeRefreshIcon, 18, kSuggestionIconColor));
  }
  favicon_icon_->SetVisible(true);
}

void DaoCommandBarView::OnInputFaviconFetched(
    const GURL& page_url,
    const favicon_base::FaviconImageResult& result) {
  // Ignore stale callbacks
  if (page_url != pending_icon_favicon_url_) {
    return;
  }
  pending_icon_favicon_url_ = GURL();

  if (result.image.IsEmpty() || !favicon_icon_) {
    return;  // Keep the vector icon fallback
  }

  gfx::ImageSkia favicon = result.image.AsImageSkia();
  favicon_icon_->SetImage(gfx::ImageSkiaOperations::CreateResizedImage(
      favicon, skia::ImageOperations::RESIZE_BEST, gfx::Size(18, 18)));
}

void DaoCommandBarView::SetSelectedIndex(int index) {
  if (index == selected_index_) {
    return;
  }

  // Deselect old
  if (selected_index_ >= 0 && selected_index_ < kMaxSuggestions) {
    suggestion_views_[selected_index_]->SetSelected(false);
  }

  selected_index_ = index;

  // Select new
  if (selected_index_ >= 0 && selected_index_ < kMaxSuggestions) {
    suggestion_views_[selected_index_]->SetSelected(true);
  }

  // Update the input icon to reflect the selected match type
  UpdateInputIcon();
}

void DaoCommandBarView::ApplySelectedSuggestion() {
  if (!autocomplete_controller_) {
    Navigate(textfield_->GetText());
    return;
  }

  const AutocompleteResult& result = autocomplete_controller_->result();

  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(result.size())) {
    NavigateToMatch(result.match_at(selected_index_));
  } else {
    // No selected match — use plain text navigation
    Navigate(textfield_->GetText());
  }
}

void DaoCommandBarView::NavigateToMatch(const AutocompleteMatch& match) {
  // Check if this is a tab switch match
  if (match.has_tab_match.value_or(false)) {
    NavigateParams params(browser_, match.destination_url,
                          ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::SWITCH_TO_TAB;
    params.path_behavior = NavigateParams::RESPECT;

    if (is_new_tab_mode_) {
      SetNewTabButtonHighlight(false);
      is_new_tab_mode_ = false;
    }

    StopAutocomplete();
    dropdown_container_->SetVisible(false);
    visible_suggestion_count_ = 0;
    SetVisible(false);
    SetWebContentEventProcessing(true);

    ::Navigate(&params);
    return;
  }

  GURL url = match.destination_url;
  if (!url.is_valid()) {
    Navigate(textfield_->GetText());
    return;
  }

  StopAutocomplete();
  dropdown_container_->SetVisible(false);
  visible_suggestion_count_ = 0;

  if (is_new_tab_mode_) {
    SetNewTabButtonHighlight(false);
    is_new_tab_mode_ = false;
    SetVisible(false);
    SetWebContentEventProcessing(true);

    chrome::AddTabAt(browser_, url, -1, true);

    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->GetContentsWebView()) {
      browser_view->GetContentsWebView()->RequestFocus();
    }
  } else {
    NavigateParams params(browser_, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::CURRENT_TAB;
    SetVisible(false);
    SetWebContentEventProcessing(true);
    ::Navigate(&params);

    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->GetContentsWebView()) {
      browser_view->GetContentsWebView()->RequestFocus();
    }
  }
}

void DaoCommandBarView::OnSuggestionClicked(int index) {
  SetSelectedIndex(index);
  ApplySelectedSuggestion();
}

void DaoCommandBarView::Navigate(const std::u16string& text) {
  std::string input = base::UTF16ToUTF8(text);
  if (input.empty()) {
    if (is_new_tab_mode_) {
      CancelNewTab();
    } else {
      Hide();
    }
    return;
  }

  GURL url;
  if (LooksLikeURL(text)) {
    // Prepend https:// if no scheme
    if (input.find("://") == std::string::npos) {
      input = "https://" + input;
    }
    url = GURL(input);
  } else {
    // Build Google search URL
    std::string query;
    for (char c : input) {
      if (c == ' ') {
        query += '+';
      } else if (c == '+' || c == '&' || c == '=' || c == '%' || c == '#') {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
        query += buf;
      } else {
        query += c;
      }
    }
    url = GURL("https://www.google.com/search?q=" + query);
  }

  if (!url.is_valid()) {
    if (is_new_tab_mode_) {
      CancelNewTab();
    } else {
      Hide();
    }
    return;
  }

  StopAutocomplete();
  dropdown_container_->SetVisible(false);
  visible_suggestion_count_ = 0;

  if (is_new_tab_mode_) {
    // Create a new tab and navigate to the URL
    SetNewTabButtonHighlight(false);
    is_new_tab_mode_ = false;
    SetVisible(false);

    // Re-enable web content event processing
    SetWebContentEventProcessing(true);

    chrome::AddTabAt(browser_, url, -1, true);

    // Return focus to web contents
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->GetContentsWebView()) {
      browser_view->GetContentsWebView()->RequestFocus();
    }
  } else {
    // Navigate in current tab
    NavigateParams params(browser_, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::CURRENT_TAB;
    ::Navigate(&params);
    Hide();
  }
}

void DaoCommandBarView::CancelNewTab() {
  // No tab was created, just hide and clear highlight
  SetNewTabButtonHighlight(false);
  is_new_tab_mode_ = false;

  StopAutocomplete();
  dropdown_container_->SetVisible(false);
  visible_suggestion_count_ = 0;

  SetVisible(false);

  // Re-enable web content event processing
  SetWebContentEventProcessing(true);

  // Return focus to web contents
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->GetContentsWebView()) {
    browser_view->GetContentsWebView()->RequestFocus();
  }
}

void DaoCommandBarView::SetNewTabButtonHighlight(bool highlighted) {
  // No-op: new tab button highlight is handled by WebUI sidebar.
}

void DaoCommandBarView::SetWebContentEventProcessing(bool enabled) {
  auto* web_contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return;
  }
  if (enabled) {
    UnblockWebContentNativeEvents(web_contents);
  } else {
    BlockWebContentNativeEvents(web_contents);
  }
}

// static
bool DaoCommandBarView::LooksLikeURL(const std::u16string& text) {
  std::string s = base::UTF16ToUTF8(text);

  // Starts with http:// or https://
  if (s.find("http://") == 0 || s.find("https://") == 0) {
    return true;
  }

  // Is localhost (with optional port)
  if (s.find("localhost") == 0) {
    return true;
  }

  // Contains a dot but no spaces (e.g. "github.com")
  if (s.find('.') != std::string::npos && s.find(' ') == std::string::npos) {
    return true;
  }

  return false;
}

}  // namespace dao
