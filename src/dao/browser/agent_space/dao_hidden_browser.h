// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_SPACE_DAO_HIDDEN_BROWSER_H_
#define DAO_BROWSER_AGENT_SPACE_DAO_HIDDEN_BROWSER_H_

#include "base/files/file_path.h"

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

// Agent Space task windows are real browsers that stay off screen. Chromium's
// window finders and navigator consult this hook so user-facing flows never
// pick, show or reuse a hidden task window. Implemented in Chromium's
// browser_collection.cc; the Agent Space service installs the predicate.
using DaoHiddenBrowserPredicate = bool (*)(BrowserWindowInterface* browser);
void SetDaoHiddenBrowserPredicate(DaoHiddenBrowserPredicate predicate);
bool IsDaoHiddenBrowser(BrowserWindowInterface* browser);

// Browser UI that only the user can answer and that nobody would see in a
// hidden task window. Chromium cancels it there and reports it instead, so
// the Agent Space can hand the task to the user.
enum class DaoHiddenPageNeed {
  kFileChooser,
  kDeviceChooser,
};
using DaoHiddenPageNeedHandler = void (*)(content::WebContents* contents,
                                          DaoHiddenPageNeed need);
void SetDaoHiddenPageNeedHandler(DaoHiddenPageNeedHandler handler);
// Returns true, after reporting `need`, when `contents` is a tab in a hidden
// task window. Callers then cancel their UI.
bool NotifyIfDaoHiddenPageNeedsUser(content::WebContents* contents,
                                    DaoHiddenPageNeed need);

// Downloads from a hidden task window are saved without a prompt into the
// folder this returns; an empty path keeps Chromium's behavior.
using DaoHiddenDownloadDirectoryProvider =
    base::FilePath (*)(content::WebContents* contents);
void SetDaoHiddenDownloadDirectoryProvider(
    DaoHiddenDownloadDirectoryProvider provider);
base::FilePath GetDaoHiddenDownloadDirectory(content::WebContents* contents);

#endif  // DAO_BROWSER_AGENT_SPACE_DAO_HIDDEN_BROWSER_H_
