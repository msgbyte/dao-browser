// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_
#define DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/l10n/l10n_util.h"

namespace dao {

inline bool CopyTabUrl(Browser* browser, content::WebContents* target) {
  BrowserView* browser_view =
      browser ? BrowserView::GetBrowserViewForBrowser(browser) : nullptr;
  if (!target || target->IsBeingDestroyed() ||
      !target->GetVisibleURL().is_valid() ||
      target->GetVisibleURL().is_empty()) {
    if (browser_view && browser_view->dao_toast()) {
      browser_view->dao_toast()->ShowToast(
          l10n_util::GetStringUTF16(IDS_DAO_COPY_CURRENT_LINK_FAILED_TOAST));
      browser_view->InvalidateLayout();
    }
    return false;
  }

  {
    ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
    writer.WriteText(base::UTF8ToUTF16(target->GetVisibleURL().spec()));
  }
  if (browser_view && browser_view->dao_toast()) {
    browser_view->dao_toast()->ShowToast(
        l10n_util::GetStringUTF16(IDS_DAO_COPY_CURRENT_LINK_COPIED_TOAST));
    browser_view->InvalidateLayout();
  }
  return true;
}

inline content::WebContents* DuplicateTabAt(Browser* browser, int index) {
  if (!browser || !chrome::CanDuplicateTabAt(browser, index)) {
    return nullptr;
  }

  TabStripModel* model = browser->tab_strip_model();
  const bool source_was_pinned = model->IsTabPinned(index);
  content::WebContents* duplicate = chrome::DuplicateTabAt(browser, index);
  if (!duplicate || !source_was_pinned) {
    return duplicate;
  }

  const int duplicate_index = model->GetIndexOfWebContents(duplicate);
  if (duplicate_index != TabStripModel::kNoTab &&
      model->IsTabPinned(duplicate_index)) {
    model->SetTabPinned(duplicate_index, false);
  }
  return duplicate;
}

inline bool DuplicateActiveTab(Browser* browser) {
  if (!browser) {
    return false;
  }
  return DuplicateTabAt(browser, browser->tab_strip_model()->active_index()) !=
         nullptr;
}

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_TAB_COMMANDS_H_
