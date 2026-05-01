// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_AGENT_SIDEBAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_AGENT_SIDEBAR_VIEW_H_

#include <string>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/controls/resize_area.h"
#include "ui/views/controls/resize_area_delegate.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class WebView;
}

namespace dao {

class DaoAgentSidebarView : public views::View,
                            public content::WebContentsDelegate,
                            public views::ResizeAreaDelegate,
                            public ui::NativeThemeObserver {
  METADATA_HEADER(DaoAgentSidebarView, views::View)

 public:
  static constexpr int kDefaultWidth = 360;
  static constexpr int kMinWidth = 200;
  static constexpr int kMaxWidth = 600;
  static constexpr int kResizeAreaWidth = 6;

  explicit DaoAgentSidebarView(Browser* browser);
  ~DaoAgentSidebarView() override;

  // Toggle visibility; returns new expanded state.
  bool Toggle();

  bool is_expanded() const { return expanded_; }

  // Expands the sidebar (if not already) and submits `prompt` as the first
  // turn of a fresh chat session.  Called from the command bar when the
  // user picks the "Ask AI" row.  The actual submission is deferred until
  // the agent WebUI's __daoExternalSubmit hook is installed — a polling
  // task keeps retrying the JS injection until it succeeds or a deadline
  // elapses.
  //
  // `include_page_context` controls whether the active tab's content +
  // selection are spliced into the first turn.  Cmd+L keeps the page
  // context (the user is asking about the page they're on); Cmd+T does not
  // (the user is opening a fresh tab to ask a standalone question).
  void ExpandAndSubmitPrompt(const std::u16string& prompt,
                             bool include_page_context);

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available_size) const override;
  void Layout(PassKey) override;

  // content::WebContentsDelegate:
  bool HandleKeyboardEvent(
      content::WebContents* source,
      const input::NativeWebKeyboardEvent& event) override;

  // views::ResizeAreaDelegate:
  void OnResize(int resize_amount, bool done_resizing) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

 private:
  void EnsureLoaded();
  void ApplyTheme();

  // Polls the agent WebUI trying to invoke window.__daoExternalSubmit; kept
  // alive across retries until the hook is installed or kSubmitTimeoutMs
  // elapses.
  void TryFlushPendingPrompt(int attempts_left);

  raw_ptr<Browser> browser_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::ResizeArea> resize_area_ = nullptr;

  bool loaded_ = false;
  bool expanded_ = false;
  bool is_resizing_ = false;

  // Pending prompt queued by ExpandAndSubmitPrompt while the WebUI hook
  // (window.__daoExternalSubmit) is still loading.  Cleared when the
  // submission is dispatched or the retry deadline is reached.
  std::u16string pending_prompt_;
  // Whether the pending prompt should include current-page / selection
  // context when it lands.  Captured at queue time alongside the prompt
  // because the dispatch is deferred and the caller's intent (Cmd+L vs
  // Cmd+T) needs to survive the wait.
  bool pending_include_page_context_ = true;

  int current_width_ = kDefaultWidth;
  int user_width_ = kDefaultWidth;
  int resize_start_width_ = kDefaultWidth;

  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};

  base::WeakPtrFactory<DaoAgentSidebarView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_AGENT_SIDEBAR_VIEW_H_
