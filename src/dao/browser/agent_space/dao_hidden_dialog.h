// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_SPACE_DAO_HIDDEN_DIALOG_H_
#define DAO_BROWSER_AGENT_SPACE_DAO_HIDDEN_DIALOG_H_

#include <string>

#include "content/public/common/javascript_dialog_type.h"

namespace content {
class WebContents;
}  // namespace content

// Pages in hidden Agent task windows have nobody to answer their JavaScript
// dialogs. Chromium's tab-modal dialog manager keeps their confirm and prompt
// dialogs pending and reports them here, so the agent can answer them or the
// user can when they open the window. Implemented in
// javascript_tab_modal_dialog_manager_delegate_desktop.cc; the Agent Space
// service installs the handlers.
using DaoHiddenDialogOpenedHandler =
    void (*)(content::WebContents* contents,
             content::JavaScriptDialogType dialog_type,
             const std::u16string& message_text,
             const std::u16string& default_prompt_text);
using DaoHiddenDialogClosedHandler = void (*)(content::WebContents* contents);
void SetDaoHiddenDialogHandlers(DaoHiddenDialogOpenedHandler opened,
                                DaoHiddenDialogClosedHandler closed);

#endif  // DAO_BROWSER_AGENT_SPACE_DAO_HIDDEN_DIALOG_H_
