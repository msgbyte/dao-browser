// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_command_bar_view.h"

#include <algorithm>
#include <array>
#include <optional>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/memory/scoped_refptr.h"
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_provider_client.h"
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"
#include "chrome/browser/autocomplete/shortcuts_backend_factory.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/favicon/favicon_utils.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/sessions/tab_restore_service_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/chrome_pages.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/grit/generated_resources.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/favicon/core/favicon_service.h"
#include "components/keyed_service/core/service_access_type.h"
#include "components/omnibox/browser/autocomplete_classifier.h"
#include "components/omnibox/browser/autocomplete_controller_config.h"
#include "components/omnibox/browser/autocomplete_enums.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/omnibox/browser/autocomplete_provider.h"
#include "components/omnibox/browser/autocomplete_result.h"
#include "components/omnibox/browser/shortcuts_backend.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/prefs/pref_service.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_service.h"
#include "components/sessions/core/tab_restore_service.h"
#include "components/url_formatter/url_fixer.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_address_bar_view.h"
#include "dao/browser/ui/views/dao_agent_sidebar_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_native_util_mac.h"
#include "dao/browser/ui/views/dao_suggestion_item_view.h"
#include "dao/browser/ui/views/dao_tab_commands.h"
#include "dao/browser/ui/views/sidebar/dao_sidebar_view.h"
#include "third_party/metrics_proto/omnibox_event.pb.h"
#include "third_party/metrics_proto/omnibox_focus_type.pb.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/compositor/layer.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/range/range.h"
#include "ui/gfx/shadow_value.h"
#include "ui/gfx/skia_paint_util.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {

constexpr int kCommandBarTextFontSize = 17;

enum class CommandAction {
  kBrowserCommand,
  kCopyCurrentLink,
  kAgentSettings,
};

struct CommandDefinition {
  int title_id;
  int aliases_id;
  const char16_t* english_title;
  const char16_t* english_aliases;
  LucideIcon icon;
  CommandAction action;
  int command_id;
  bool safe_default;
};

constexpr std::array<CommandDefinition, 8> kCommands = {{
    {IDS_DAO_COMMAND_OPEN_SETTINGS, IDS_DAO_COMMAND_OPEN_SETTINGS_ALIASES,
     u"Open Settings", u"settings|preferences", LucideIcon::kSettings,
     CommandAction::kBrowserCommand, IDC_OPTIONS, true},
    {IDS_DAO_COMMAND_REOPEN_CLOSED_TAB,
     IDS_DAO_COMMAND_REOPEN_CLOSED_TAB_ALIASES, u"Reopen Closed Tab",
     u"reopen closed tab|reopen tab|restore tab|undo close tab",
     LucideIcon::kRotateCw, CommandAction::kBrowserCommand, IDC_RESTORE_TAB,
     false},
    {IDS_DAO_COMMAND_COPY_CURRENT_LINK,
     IDS_DAO_COMMAND_COPY_CURRENT_LINK_ALIASES, u"Copy Current Link",
     u"copy link|copy url|copy current link", LucideIcon::kShare,
     CommandAction::kCopyCurrentLink, 0, false},
    {IDS_DAO_COMMAND_OPEN_DOWNLOADS, IDS_DAO_COMMAND_OPEN_DOWNLOADS_ALIASES,
     u"Open Downloads", u"downloads|download history",
     LucideIcon::kSquareArrowDownLeft, CommandAction::kBrowserCommand,
     IDC_SHOW_DOWNLOADS, true},
    {IDS_DAO_COMMAND_OPEN_HISTORY, IDS_DAO_COMMAND_OPEN_HISTORY_ALIASES,
     u"Open History", u"history|browsing history", LucideIcon::kRotateCw,
     CommandAction::kBrowserCommand, IDC_SHOW_HISTORY, true},
    {IDS_DAO_COMMAND_MANAGE_EXTENSIONS,
     IDS_DAO_COMMAND_MANAGE_EXTENSIONS_ALIASES, u"Manage Extensions",
     u"extensions|manage extensions|addons", LucideIcon::kSlidersHorizontal,
     CommandAction::kBrowserCommand, IDC_MANAGE_EXTENSIONS, true},
    {IDS_DAO_COMMAND_OPEN_AGENT_SETTINGS,
     IDS_DAO_COMMAND_OPEN_AGENT_SETTINGS_ALIASES, u"Open Agent Settings",
     u"agent settings|ai settings|model settings", LucideIcon::kBot,
     CommandAction::kAgentSettings, 0, true},
    {IDS_DAO_COMMAND_OPEN_TASK_MANAGER,
     IDS_DAO_COMMAND_OPEN_TASK_MANAGER_ALIASES, u"Open Task Manager",
     u"task manager|taskmanager|process manager",
     LucideIcon::kSlidersHorizontal, CommandAction::kBrowserCommand,
     IDC_TASK_MANAGER, true},
}};

void SetTextAndSelectedRangeKeepingCaretVisible(
    views::Textfield* textfield,
    const std::u16string& text,
    const gfx::Range& selection) {
  // Keep useful context on both sides of the caret when a completion is wider
  // than the textfield. The positions are applied in priority order, matching
  // Chromium's Omnibox behavior: leading context wins, then trailing context.
  static constexpr size_t kTrailingContextLength = 30;
  static constexpr size_t kLeadingContextLength = 10;

  textfield->SetTextWithoutCaretBoundsChangeNotification(text,
                                                          selection.end());
  textfield->Scroll(
      {0, std::min(selection.end() + kTrailingContextLength, text.size()),
       selection.end() -
           std::min(kLeadingContextLength, selection.end())});
  textfield->SetSelectedRange(selection);
}

bool LooksLikeLocalFilePath(const std::string& text) {
  return !text.empty() && (text[0] == '/' || text[0] == '~');
}

std::u16string NormalizeSearchTerms(const std::u16string& text) {
  return std::u16string(base::TrimWhitespace(text, base::TRIM_ALL));
}

std::u16string NormalizeCommandText(const std::u16string& text) {
  return base::i18n::ToLower(
      base::CollapseWhitespace(text, /*trim_sequences_with_line_breaks=*/true));
}

std::vector<std::u16string> CommandTerms(const CommandDefinition& command) {
  std::vector<std::u16string> terms = {
      NormalizeCommandText(l10n_util::GetStringUTF16(command.title_id)),
      NormalizeCommandText(command.english_title)};
  for (const std::u16string& aliases :
       {l10n_util::GetStringUTF16(command.aliases_id),
        std::u16string(command.english_aliases)}) {
    for (const auto& alias : base::SplitString(
             aliases, u"|", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
      terms.push_back(NormalizeCommandText(alias));
    }
  }
  return terms;
}

}  // namespace

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

// A Background that paints a translucent color but reports an opaque color
// from get_color().  This satisfies Chromium's Label subpixel-rendering
// DCHECK (which walks ancestors looking for an opaque background) while
// still allowing SetBackgroundBlur to show through.
class FrostedGlassBackground : public views::Background {
 public:
  explicit FrostedGlassBackground(SkColor paint_color)
      : paint_color_(paint_color) {
    // Report opaque white so the DCHECK in Label::PaintText passes.
    SetColor(SK_ColorWHITE);
  }

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    canvas->DrawColor(paint_color_);
  }

 private:
  SkColor paint_color_;
};

// A view whose sole job is to paint a DrawLooper shadow.
// It has its own layer so it can render outside its clip without
// requiring the parent DaoCommandBarView to have a layer (which
// would block glass_container_'s backdrop blur).
// The view is sized larger than the glass container by kShadowPadding
// on each side so the blurred shadow is not clipped by the layer bounds.
class CommandBarShadowView : public views::View {
  METADATA_HEADER(CommandBarShadowView, views::View)

 public:
  static constexpr int kShadowPadding = 60;

  CommandBarShadowView() {
    SetPaintToLayer();
    layer()->SetFillsBoundsOpaquely(false);
    SetCanProcessEventsWithinSubtree(false);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    std::vector<gfx::ShadowValue> shadows;
    shadows.emplace_back(gfx::Vector2d(0, 8), 60,
                         SkColorSetARGB(50, 0, 0, 0));  // theme-independent
    shadows.emplace_back(gfx::Vector2d(0, 2), 16,
                         SkColorSetARGB(30, 0, 0, 0));  // theme-independent

    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kFill_Style);
    flags.setColor(SK_ColorTRANSPARENT);
    flags.setLooper(gfx::CreateShadowDrawLooper(shadows));

    // Draw the card shape inset by the padding so the shadow extends
    // into the padding area without being clipped.
    gfx::RectF card_rect(kShadowPadding, kShadowPadding,
                         width() - 2 * kShadowPadding,
                         height() - 2 * kShadowPadding);
    canvas->DrawRoundRect(card_rect, 16.0f, flags);
  }
};

BEGIN_METADATA(CommandBarShadowView)
END_METADATA

BEGIN_METADATA(DaoCommandBarView)
END_METADATA

DaoCommandBarView::DaoCommandBarView(Browser* browser) : browser_(browser) {
  SetVisible(false);
  // NOTE: No SetPaintToLayer() here — an intermediate layer would block
  // glass_container_'s backdrop blur from reaching the webpage content.

  // Shadow view: rendered before (behind) the glass container so its
  // DrawLooper shadow appears underneath.
  shadow_view_ = AddChildView(std::make_unique<CommandBarShadowView>());

  // Unified frosted glass container: single layer with blur + background
  // that wraps both the input card and the dropdown suggestions.
  glass_container_ = AddChildView(std::make_unique<views::View>());
  glass_container_->SetPaintToLayer();
  glass_container_->layer()->SetFillsBoundsOpaquely(false);
  glass_container_->layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(16));
  glass_container_->layer()->SetIsFastRoundedCorner(true);
  glass_container_->layer()->SetBackgroundBlur(kCommandBarBlurSigma);

  // Card container: input area inside the glass container (no own layer)
  card_container_ = glass_container_->AddChildView(
      std::make_unique<views::View>());

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
  textfield->SetPlaceholderText(
      l10n_util::GetStringUTF16(IDS_DAO_COMMAND_BAR_PLACEHOLDER));
  textfield->set_controller(this);
  textfield->SetBorder(nullptr);
  textfield->SetBackgroundColor(SK_ColorTRANSPARENT);
  textfield->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL,
                                        kCommandBarTextFontSize,
                                        gfx::Font::Weight::SEMIBOLD));
  textfield_ = card_container_->AddChildView(std::move(textfield));

  // Make the textfield fill the card width
  card_layout->SetFlexForView(textfield_, 1);

  auto dropdown_scroll_view = std::make_unique<views::ScrollView>();
  dropdown_scroll_view->SetBackgroundColor(std::nullopt);
  dropdown_scroll_view->SetDrawOverflowIndicator(false);
  dropdown_scroll_view->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  dropdown_scroll_view->SetVerticalScrollBarMode(
      views::ScrollView::ScrollBarMode::kHiddenButEnabled);
  dropdown_scroll_view->ClipHeightTo(0, kVisibleSuggestionRows * 40 + 8);
  dropdown_scroll_view_ =
      glass_container_->AddChildView(std::move(dropdown_scroll_view));
  dropdown_container_ =
      dropdown_scroll_view_->SetContents(std::make_unique<views::View>());
  dropdown_container_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 0)));
  dropdown_scroll_view_->SetVisible(false);

  // Pre-create suggestion items
  for (int i = 0; i < kVisibleSuggestionRows; ++i) {
    auto* item = dropdown_container_->AddChildView(
        std::make_unique<DaoSuggestionItemView>(
            i, base::BindRepeating(&DaoCommandBarView::OnSuggestionClicked,
                                   base::Unretained(this)),
            browser_->profile()));
    item->SetVisible(false);
    suggestion_views_.push_back(item);
  }

  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();
}

void DaoCommandBarView::ApplyTheme() {
  if (glass_container_) {
    glass_container_->SetBackground(
        std::make_unique<FrostedGlassBackground>(CommandBarBackground()));
    glass_container_->SetBorder(
        views::CreateRoundedRectBorder(1, 16, CommandBarBorder()));
  }
  if (textfield_) {
    textfield_->SetTextColor(TextPrimary());
    // Placeholder sits at the lightest step of the text hierarchy (40%)
    // so it reads as a hint rather than content.
    textfield_->set_placeholder_text_color(TextMuted());
  }
  // Suggestion rows cache their text color in views::Label and rasterize
  // their vector icons at SetMatch time, so they need an explicit refresh
  // when the theme changes.  Favicon images are left untouched.
  for (auto& item : suggestion_views_) {
    if (item) {
      item->RefreshTheme();
    }
  }
  // Re-rasterize the input field's leading icon (favicon or sparkle) so it
  // matches the new theme.  UpdateInputIcon() picks the right glyph based
  // on the current selection / input.
  UpdateInputIcon();
}

void DaoCommandBarView::OnNativeThemeUpdated(ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaint();
}

DaoCommandBarView::~DaoCommandBarView() {
  if (autocomplete_controller_) {
    autocomplete_controller_->RemoveObserver(this);
  }
}

bool DaoCommandBarView::EnhancedSuggestionsEnabled() const {
  return browser_ && browser_->profile() &&
         browser_->profile()->GetPrefs()->GetBoolean(
             dao::prefs::kDaoEnhancedCommandBarSuggestionsEnabled);
}

bool DaoCommandBarView::AskAiEnabled() const {
  return browser_ && browser_->profile() &&
         browser_->profile()->GetPrefs()->GetBoolean(
             dao::prefs::kDaoAskAiEnabled);
}

bool DaoCommandBarView::ShouldShowAskAiSuggestion() const {
  return AskAiEnabled() && !user_input_text_.empty() &&
         !LooksLikeURL(user_input_text_);
}

int DaoCommandBarView::GetAutocompleteProviderTypesForCurrentMode() const {
  if (EnhancedSuggestionsEnabled()) {
    return AutocompleteClassifier::DefaultOmniboxProviders();
  }

  return AutocompleteProvider::TYPE_HISTORY_QUICK |
         AutocompleteProvider::TYPE_HISTORY_URL |
         AutocompleteProvider::TYPE_BOOKMARK |
         AutocompleteProvider::TYPE_SEARCH |
         AutocompleteProvider::TYPE_SHORTCUTS |
         AutocompleteProvider::TYPE_OPEN_TAB;
}

void DaoCommandBarView::InitAutocompleteController() {
  const bool enhanced_suggestions_enabled = EnhancedSuggestionsEnabled();
  if (autocomplete_controller_ &&
      autocomplete_controller_uses_enhanced_suggestions_ ==
          enhanced_suggestions_enabled) {
    return;
  }

  if (autocomplete_controller_) {
    autocomplete_controller_->RemoveObserver(this);
    autocomplete_controller_.reset();
    scheme_classifier_.reset();
  }

  Profile* profile = browser_->profile();
  scheme_classifier_ =
      std::make_unique<ChromeAutocompleteSchemeClassifier>(profile);

  AutocompleteControllerConfig config;
  config.provider_types = GetAutocompleteProviderTypesForCurrentMode();

  autocomplete_controller_ = std::make_unique<AutocompleteController>(
      std::make_unique<ChromeAutocompleteProviderClient>(profile), config);
  autocomplete_controller_->AddObserver(this);
  autocomplete_controller_uses_enhanced_suggestions_ =
      enhanced_suggestions_enabled;
}

void DaoCommandBarView::Show() {
  if (GetVisible()) {
    return;
  }

  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  command_target_ =
      contents ? contents->GetWeakPtr() : base::WeakPtr<content::WebContents>();

  ClearSelectionPreview(false);
  is_new_tab_mode_ = false;
  selected_index_ = -1;
  selected_command_id_ = -1;
  command_selection_invalidated_ = false;
  command_mode_ = false;
  selection_explicitly_changed_ = false;
  inline_autocompletion_.clear();
  rejected_selection_preview_text_.clear();
  suppress_ghost_for_current_query_ = false;
  last_text_length_ = 0;

  InitAutocompleteController();

  // Ensure command bar is on top of all sibling views (both view order and
  // layer order).  Also stack all ancestor layers above their siblings so
  // compositor hit-testing routes events here instead of to
  // contents_container_ or the address bar.
  if (parent()) {
    parent()->ReorderChildView(this, parent()->children().size());
  }

  SetVisible(true);

  // Stack child layers (shadow + glass) above siblings so compositor
  // hit-testing and painting order is correct.
  if (shadow_view_ && shadow_view_->layer()) {
    for (ui::Layer* l = shadow_view_->layer(); l && l->parent();
         l = l->parent()) {
      l->parent()->StackAtTop(l);
    }
  }
  if (glass_container_ && glass_container_->layer()) {
    for (ui::Layer* l = glass_container_->layer(); l && l->parent();
         l = l->parent()) {
      l->parent()->StackAtTop(l);
    }
  }
  // Prevent web content's native view from stealing events
  SetWebContentEventProcessing(false);

  // Pre-fill with current tab URL and set favicon
  if (contents) {
    GURL url = contents->GetVisibleURL();
    if (url.is_valid() && !url.IsAboutBlank()) {
      std::u16string url_text = base::UTF8ToUTF16(url.spec());
      updating_textfield_ = true;
      textfield_->SetText(url_text);
      textfield_->SelectAll(false);
      updating_textfield_ = false;
      user_input_text_ = url_text;
      last_text_length_ = url_text.length();
      StartAutocomplete(url_text);

      // Show favicon for the current page
      gfx::Image favicon = favicon::TabFaviconFromWebContents(contents);
      if (!favicon.IsEmpty()) {
        favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(
            gfx::ImageSkiaOperations::CreateResizedImage(
                *favicon.ToImageSkia(),
                skia::ImageOperations::RESIZE_BEST,
                gfx::Size(18, 18))));
        favicon_icon_->SetVisible(true);
      } else {
        // No favicon — show URL intent icon
        UpdateInputIcon();
      }
    } else {
      textfield_->SetText(u"");
      user_input_text_.clear();
      UpdateInputIcon();
      ClearSuggestions();
    }
  } else {
    textfield_->SetText(u"");
    user_input_text_.clear();
    UpdateInputIcon();
    ClearSuggestions();
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

  content::WebContents* contents =
      browser_->tab_strip_model()->GetActiveWebContents();
  command_target_ =
      contents ? contents->GetWeakPtr() : base::WeakPtr<content::WebContents>();

  ClearSelectionPreview(false);
  is_new_tab_mode_ = true;
  selected_index_ = -1;
  selected_command_id_ = -1;
  command_selection_invalidated_ = false;
  command_mode_ = false;
  selection_explicitly_changed_ = false;
  inline_autocompletion_.clear();
  user_input_text_.clear();
  rejected_selection_preview_text_.clear();
  suppress_ghost_for_current_query_ = false;
  last_text_length_ = 0;

  InitAutocompleteController();

  // Highlight the new-tab button in the sidebar
  SetNewTabButtonHighlight(true);

  // Ensure command bar is on top of all sibling views.
  if (parent()) {
    parent()->ReorderChildView(this, parent()->children().size());
  }

  SetVisible(true);

  // Stack child layers above siblings for correct painting/hit-testing.
  if (shadow_view_ && shadow_view_->layer()) {
    for (ui::Layer* l = shadow_view_->layer(); l && l->parent();
         l = l->parent()) {
      l->parent()->StackAtTop(l);
    }
  }
  if (glass_container_ && glass_container_->layer()) {
    for (ui::Layer* l = glass_container_->layer(); l && l->parent();
         l = l->parent()) {
      l->parent()->StackAtTop(l);
    }
  }
  // Prevent web content's native view from stealing events
  SetWebContentEventProcessing(false);

  // New tab mode: empty textfield for fresh input
  updating_textfield_ = true;
  textfield_->SetText(u"");
  updating_textfield_ = false;

  // Show search icon for new tab mode (empty input)
  UpdateInputIcon();
  ClearSuggestions();

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
  ClearSuggestions();
  command_mode_ = false;
  command_target_.reset();

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
  if (browser_view && browser_view->contents_web_view()) {
    browser_view->contents_web_view()->RequestFocus();
  }
}

void DaoCommandBarView::Layout(PassKey) {
  // Position glass container centered horizontally, ~38% from top
  const int kCardWidth = 600;
  const int kCardHeight = 52;

  int card_width = std::min(kCardWidth, width() - 40);
  int card_x = (width() - card_width) / 2;
  int card_y = static_cast<int>(height() * 0.38) - kCardHeight / 2;
  card_y = std::max(20, card_y);

  // Calculate total glass container height
  int glass_height = kCardHeight;
  bool has_dropdown =
      dropdown_scroll_view_ && dropdown_scroll_view_->GetVisible();
  int dropdown_height = 0;
  if (has_dropdown) {
    dropdown_height =
        std::min(visible_suggestion_count_, kVisibleSuggestionRows) * 40 + 8;
    glass_height += dropdown_height;
  }

  // Position the shadow view behind the glass container, expanded by
  // kShadowPadding so the blurred shadow is not clipped by the layer.
  constexpr int kPad = CommandBarShadowView::kShadowPadding;
  if (shadow_view_) {
    shadow_view_->SetBounds(card_x - kPad, card_y - kPad,
                            card_width + 2 * kPad,
                            glass_height + 2 * kPad);
  }

  // Position the unified glass container
  if (glass_container_) {
    glass_container_->SetBounds(card_x, card_y, card_width, glass_height);
  }

  // Card is at the top of glass_container_ (local coords)
  if (card_container_) {
    card_container_->SetBounds(0, 0, card_width, kCardHeight);
  }

  // Dropdown is directly below the card (local coords)
  if (has_dropdown) {
    dropdown_scroll_view_->SetBounds(0, kCardHeight, card_width,
                                     dropdown_height);
  }

}

void DaoCommandBarView::OnPaint(gfx::Canvas* canvas) {
  // Shadow is painted by CommandBarShadowView (a sibling child with its
  // own layer).  Nothing else to paint here — the glass container handles
  // the frosted glass effect via its own layer + backdrop blur.
}

bool DaoCommandBarView::OnMousePressed(const ui::MouseEvent& event) {
  gfx::Point loc = event.location();

  // Click inside glass container (card + dropdown) = normal interaction
  if (glass_container_ && glass_container_->bounds().Contains(loc)) {
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

  // A real text edit commits the textfield's current visible contents as the
  // next query. When a preview suffix is selected, normal Textfield editing
  // replaces that suffix before this callback runs.
  ClearSelectionPreview(false);

  const size_t prev_len = last_text_length_;
  const size_t new_len = new_contents.length();
  const bool is_deletion = new_len < prev_len;

  user_input_text_ = new_contents;
  last_text_length_ = new_len;
  selected_index_ = -1;
  selected_command_id_ = -1;
  command_selection_invalidated_ = false;
  selection_explicitly_changed_ = false;

  // Scope Chromium's provider-level inline suppression to deletion requests.
  // Rejected preview text is tracked separately so a later insertion restores
  // normal provider ranking without allowing that exact stale preview back.
  suppress_ghost_for_current_query_ = is_deletion;
  inline_autocompletion_.clear();

  const std::u16string trimmed = NormalizeSearchTerms(new_contents);
  command_mode_ = browser_->profile()->GetPrefs()->GetBoolean(
                      dao::prefs::kDaoCommandBarCommandModeEnabled) &&
                  !trimmed.empty() && trimmed.front() == u'>';

  UpdateInputIcon();

  if (trimmed.empty()) {
    StopAutocomplete();
    ClearSuggestions();
    return;
  }

  if (command_mode_) {
    StopAutocomplete();
    UpdateSuggestions();
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

  // First Backspace rejects a non-identical preview without deleting query
  // text. Suppression prevents the same async result from restoring it until
  // the user explicitly selects a row again.
  if (key_event.key_code() == ui::VKEY_BACK &&
      selection_preview_active_ &&
      selection_preview_text_ != user_input_text_) {
    rejected_selection_preview_text_ = selection_preview_text_;
    ClearSelectionPreview(true);
    suppress_ghost_for_current_query_ = true;
    selection_explicitly_changed_ = false;
    return true;
  }

  // First Backspace with the inline suffix selected rejects only that suffix,
  // leaving the user's typed prefix intact. A broader selection, including
  // Select All, follows normal Textfield editing semantics.
  const gfx::Range selected_range = sender->GetSelectedRange();
  const size_t inline_text_end =
      user_input_text_.length() + inline_autocompletion_.length();
  if (key_event.key_code() == ui::VKEY_BACK &&
      !inline_autocompletion_.empty() &&
      selected_range.GetMin() == user_input_text_.length() &&
      selected_range.GetMax() == inline_text_end) {
    ClearInlineAutocompletion(true);
    suppress_ghost_for_current_query_ = true;
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
      int next = GetNextEnabledIndex(selected_index_, 1);
      SetSelectedIndex(next, true);
    }
    return true;
  }

  if (key_event.key_code() == ui::VKEY_UP) {
    if (visible_suggestion_count_ > 0) {
      int prev = GetNextEnabledIndex(selected_index_, -1);
      SetSelectedIndex(prev, true);
    }
    return true;
  }

  // Right arrow accepts either an explicit preview or the automatically
  // highlighted row. Automatic highlighting alone must never write suggestion
  // text into the textfield.
  if (key_event.key_code() == ui::VKEY_RIGHT) {
    if (selection_preview_active_) {
      AcceptSelectionPreview();
      return true;
    }

    if (sender->GetCursorPosition() == user_input_text_.length()) {
      if (selected_index_ >= 0 &&
          selected_index_ < visible_suggestion_count_) {
        SetSelectedIndex(selected_index_, true);
        if (selection_preview_active_) {
          AcceptSelectionPreview();
          return true;
        }
      }

      if (selection_explicitly_changed_) {
        if (const AutocompleteMatch* selected_match =
                GetSelectedVisibleAutocompleteMatch();
            selected_match && !selected_match->fill_into_edit.empty()) {
          FillInput(selected_match->fill_into_edit);
          return true;
        }
      }

      if (!inline_autocompletion_.empty()) {
        FillInput(user_input_text_ + inline_autocompletion_);
        return true;
      }
    }
  }

  if (key_event.key_code() == ui::VKEY_TAB) {
    if (!selection_preview_active_ && selected_index_ >= 0 &&
        selected_index_ < visible_suggestion_count_) {
      SetSelectedIndex(selected_index_, true);
    }
    if (selection_preview_active_) {
      AcceptSelectionPreview();
    }
    return true;
  }

  return false;
}

void DaoCommandBarView::FillInput(const std::u16string& text) {
  ClearSelectionPreview(false);
  const bool retain_command_prefix = command_mode_;
  user_input_text_ = retain_command_prefix ? u"> " + text : text;
  inline_autocompletion_.clear();
  updating_textfield_ = true;
  textfield_->SetText(user_input_text_);
  textfield_->SetSelectedRange(gfx::Range(user_input_text_.length()));
  updating_textfield_ = false;
  last_text_length_ = user_input_text_.length();
  selected_index_ = -1;
  selection_explicitly_changed_ = false;
  rejected_selection_preview_text_.clear();
  suppress_ghost_for_current_query_ = false;
  command_mode_ = retain_command_prefix;
  if (command_mode_) {
    UpdateSuggestions();
  } else {
    StartAutocomplete(user_input_text_);
  }
}

void DaoCommandBarView::OnResultChanged(AutocompleteController* controller,
                                        bool default_match_changed) {
  if (command_mode_) {
    return;
  }
  UpdateSuggestions();
  // Inline text must update on every tick — providers may publish a valid
  // inline_autocompletion without flipping the default match.
  UpdateInlineAutocompletion();
  // The input field's leading icon is keyed off the default match; only
  // refresh it when the default has actually changed so we don't fire
  // redundant favicon lookups for every async tick.
  if (default_match_changed) {
    UpdateInputIcon();
  }
}

void DaoCommandBarView::StartAutocomplete(const std::u16string& text) {
  ++autocomplete_start_count_for_testing_;
  InitAutocompleteController();
  if (!autocomplete_controller_ || !scheme_classifier_) {
    return;
  }

  // Pass the caret position via the cursor_position constructor. Some
  // providers (notably HistoryURL) suppress inline_autocompletion entirely
  // when the cursor is unspecified, which breaks the stability that ghost
  // text depends on.
  AutocompleteInput input(
      text, /*cursor_position=*/text.length(),
      metrics::OmniboxEventProto::INSTANT_NTP_WITH_OMNIBOX_AS_STARTING_FOCUS,
      *scheme_classifier_);
  if (EnhancedSuggestionsEnabled()) {
    if (auto* contents = browser_->tab_strip_model()->GetActiveWebContents()) {
      input.set_current_url(contents->GetVisibleURL());
      input.set_current_title(contents->GetTitle());
    }
    if (text.empty()) {
      input.set_focus_type(metrics::OmniboxFocusType::INTERACTION_FOCUS);
    }
  }

  // Deletion queries must also tell Chromium not to compute
  // inline_autocompletion; without this, providers can still publish
  // aggressive history URL tails that Dao then has to filter out tick-by-tick.
  // Keep this scoped to deletion only because providers also use the flag when
  // calculating match relevance.
  if (suppress_ghost_for_current_query_) {
    input.set_prevent_inline_autocomplete(true);
  }
  autocomplete_controller_->Start(input);
}

void DaoCommandBarView::StopAutocomplete() {
  if (autocomplete_controller_) {
    autocomplete_controller_->Stop(AutocompleteStopReason::kClobbered);
  }
  // Stop() may synchronously publish a final result tick. Clear after it so
  // that tick cannot leave a preview active during dismissal or submission.
  ClearSelectionPreview(true);
  ClearInlineAutocompletion(true);
  selected_index_ = -1;
  selection_explicitly_changed_ = false;
}

void DaoCommandBarView::ClearSuggestions() {
  ClearSelectionPreview(true);
  for (DaoSuggestionItemView* suggestion_view : suggestion_views_) {
    suggestion_view->SetVisible(false);
    suggestion_view->SetSelected(false);
  }
  visible_matches_.clear();
  displayed_rows_.clear();
  dropdown_scroll_view_->SetVisible(false);
  visible_suggestion_count_ = 0;
  selected_index_ = -1;
  selected_command_id_ = -1;
  command_selection_invalidated_ = false;
  selection_explicitly_changed_ = false;
  ask_ai_row_index_ = -1;
  InvalidateLayout();
}

std::vector<int> DaoCommandBarView::GetMatchingCommandIndices(
    bool command_mode) const {
  std::u16string query = NormalizeSearchTerms(user_input_text_);
  if (command_mode && !query.empty() && query.front() == u'>') {
    query = NormalizeCommandText(query.substr(1));
  } else {
    query = NormalizeCommandText(query);
  }

  if (!command_mode && (query.size() < 2 || LooksLikeURL(user_input_text_) ||
                        LooksLikeLocalFilePath(base::UTF16ToUTF8(query)))) {
    return {};
  }

  std::vector<int> exact;
  std::vector<int> prefixes;
  for (size_t i = 0; i < kCommands.size(); ++i) {
    bool is_exact = query.empty();
    bool is_prefix = query.empty();
    std::vector<std::u16string> terms = CommandTerms(kCommands[i]);
    terms.push_back(NormalizeCommandText(GetCommandTitle(i)));
    for (const std::u16string& term : terms) {
      is_exact = is_exact || term == query;
      is_prefix = is_prefix ||
                  base::StartsWith(term, query, base::CompareCase::SENSITIVE);
    }
    if (is_exact) {
      exact.push_back(static_cast<int>(i));
    } else if (is_prefix) {
      prefixes.push_back(static_cast<int>(i));
    }
  }
  exact.insert(exact.end(), prefixes.begin(), prefixes.end());
  return exact;
}

bool DaoCommandBarView::IsCommandEnabled(int command_index) const {
  if (command_index < 0 ||
      command_index >= static_cast<int>(kCommands.size())) {
    return false;
  }
  const CommandDefinition& command = kCommands[command_index];
  switch (command.action) {
    case CommandAction::kBrowserCommand:
      return chrome::IsCommandEnabled(browser_, command.command_id);
    case CommandAction::kCopyCurrentLink:
      return command_target_ && !command_target_->IsBeingDestroyed() &&
             command_target_->GetVisibleURL().is_valid() &&
             !command_target_->GetVisibleURL().is_empty();
    case CommandAction::kAgentSettings:
      return chrome::IsCommandEnabled(browser_, IDC_OPTIONS);
  }
  return false;
}

std::u16string DaoCommandBarView::GetCommandTitle(int command_index) const {
  if (command_index == 1 && IsCommandEnabled(command_index)) {
    sessions::TabRestoreService* restore_service =
        TabRestoreServiceFactory::GetForProfile(browser_->profile());
    if (restore_service) {
      restore_service->LoadTabsFromLastSession();
      if (!restore_service->entries().empty()) {
        const sessions::tab_restore::Type type =
            restore_service->entries().front()->type;
        if (type == sessions::tab_restore::Type::WINDOW) {
          return gfx::RemoveAccelerator(
              l10n_util::GetStringUTF16(IDS_REOPEN_WINDOW));
        }
        if (type == sessions::tab_restore::Type::GROUP) {
          return gfx::RemoveAccelerator(
              l10n_util::GetStringUTF16(IDS_REOPEN_GROUP));
        }
      }
    }
  }
  return l10n_util::GetStringUTF16(kCommands[command_index].title_id);
}

std::u16string DaoCommandBarView::GetCommandDisabledReason(
    int command_index) const {
  if (command_index == 1) {
    return l10n_util::GetStringUTF16(
        IDS_DAO_COMMAND_REOPEN_CLOSED_TAB_UNAVAILABLE);
  }
  if (command_index == 2) {
    return l10n_util::GetStringUTF16(
        IDS_DAO_COMMAND_COPY_CURRENT_LINK_UNAVAILABLE);
  }
  return l10n_util::GetStringUTF16(IDS_DAO_COMMAND_UNAVAILABLE);
}

void DaoCommandBarView::UpdateSuggestions() {
  if (!autocomplete_controller_) {
    return;
  }

  const std::u16string search_terms = NormalizeSearchTerms(user_input_text_);
  if (search_terms.empty()) {
    ClearSuggestions();
    return;
  }

  const int previously_selected_command = selected_command_id_;
  std::optional<SuggestionRow::Kind> previously_selected_non_command_kind;
  GURL previously_selected_destination;
  std::u16string previously_selected_fill_into_edit;
  if (selection_explicitly_changed_ && previously_selected_command < 0) {
    const SuggestionRow* selected_row = GetSelectedRow();
    if (selected_row && selected_row->kind != SuggestionRow::Kind::kCommand) {
      previously_selected_non_command_kind = selected_row->kind;
      if (selected_row->kind == SuggestionRow::Kind::kAutocomplete &&
          selected_row->index >= 0 &&
          selected_row->index < static_cast<int>(visible_matches_.size())) {
        const AutocompleteMatch& match = visible_matches_[selected_row->index];
        previously_selected_destination = match.destination_url;
        previously_selected_fill_into_edit = match.fill_into_edit;
      }
    }
  }
  const bool enhanced_suggestions_enabled = EnhancedSuggestionsEnabled();
  const AutocompleteResult& result = autocomplete_controller_->result();
  const bool show_ask_ai = !command_mode_ && ShouldShowAskAiSuggestion();
  visible_matches_.clear();
  displayed_rows_.clear();
  if (!command_mode_) {
    visible_matches_.reserve(result.size() + 1);
    for (size_t i = 0; i < result.size(); ++i) {
      visible_matches_.push_back(result.match_at(i));
    }
  }

  const bool exact_search_is_visible =
      command_mode_ ||
      std::any_of(visible_matches_.begin(), visible_matches_.end(),
                  [&](const AutocompleteMatch& match) {
                    return IsExactSearchMatch(match, search_terms);
                  });
  if (!exact_search_is_visible) {
    visible_matches_.push_back(CreateExactSearchMatch(search_terms));
  }

  std::vector<SuggestionRow> non_command_rows;
  non_command_rows.reserve(visible_matches_.size() + 1);
  for (size_t i = 0; i < visible_matches_.size(); ++i) {
    non_command_rows.push_back(
        {SuggestionRow::Kind::kAutocomplete, static_cast<int>(i)});
    if (show_ask_ai && i == 0) {
      non_command_rows.push_back({SuggestionRow::Kind::kAskAi, 0});
    }
  }
  if (show_ask_ai && visible_matches_.empty()) {
    non_command_rows.push_back({SuggestionRow::Kind::kAskAi, 0});
  }

  const std::vector<int> matching_commands =
      GetMatchingCommandIndices(command_mode_);
  std::vector<SuggestionRow> front_commands;
  std::vector<SuggestionRow> remaining_commands;
  const std::u16string normalized_query = NormalizeCommandText(search_terms);
  for (int command_index : matching_commands) {
    bool exact = false;
    std::vector<std::u16string> terms = CommandTerms(kCommands[command_index]);
    terms.push_back(NormalizeCommandText(GetCommandTitle(command_index)));
    for (const auto& term : terms) {
      exact = exact || term == normalized_query;
    }
    SuggestionRow row{SuggestionRow::Kind::kCommand, command_index,
                      IsCommandEnabled(command_index)};
    if (!command_mode_ && exact && kCommands[command_index].safe_default &&
        row.enabled) {
      front_commands.push_back(row);
    } else if (command_mode_ || row.enabled) {
      remaining_commands.push_back(row);
    }
  }
  if (!command_mode_) {
    if (front_commands.size() >= 2) {
      front_commands.resize(2);
      remaining_commands.clear();
    } else {
      remaining_commands.resize(
          std::min(remaining_commands.size(), 2 - front_commands.size()));
    }
  }

  displayed_rows_.insert(displayed_rows_.end(), front_commands.begin(),
                         front_commands.end());
  if (command_mode_) {
    displayed_rows_.insert(displayed_rows_.end(), remaining_commands.begin(),
                           remaining_commands.end());
  } else {
    if (!non_command_rows.empty()) {
      displayed_rows_.push_back(non_command_rows.front());
    }
    displayed_rows_.insert(displayed_rows_.end(), remaining_commands.begin(),
                           remaining_commands.end());
    displayed_rows_.insert(
        displayed_rows_.end(),
        non_command_rows.begin() + std::min<size_t>(1, non_command_rows.size()),
        non_command_rows.end());
  }

  ask_ai_row_index_ = -1;
  for (size_t i = 0; i < displayed_rows_.size(); ++i) {
    if (displayed_rows_[i].kind == SuggestionRow::Kind::kAskAi) {
      ask_ai_row_index_ = static_cast<int>(i);
      break;
    }
  }
  visible_suggestion_count_ = static_cast<int>(displayed_rows_.size());
  while (suggestion_views_.size() <
         static_cast<size_t>(visible_suggestion_count_)) {
    const int index = static_cast<int>(suggestion_views_.size());
    suggestion_views_.push_back(dropdown_container_->AddChildView(
        std::make_unique<DaoSuggestionItemView>(
            index,
            base::BindRepeating(&DaoCommandBarView::OnSuggestionClicked,
                                base::Unretained(this)),
            browser_->profile())));
  }

  // Check if we have a bookmark model for icon determination
  bookmarks::BookmarkModel* bookmark_model =
      BookmarkModelFactory::GetForBrowserContext(browser_->profile());

  for (int i = 0; i < static_cast<int>(suggestion_views_.size()); ++i) {
    if (i >= visible_suggestion_count_) {
      suggestion_views_[i]->SetVisible(false);
      suggestion_views_[i]->SetSelected(false);
      continue;
    }
    const SuggestionRow& row = displayed_rows_[i];
    if (row.kind == SuggestionRow::Kind::kAskAi) {
      suggestion_views_[i]->SetAskAiPrompt(
          user_input_text_,
          enhanced_suggestions_enabled
              ? l10n_util::GetStringUTF16(IDS_DAO_SUGGESTION_INTENT_ASK_DAO)
              : std::u16string());
      suggestion_views_[i]->SetVisible(true);
      suggestion_views_[i]->SetSelected(i == selected_index_);
    } else if (row.kind == SuggestionRow::Kind::kCommand) {
      suggestion_views_[i]->SetCommand(
          GetCommandTitle(row.index), kCommands[row.index].icon,
          row.enabled ? std::u16string() : GetCommandDisabledReason(row.index),
          row.enabled);
      suggestion_views_[i]->SetVisible(true);
      suggestion_views_[i]->SetSelected(i == selected_index_);
    } else {
      const AutocompleteMatch& match = visible_matches_[row.index];
      bool is_bookmark =
          bookmark_model && bookmark_model->IsBookmarked(match.destination_url);
      suggestion_views_[i]->SetMatch(match, is_bookmark,
                                     enhanced_suggestions_enabled
                                         ? GetIntentLabelForMatch(match)
                                         : std::u16string());
      suggestion_views_[i]->SetVisible(true);
      suggestion_views_[i]->SetSelected(i == selected_index_);
    }
  }

  if (visible_suggestion_count_ > 0) {
    dropdown_scroll_view_->SetVisible(true);
    int next_selected_index = -1;
    if (previously_selected_command >= 0) {
      for (size_t i = 0; i < displayed_rows_.size(); ++i) {
        const auto& row = displayed_rows_[i];
        if (row.kind == SuggestionRow::Kind::kCommand &&
            row.index == previously_selected_command && row.enabled) {
          next_selected_index = static_cast<int>(i);
          break;
        }
      }
      if (next_selected_index < 0) {
        command_selection_invalidated_ = true;
      }
    } else if (previously_selected_non_command_kind.has_value()) {
      for (size_t i = 0; i < displayed_rows_.size(); ++i) {
        const SuggestionRow& row = displayed_rows_[i];
        if (row.kind != *previously_selected_non_command_kind) {
          continue;
        }
        if (row.kind == SuggestionRow::Kind::kAskAi) {
          next_selected_index = static_cast<int>(i);
          break;
        }
        const AutocompleteMatch& match = visible_matches_[row.index];
        if (match.destination_url == previously_selected_destination &&
            match.fill_into_edit == previously_selected_fill_into_edit) {
          next_selected_index = static_cast<int>(i);
          break;
        }
      }
      if (next_selected_index < 0) {
        selection_explicitly_changed_ = false;
      }
    }
    if (next_selected_index < 0 && previously_selected_command < 0 &&
        !command_selection_invalidated_) {
      if (command_mode_) {
        next_selected_index = GetNextEnabledIndex(-1, 1);
      } else if (!front_commands.empty() || !displayed_rows_.empty()) {
        next_selected_index = 0;
      }
    }

    // Refresh even when the selected index is unchanged because async
    // providers can replace the match that backs the same visible row. An
    // automatically highlighted row does not preview into the textfield until
    // the user explicitly browses or accepts it.
    SetSelectedIndex(next_selected_index, false);
  } else {
    dropdown_scroll_view_->SetVisible(false);
    ClearSelectionPreview(true);
  }

  InvalidateLayout();
}

std::u16string DaoCommandBarView::GetIntentLabelForMatch(
    const AutocompleteMatch& match) const {
  if (match.has_tab_match.value_or(false)) {
    return l10n_util::GetStringUTF16(IDS_DAO_SUGGESTION_INTENT_SWITCH_TAB);
  }

  if (AutocompleteMatch::IsSearchType(match.type)) {
    return l10n_util::GetStringUTF16(IDS_DAO_SUGGESTION_INTENT_SEARCH);
  }

  return l10n_util::GetStringUTF16(IDS_DAO_SUGGESTION_INTENT_OPEN);
}

GURL DaoCommandBarView::GetSearchUrl(const std::u16string& search_terms) const {
  const std::u16string normalized_terms = NormalizeSearchTerms(search_terms);
  if (normalized_terms.empty()) {
    return GURL();
  }

  TemplateURLService* template_url_service =
      TemplateURLServiceFactory::GetForProfile(browser_->profile());
  const TemplateURL* default_provider =
      template_url_service ? template_url_service->GetDefaultSearchProvider()
                           : nullptr;
  if (default_provider && default_provider->SupportsReplacement(
                              template_url_service->search_terms_data())) {
    GURL url = default_provider->GenerateSearchURL(
        template_url_service->search_terms_data(), normalized_terms);
    if (url.is_valid()) {
      return url;
    }
  }

  return GURL(
      "https://www.google.com/search?q=" +
      base::EscapeQueryParamValue(base::UTF16ToUTF8(normalized_terms), true));
}

AutocompleteMatch DaoCommandBarView::CreateExactSearchMatch(
    const std::u16string& search_terms) const {
  const std::u16string normalized_terms = NormalizeSearchTerms(search_terms);
  AutocompleteMatch match(nullptr, 0, false,
                          AutocompleteMatchType::SEARCH_WHAT_YOU_TYPED);
  match.fill_into_edit = normalized_terms;
  match.contents = normalized_terms;
  match.contents_class = {{0, AutocompleteMatch::ACMatchClassification::NONE}};
  match.destination_url = GetSearchUrl(normalized_terms);
  return match;
}

bool DaoCommandBarView::IsExactSearchMatch(
    const AutocompleteMatch& match,
    const std::u16string& search_terms) const {
  return AutocompleteMatch::IsSearchType(match.type) &&
         !match.has_tab_match.value_or(false) &&
         match.destination_url.is_valid() &&
         NormalizeSearchTerms(match.fill_into_edit) ==
             NormalizeSearchTerms(search_terms);
}

std::u16string DaoCommandBarView::GetInlineAutocompletionForResult() const {
  if (!autocomplete_controller_ || selection_preview_active_ ||
      suppress_ghost_for_current_query_ || user_input_text_.empty()) {
    return std::u16string();
  }

  const SuggestionRow* selected_row = GetSelectedRow();
  if (selected_row && selected_row->kind == SuggestionRow::Kind::kCommand) {
    return std::u16string();
  }

  const AutocompleteResult& result = autocomplete_controller_->result();

  const AutocompleteMatch* default_match = result.default_match();
  if (!default_match) {
    return std::u16string();
  }

  if (!rejected_selection_preview_text_.empty() &&
      default_match->fill_into_edit == rejected_selection_preview_text_) {
    return std::u16string();
  }

  if (!IsAutocompleteResultStableForInlineAutocompletion()) {
    return std::u16string();
  }

  return default_match->inline_autocompletion;
}

bool DaoCommandBarView::IsAutocompleteResultStableForInlineAutocompletion()
    const {
  if (autocomplete_update_type_for_testing_.has_value()) {
    // Mirror AutocompleteController::done(). kLastAsyncPassExceptDoc still
    // waits for the doc provider and can carry transient inline text.
    return *autocomplete_update_type_for_testing_ ==
               AutocompleteController::UpdateType::kNone ||
           *autocomplete_update_type_for_testing_ ==
               AutocompleteController::UpdateType::kSyncPassOnly ||
           *autocomplete_update_type_for_testing_ ==
               AutocompleteController::UpdateType::kLastAsyncPass ||
           *autocomplete_update_type_for_testing_ ==
               AutocompleteController::UpdateType::kStop ||
           *autocomplete_update_type_for_testing_ ==
               AutocompleteController::UpdateType::kMatchDeletion;
  }

  if (!autocomplete_controller_) {
    return false;
  }

  return autocomplete_controller_->done();
}

bool DaoCommandBarView::HasSubmittableInlineAutocompletion() const {
  return textfield_ && !selection_preview_active_ &&
         !inline_autocompletion_.empty() &&
         std::u16string(textfield_->GetText()) ==
             user_input_text_ + inline_autocompletion_;
}

std::u16string DaoCommandBarView::GetInlineAutocompletedInputText() const {
  return std::u16string(textfield_->GetText());
}

const AutocompleteMatch*
DaoCommandBarView::GetVisibleInlineAutocompletionMatch() const {
  if (!autocomplete_controller_ || !HasSubmittableInlineAutocompletion()) {
    return nullptr;
  }

  const AutocompleteResult& result = autocomplete_controller_->result();
  const AutocompleteMatch* default_match = result.default_match();
  if (!default_match) {
    return nullptr;
  }

  if (!default_match->inline_autocompletion.empty() &&
      default_match->inline_autocompletion == inline_autocompletion_) {
    return default_match;
  }

  return nullptr;
}

const AutocompleteMatch*
DaoCommandBarView::GetSelectedVisibleAutocompleteMatch() const {
  const SuggestionRow* row = GetSelectedRow();
  if (!row || row->kind != SuggestionRow::Kind::kAutocomplete) {
    return nullptr;
  }
  if (row->index >= 0 &&
      row->index < static_cast<int>(visible_matches_.size())) {
    return &visible_matches_[row->index];
  }

  return nullptr;
}

const DaoCommandBarView::SuggestionRow* DaoCommandBarView::GetSelectedRow()
    const {
  if (selected_index_ < 0 ||
      selected_index_ >= static_cast<int>(displayed_rows_.size())) {
    return nullptr;
  }
  return &displayed_rows_[selected_index_];
}

std::u16string DaoCommandBarView::GetSelectionPreviewText() const {
  if (selected_index_ < 0 ||
      selected_index_ >= visible_suggestion_count_) {
    return std::u16string();
  }

  const SuggestionRow* row = GetSelectedRow();
  if (!row) {
    return std::u16string();
  }
  if (row->kind == SuggestionRow::Kind::kAskAi) {
    return user_input_text_;
  }
  if (row->kind == SuggestionRow::Kind::kCommand) {
    return GetCommandTitle(row->index);
  }

  const AutocompleteMatch* selected_match =
      GetSelectedVisibleAutocompleteMatch();
  if (!selected_match ||
      IsExactSearchMatch(*selected_match, user_input_text_) ||
      selected_match->fill_into_edit.empty()) {
    return user_input_text_;
  }

  return selected_match->fill_into_edit;
}

void DaoCommandBarView::UpdateSelectionPreview() {
  if (selected_index_ < 0 ||
      selected_index_ >= visible_suggestion_count_) {
    ClearSelectionPreview(true);
    return;
  }

  std::u16string preview = GetSelectionPreviewText();
  if (preview.empty()) {
    ClearSelectionPreview(true);
    return;
  }

  const bool preview_rejected =
      !selection_explicitly_changed_ &&
      (suppress_ghost_for_current_query_ ||
       preview == rejected_selection_preview_text_);
  if (preview_rejected) {
    ClearSelectionPreview(true);
    return;
  }

  selection_preview_text_ = std::move(preview);
  selection_preview_active_ = true;
  inline_autocompletion_.clear();

  updating_textfield_ = true;
  textfield_->SetText(selection_preview_text_);
  if (base::StartsWith(selection_preview_text_, user_input_text_,
                       base::CompareCase::SENSITIVE)) {
    textfield_->SetSelectedRange(gfx::Range(
        user_input_text_.length(), selection_preview_text_.length()));
  } else {
    textfield_->SetSelectedRange(
        gfx::Range(selection_preview_text_.length()));
  }
  updating_textfield_ = false;
}

void DaoCommandBarView::ClearSelectionPreview(bool restore_user_input) {
  const bool was_active = selection_preview_active_;
  selection_preview_active_ = false;
  selection_preview_text_.clear();

  if (!restore_user_input || !was_active || !textfield_) {
    return;
  }

  updating_textfield_ = true;
  textfield_->SetText(user_input_text_);
  textfield_->SetSelectedRange(gfx::Range(user_input_text_.length()));
  updating_textfield_ = false;
}

void DaoCommandBarView::AcceptSelectionPreview() {
  if (!selection_preview_active_) {
    return;
  }

  const std::u16string accepted_text = selection_preview_text_;
  FillInput(accepted_text);
}

void DaoCommandBarView::UpdateInlineAutocompletion() {
  // Replacing the Textfield contents while an IME composition is active can
  // cancel or corrupt the composition. The committed edit will start another
  // autocomplete request, so defer inline completion until that result tick.
  if (textfield_ && textfield_->IsIMEComposing()) {
    return;
  }

  std::u16string inline_autocompletion =
      GetInlineAutocompletionForResult();
  if (inline_autocompletion == inline_autocompletion_) {
    // Preserve a user-created selection such as Select All when an async
    // provider republishes the same completion.
    return;
  }

  const std::u16string previous_display_text =
      user_input_text_ + inline_autocompletion_;
  inline_autocompletion_ = std::move(inline_autocompletion);
  if (!textfield_ || selection_preview_active_) {
    return;
  }

  updating_textfield_ = true;
  if (inline_autocompletion_.empty()) {
    if (std::u16string(textfield_->GetText()) == previous_display_text) {
      textfield_->SetText(user_input_text_);
      textfield_->SetSelectedRange(gfx::Range(user_input_text_.length()));
    }
  } else {
    const std::u16string display_text =
        user_input_text_ + inline_autocompletion_;
    // Match Chromium's Omnibox model: the full completion lives in the
    // Textfield while a reversed selection keeps the caret at the end of the
    // user's typed prefix. This makes Select All, copy, and replacement edits
    // naturally include the completion. Pre-scrolling around that caret keeps
    // a long suffix from pushing the typed prefix out of view.
    SetTextAndSelectedRangeKeepingCaretVisible(
        textfield_, display_text,
        gfx::Range(display_text.length(), user_input_text_.length()));
  }
  updating_textfield_ = false;
}

void DaoCommandBarView::ClearInlineAutocompletion(bool restore_user_input) {
  if (inline_autocompletion_.empty()) {
    return;
  }

  const std::u16string display_text =
      user_input_text_ + inline_autocompletion_;
  inline_autocompletion_.clear();
  if (!restore_user_input || !textfield_ ||
      std::u16string(textfield_->GetText()) != display_text) {
    return;
  }

  updating_textfield_ = true;
  textfield_->SetText(user_input_text_);
  textfield_->SetSelectedRange(gfx::Range(user_input_text_.length()));
  updating_textfield_ = false;
}

void DaoCommandBarView::UpdateInputIcon() {
  if (!favicon_icon_) {
    return;
  }

  // Cancel any pending favicon request for the input icon
  icon_favicon_tracker_.TryCancelAll();
  pending_icon_favicon_url_ = GURL();

  const SkColor icon_color = SuggestionIconColor();

  if (const SuggestionRow* row = GetSelectedRow();
      row && row->kind == SuggestionRow::Kind::kCommand) {
    favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(
        CreateLucideImageSkia(kCommands[row->index].icon, 18, icon_color)));
    favicon_icon_->SetVisible(true);
    return;
  }

  // Ask-AI row is selected: mirror the sparkle icon shown on that row into
  // the input-field icon so the glyph the user is about to commit is
  // visible before they press Enter.
  if (selected_index_ >= 0 && selected_index_ == ask_ai_row_index_) {
    favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(
        CreateLucideImageSkia(LucideIcon::kSparkles, 18, icon_color)));
    favicon_icon_->SetVisible(true);
    return;
  }

  // If there's a selected autocomplete match, use its type.
  if (const AutocompleteMatch* selected_match =
          GetSelectedVisibleAutocompleteMatch()) {
    const AutocompleteMatch& match = *selected_match;
    bool is_search = AutocompleteMatch::IsSearchType(match.type);
    if (is_search) {
      favicon_icon_->SetImage(
          ui::ImageModel::FromImageSkia(gfx::CreateVectorIcon(
              vector_icons::kSearchChromeRefreshIcon, 18, icon_color)));
    } else {
      // Set page icon as immediate fallback, then try loading favicon
      favicon_icon_->SetImage(
          ui::ImageModel::FromImageSkia(gfx::CreateVectorIcon(
              omnibox::kPageChromeRefreshIcon, 18, icon_color)));

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
                             base::Unretained(this), match.destination_url),
              &icon_favicon_tracker_);
        }
      }
    }
    favicon_icon_->SetVisible(true);
    return;
  }

  // Fallback: determine icon from input text
  if (user_input_text_.empty()) {
    favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(gfx::CreateVectorIcon(
        vector_icons::kSearchChromeRefreshIcon, 18, icon_color)));
  } else if (LooksLikeURL(user_input_text_)) {
    favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(gfx::CreateVectorIcon(
        omnibox::kPageChromeRefreshIcon, 18, icon_color)));
  } else {
    favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(gfx::CreateVectorIcon(
        vector_icons::kSearchChromeRefreshIcon, 18, icon_color)));
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
  favicon_icon_->SetImage(ui::ImageModel::FromImageSkia(
      gfx::ImageSkiaOperations::CreateResizedImage(
          favicon, skia::ImageOperations::RESIZE_BEST, gfx::Size(18, 18))));
}

void DaoCommandBarView::SetUserInputAndInlineAutocompletionForTesting(
    const std::u16string& user_input,
    const std::u16string& inline_autocompletion) {
  ClearSelectionPreview(false);
  user_input_text_ = user_input;
  inline_autocompletion_ = inline_autocompletion;
  last_text_length_ = user_input.length();
  suppress_ghost_for_current_query_ = false;
  rejected_selection_preview_text_.clear();
  selected_index_ = -1;
  selection_explicitly_changed_ = false;
  autocomplete_start_count_for_testing_ = 0;
  ask_ai_row_index_ = -1;
  visible_matches_.clear();

  const std::u16string display_text = user_input + inline_autocompletion;
  updating_textfield_ = true;
  if (inline_autocompletion.empty()) {
    textfield_->SetText(display_text);
    textfield_->SetSelectedRange(gfx::Range(user_input.length()));
  } else {
    SetTextAndSelectedRangeKeepingCaretVisible(
        textfield_, display_text,
        gfx::Range(display_text.length(), user_input.length()));
  }
  updating_textfield_ = false;
  textfield_->RequestFocus();

  UpdateInputIcon();
}

void DaoCommandBarView::SetAutocompleteMatchesForTesting(
    const ACMatches& matches) {
  autocomplete_update_type_for_testing_.reset();
  InitAutocompleteController();
  AutocompleteResult& result =
      const_cast<AutocompleteResult&>(autocomplete_controller_->result());
  result.Reset();
  result.AppendMatches(matches);
  UpdateSuggestions();
  UpdateInlineAutocompletion();
}

void DaoCommandBarView::SetAutocompleteMatchesForTesting(
    const ACMatches& matches,
    bool autocomplete_done) {
  SetAutocompleteMatchesForTesting(
      matches, autocomplete_done
                   ? AutocompleteController::UpdateType::kLastAsyncPass
                   : AutocompleteController::UpdateType::kAsyncPass);
}

void DaoCommandBarView::SetAutocompleteMatchesForTesting(
    const ACMatches& matches,
    AutocompleteController::UpdateType update_type) {
  autocomplete_update_type_for_testing_ = update_type;
  InitAutocompleteController();
  AutocompleteResult& result =
      const_cast<AutocompleteResult&>(autocomplete_controller_->result());
  result.Reset();
  result.AppendMatches(matches);
  UpdateSuggestions();
  UpdateInlineAutocompletion();
}

std::u16string DaoCommandBarView::GetVisibleSuggestionTitleForTesting(
    int index) const {
  if (index < 0 || index >= static_cast<int>(displayed_rows_.size())) {
    return std::u16string();
  }
  const SuggestionRow& row = displayed_rows_[index];
  if (row.kind == SuggestionRow::Kind::kCommand) {
    return GetCommandTitle(row.index);
  }
  if (row.kind == SuggestionRow::Kind::kAutocomplete) {
    return visible_matches_[row.index].contents;
  }
  return l10n_util::GetStringFUTF16(IDS_DAO_SUGGESTION_ASK_AI,
                                    user_input_text_);
}

bool DaoCommandBarView::IsVisibleSuggestionCommandForTesting(int index) const {
  return index >= 0 && index < static_cast<int>(displayed_rows_.size()) &&
         displayed_rows_[index].kind == SuggestionRow::Kind::kCommand;
}

bool DaoCommandBarView::HasVisibleCommandForTesting() const {
  return std::any_of(displayed_rows_.begin(), displayed_rows_.end(),
                     [](const SuggestionRow& row) {
                       return row.kind == SuggestionRow::Kind::kCommand;
                     });
}

bool DaoCommandBarView::IsVisibleSuggestionEnabledForTesting(int index) const {
  return index >= 0 && index < static_cast<int>(displayed_rows_.size()) &&
         displayed_rows_[index].enabled;
}

bool DaoCommandBarView::IsSelectedCommandForTesting() const {
  const SuggestionRow* row = GetSelectedRow();
  return row && row->kind == SuggestionRow::Kind::kCommand;
}

int DaoCommandBarView::GetSelectedCommandIdForTesting() const {
  return selected_command_id_;
}

int DaoCommandBarView::GetNextEnabledIndex(int start, int direction) const {
  if (displayed_rows_.empty()) {
    return -1;
  }
  int index = start;
  for (size_t i = 0; i < displayed_rows_.size(); ++i) {
    index = (index + direction + static_cast<int>(displayed_rows_.size())) %
            static_cast<int>(displayed_rows_.size());
    if (displayed_rows_[index].enabled) {
      return index;
    }
  }
  return -1;
}

void DaoCommandBarView::ExecuteSystemCommand(int command_index) {
  if (command_index < 0 ||
      command_index >= static_cast<int>(kCommands.size())) {
    return;
  }

  const CommandDefinition& command = kCommands[command_index];
  base::WeakPtr<content::WebContents> target = command_target_;
  const bool enabled = IsCommandEnabled(command_index);
  if (!enabled && command.action != CommandAction::kCopyCurrentLink) {
    return;
  }

  Hide();
  switch (command.action) {
    case CommandAction::kBrowserCommand:
      chrome::ExecuteCommand(browser_, command.command_id);
      return;
    case CommandAction::kCopyCurrentLink:
      CopyTabUrl(browser_, target.get());
      return;
    case CommandAction::kAgentSettings:
      chrome::ShowSettingsSubPage(browser_, "agent");
      return;
  }
}

void DaoCommandBarView::SetSelectedIndex(int index, bool user_initiated) {
  if (index >= 0 && (index >= static_cast<int>(displayed_rows_.size()) ||
                     !displayed_rows_[index].enabled)) {
    index = -1;
  }
  if (user_initiated) {
    command_selection_invalidated_ = false;
    selection_explicitly_changed_ = true;
    suppress_ghost_for_current_query_ = false;
    rejected_selection_preview_text_.clear();
  }

  if (index != selected_index_) {
    // Deselect old
    if (selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(suggestion_views_.size())) {
      suggestion_views_[selected_index_]->SetSelected(false);
    }

    selected_index_ = index;

    // Select new
    if (selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(suggestion_views_.size())) {
      suggestion_views_[selected_index_]->SetSelected(true);
      suggestion_views_[selected_index_]->ScrollViewToVisible();
    }
  }

  selected_command_id_ = -1;
  if (const SuggestionRow* row = GetSelectedRow();
      row && row->kind == SuggestionRow::Kind::kCommand) {
    selected_command_id_ = row->index;
  }

  if (selection_explicitly_changed_) {
    UpdateSelectionPreview();
  } else {
    // Provider updates may automatically highlight the first row, but the
    // user's query remains the sole textfield value until an explicit keyboard
    // or pointer selection occurs.
    ClearSelectionPreview(true);
  }

  // Update the input icon to reflect the selected match type
  UpdateInputIcon();
}

void DaoCommandBarView::ApplySelectedSuggestion() {
  // Empty input: Enter only dismisses the bar.
  if (user_input_text_.empty()) {
    Navigate(std::u16string());
    return;
  }

  if (const SuggestionRow* row = GetSelectedRow();
      row && row->kind == SuggestionRow::Kind::kCommand && row->enabled) {
    ExecuteSystemCommand(row->index);
    return;
  }
  if (command_mode_) {
    return;
  }

  if (selection_preview_active_) {
    if (selected_index_ == ask_ai_row_index_ && !user_input_text_.empty()) {
      SubmitAskAi(user_input_text_);
      return;
    }

    if (const AutocompleteMatch* selected_match =
            GetSelectedVisibleAutocompleteMatch()) {
      NavigateToMatch(*selected_match);
      return;
    }
  }

  if (!selection_explicitly_changed_) {
    if (HasSubmittableInlineAutocompletion()) {
      if (const AutocompleteMatch* inline_match =
              GetVisibleInlineAutocompletionMatch()) {
        NavigateToMatch(*inline_match);
        return;
      }

      Navigate(GetInlineAutocompletedInputText());
      return;
    }

    // Automatic highlighting is a visual default and must not mutate the
    // textfield. Enter may still submit that highlighted action, unless the
    // user has rejected a preview or edited under inline suppression.
    if (!suppress_ghost_for_current_query_ &&
        rejected_selection_preview_text_.empty()) {
      if (selected_index_ == ask_ai_row_index_ && !user_input_text_.empty()) {
        SubmitAskAi(user_input_text_);
        return;
      }

      if (const AutocompleteMatch* selected_match =
              GetSelectedVisibleAutocompleteMatch()) {
        NavigateToMatch(*selected_match);
        return;
      }
    }

    Navigate(std::u16string(textfield_->GetText()));
    return;
  }

  // Ask-AI row wins over autocomplete match lookup — its slot sits between
  // real matches, so check it first.
  if (selected_index_ >= 0 && selected_index_ == ask_ai_row_index_ &&
      !user_input_text_.empty()) {
    SubmitAskAi(user_input_text_);
    return;
  }

  if (const AutocompleteMatch* selected_match =
          GetSelectedVisibleAutocompleteMatch()) {
    NavigateToMatch(*selected_match);
  } else {
    // No selected match — use plain text navigation
    Navigate(GetInlineAutocompletedInputText());
  }
}

void DaoCommandBarView::SubmitAskAi(const std::u16string& prompt) {
  StopAutocomplete();
  dropdown_scroll_view_->SetVisible(false);
  visible_suggestion_count_ = 0;
  ask_ai_row_index_ = -1;

  // Cmd+T (new-tab mode) opens a fresh standalone agent question, so the
  // current page context is intentionally NOT attached. Cmd+L opens the
  // command bar over the current tab, so the page context IS attached
  // (the user is asking about the page they're on). Capture the mode now,
  // before the reset below clears it.
  const bool include_page_context = !is_new_tab_mode_;

  if (is_new_tab_mode_) {
    SetNewTabButtonHighlight(false);
    is_new_tab_mode_ = false;
  }

  SetVisible(false);
  SetWebContentEventProcessing(true);

  BrowserView* browser_view = BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_agent_sidebar()) {
    browser_view->dao_agent_sidebar()->ExpandAndSubmitPrompt(
        prompt, include_page_context);
    // Keep the right-side sidebar layout + address-bar chat-button state
    // in sync with the freshly-expanded agent panel.
    browser_view->InvalidateLayout();
    if (browser_view->dao_address_bar()) {
      browser_view->dao_address_bar()->SetChatButtonHighlighted(true);
    }
  }
}

void DaoCommandBarView::RecordShortcut(const AutocompleteMatch& match) {
  // Unlike Chromium, which records after the navigation commits
  // successfully (ChromeOmniboxNavigationObserver), Dao records at
  // accept time. A shortcut for an occasionally failing navigation is
  // harmless — its relevance decays when unused.
  scoped_refptr<ShortcutsBackend> shortcuts_backend =
      ShortcutsBackendFactory::GetForProfile(browser_->profile());
  // Can be null in incognito.
  if (!shortcuts_backend) {
    return;
  }
  shortcuts_backend->AddOrUpdateShortcut(user_input_text_, match);
}

void DaoCommandBarView::NavigateToMatch(const AutocompleteMatch& match) {
  // Learn from the accepted suggestion regardless of which navigation
  // branch runs below; invalid destinations fall back to plain text
  // navigation and carry nothing worth learning.
  if (match.destination_url.is_valid()) {
    RecordShortcut(match);
  }

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
    dropdown_scroll_view_->SetVisible(false);
    visible_suggestion_count_ = 0;
    SetVisible(false);
    SetWebContentEventProcessing(true);

    ::Navigate(&params);
    return;
  }

  GURL url = match.destination_url;
  if (!url.is_valid()) {
    Navigate(std::u16string(textfield_->GetText()));
    return;
  }

  StopAutocomplete();
  dropdown_scroll_view_->SetVisible(false);
  visible_suggestion_count_ = 0;

  if (is_new_tab_mode_) {
    SetNewTabButtonHighlight(false);
    is_new_tab_mode_ = false;
    SetVisible(false);
    SetWebContentEventProcessing(true);

    chrome::AddTabAt(browser_, url, 0, true);

    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->contents_web_view()) {
      browser_view->contents_web_view()->RequestFocus();
    }
  } else {
    NavigateParams params(browser_, url, ui::PAGE_TRANSITION_TYPED);
    params.disposition = WindowOpenDisposition::CURRENT_TAB;
    SetVisible(false);
    SetWebContentEventProcessing(true);
    ::Navigate(&params);

    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->contents_web_view()) {
      browser_view->contents_web_view()->RequestFocus();
    }
  }
}

void DaoCommandBarView::OnSuggestionClicked(int index) {
  SetSelectedIndex(index, true);
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
  if (LooksLikeLocalFilePath(input)) {
    url = url_formatter::FixupURL(input, std::string());
  } else if (LooksLikeURL(text)) {
    // Prepend https:// if no scheme
    if (input.find("://") == std::string::npos) {
      input = "https://" + input;
    }
    url = GURL(input);
  } else {
    url = GetSearchUrl(text);
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
  dropdown_scroll_view_->SetVisible(false);
  visible_suggestion_count_ = 0;

  if (is_new_tab_mode_) {
    // Create a new tab and navigate to the URL
    SetNewTabButtonHighlight(false);
    is_new_tab_mode_ = false;
    SetVisible(false);

    // Re-enable web content event processing
    SetWebContentEventProcessing(true);

    chrome::AddTabAt(browser_, url, 0, true);

    // Return focus to web contents
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->contents_web_view()) {
      browser_view->contents_web_view()->RequestFocus();
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
  dropdown_scroll_view_->SetVisible(false);
  visible_suggestion_count_ = 0;

  SetVisible(false);

  // Re-enable web content event processing
  SetWebContentEventProcessing(true);

  // Return focus to web contents
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->contents_web_view()) {
    browser_view->contents_web_view()->RequestFocus();
  }
}

void DaoCommandBarView::SetNewTabButtonHighlight(bool highlighted) {
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_sidebar()) {
    browser_view->dao_sidebar()->SetNewTabButtonHighlight(highlighted);
  }
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

  // Dao WebUI pages are entered through the Dao command bar, not Chromium's
  // native omnibox, so the local URL heuristic must recognize them directly.
  if (s.find("dao://") == 0 || s.find("chrome://") == 0 ||
      s.find("file://") == 0) {
    return true;
  }

  if (LooksLikeLocalFilePath(s)) {
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
