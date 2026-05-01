// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_agent_sidebar_view.h"

#include <algorithm>

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/url_constants.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/controls/webview/web_contents_set_background_color.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "url/gurl.h"

namespace dao {

BEGIN_METADATA(DaoAgentSidebarView)
END_METADATA

DaoAgentSidebarView::DaoAgentSidebarView(Browser* browser)
    : browser_(browser) {
  // Resize handle on the left edge (drag to resize).
  resize_area_ = AddChildView(std::make_unique<views::ResizeArea>(this));

  // WebView is created here. The chrome://agent WebUI is preloaded on an
  // idle delay below so the first Toggle() is instantaneous — if the user
  // opens the panel before the timer fires, Toggle() will call EnsureLoaded()
  // directly (it is idempotent).
  web_view_ = AddChildView(
      std::make_unique<views::WebView>(browser->profile()));

  native_theme_observation_.Observe(ui::NativeTheme::GetInstanceForNativeUi());
  ApplyTheme();

  // Warm up the WebUI after the browser window has settled. The panel stays
  // SetVisible(false) / width 0, so the user sees nothing — but when they
  // click the toggle, the WebContents has already parsed/executed its JS and
  // built its DOM, and only a single paint frame remains.
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(&DaoAgentSidebarView::EnsureLoaded,
                     weak_factory_.GetWeakPtr()),
      base::Seconds(3));
}

void DaoAgentSidebarView::ApplyTheme() {
  const SkColor bg = dao::SidebarBackground();
  SetBackground(views::CreateSolidBackground(bg));

  if (web_view_) {
    if (content::WebContents* web_contents = web_view_->GetWebContents()) {
      web_contents->SetPageBaseBackgroundColor(bg);
      if (content::RenderWidgetHostView* host_view =
              web_contents->GetRenderWidgetHostView()) {
        host_view->SetBackgroundColor(bg);
      }
      // Refresh the UserData cache installed in EnsureLoaded() so future
      // RenderFrameCreated events re-apply the current theme's color instead
      // of the stale startup color. Only after first load — do not install
      // the UserData here before EnsureLoaded() has run.
      if (loaded_) {
        views::WebContentsSetBackgroundColor::CreateForWebContentsWithColor(
            web_contents, bg);
      }
    }
  }
}

void DaoAgentSidebarView::OnNativeThemeUpdated(
    ui::NativeTheme* observed_theme) {
  ApplyTheme();
  SchedulePaint();
}

DaoAgentSidebarView::~DaoAgentSidebarView() {
  if (web_view_ && web_view_->GetWebContents()) {
    web_view_->GetWebContents()->SetDelegate(nullptr);
  }
}

void DaoAgentSidebarView::EnsureLoaded() {
  if (loaded_) {
    return;
  }

  loaded_ = true;

  if (content::WebContents* web_contents = web_view_->GetWebContents()) {
    web_contents->SetPageBaseBackgroundColor(dao::SidebarBackground());
    views::WebContentsSetBackgroundColor::CreateForWebContentsWithColor(
        web_contents, dao::SidebarBackground());
    if (content::RenderWidgetHostView* host_view =
            web_contents->GetRenderWidgetHostView()) {
      host_view->SetBackgroundColor(dao::SidebarBackground());
    }
    web_contents->SetDelegate(this);
  }

  web_view_->LoadInitialURL(
      GURL(std::string(content::kChromeUIScheme) + "://agent"));
}

void DaoAgentSidebarView::ExpandAndSubmitPrompt(
    const std::u16string& prompt) {
  if (prompt.empty()) {
    return;
  }
  pending_prompt_ = prompt;

  if (!expanded_) {
    Toggle();
  }

  // Kick the flush loop on both paths.  When we just expanded, the WebUI may
  // still be loading — TryFlushPendingPrompt's RFH-live check + 100ms retry
  // (60 attempts = 6s) covers the load window.  When we were already open,
  // this dispatches the prompt on the next task.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoAgentSidebarView::TryFlushPendingPrompt,
                     weak_factory_.GetWeakPtr(), /*attempts_left=*/60));
}

void DaoAgentSidebarView::TryFlushPendingPrompt(int attempts_left) {
  if (pending_prompt_.empty() || !web_view_) {
    return;
  }
  content::WebContents* web_contents = web_view_->GetWebContents();
  content::RenderFrameHost* rfh =
      web_contents ? web_contents->GetPrimaryMainFrame() : nullptr;
  if (!rfh || !rfh->IsRenderFrameLive()) {
    if (attempts_left > 0) {
      base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
          FROM_HERE,
          base::BindOnce(&DaoAgentSidebarView::TryFlushPendingPrompt,
                         weak_factory_.GetWeakPtr(), attempts_left - 1),
          base::Milliseconds(100));
    } else {
      pending_prompt_.clear();
    }
    return;
  }

  // JSON-encode the prompt so it survives the string -> JS literal crossing
  // (newlines, quotes, unicode escapes).  base::EscapeJSONString wraps it
  // in double quotes and escapes control characters.
  std::string prompt_json;
  base::EscapeJSONString(base::UTF16ToUTF8(pending_prompt_),
                         /*put_in_quotes=*/true, &prompt_json);

  std::u16string script = base::UTF8ToUTF16(base::StrCat({
      "(function(){",
      "  if (typeof window.__daoExternalSubmit === 'function') {",
      "    window.__daoExternalSubmit(", prompt_json, ");",
      "    return true;",
      "  }",
      "  return false;",
      "})()"}));

  auto weak_self = weak_factory_.GetWeakPtr();
  rfh->ExecuteJavaScript(
      script,
      base::BindOnce(
          [](base::WeakPtr<DaoAgentSidebarView> self, int attempts_left,
             base::Value value) {
            if (!self) {
              return;
            }
            const bool dispatched = value.is_bool() && value.GetBool();
            if (dispatched) {
              self->pending_prompt_.clear();
              return;
            }
            if (attempts_left <= 0) {
              self->pending_prompt_.clear();
              return;
            }
            base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
                FROM_HERE,
                base::BindOnce(&DaoAgentSidebarView::TryFlushPendingPrompt,
                               self, attempts_left - 1),
                base::Milliseconds(100));
          },
          weak_self, attempts_left));
}

bool DaoAgentSidebarView::Toggle() {
  expanded_ = !expanded_;

  if (expanded_) {
    EnsureLoaded();
    SetVisible(true);
  } else {
    SetVisible(false);
  }

  // Single layout pass — web content repaints exactly once.
  PreferredSizeChanged();

  if (expanded_) {
    // Route keyboard focus into the WebView so the agent input can auto-focus
    // after its visibilitychange handler runs. Deferred so the focus lands
    // after layout + visibility propagation.
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE,
        base::BindOnce(
            [](base::WeakPtr<DaoAgentSidebarView> self) {
              if (!self || !self->expanded_ || !self->web_view_) {
                return;
              }
              self->web_view_->RequestFocus();
            },
            weak_factory_.GetWeakPtr()));
  }

  return expanded_;
}

gfx::Size DaoAgentSidebarView::CalculatePreferredSize(
    const views::SizeBounds& available_size) const {
  return gfx::Size(expanded_ ? current_width_ : 0, 0);
}

void DaoAgentSidebarView::Layout(PassKey) {
  // Position the resize area on the left edge.
  if (resize_area_) {
    resize_area_->SetBoundsRect(
        gfx::Rect(0, 0, kResizeAreaWidth, height()));
  }

  // WebView fills the remaining area after the resize handle.
  if (web_view_) {
    web_view_->SetBoundsRect(
        gfx::Rect(kResizeAreaWidth, 0,
                   std::max(0, width() - kResizeAreaWidth), height()));
  }
}

bool DaoAgentSidebarView::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, GetFocusManager());
}

void DaoAgentSidebarView::OnResize(int resize_amount, bool done_resizing) {
  if (!expanded_) {
    return;
  }

  if (!is_resizing_) {
    is_resizing_ = true;
    resize_start_width_ = current_width_;
  }

  // Left-edge drag: dragging left (negative resize_amount) should increase
  // width, dragging right should decrease it.
  int new_width = resize_start_width_ - resize_amount;
  new_width = std::clamp(new_width, kMinWidth, kMaxWidth);
  current_width_ = new_width;

  if (done_resizing) {
    is_resizing_ = false;
    user_width_ = new_width;
  }

  PreferredSizeChanged();
}

}  // namespace dao
