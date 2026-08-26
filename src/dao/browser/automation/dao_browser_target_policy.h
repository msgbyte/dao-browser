// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_BROWSER_TARGET_POLICY_H_
#define DAO_BROWSER_AUTOMATION_DAO_BROWSER_TARGET_POLICY_H_

#include "base/types/expected.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

class Browser;
class Profile;
class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace dao {

// Validates the exact browser target selected by an external automation
// session. This policy is intentionally separate from the Dao Agent target
// policy.
base::expected<void, DaoToolError> ValidateExternalTargetUrl(const GURL& url);

base::expected<void, DaoToolError> ValidateExternalTarget(
    Browser* browser,
    Profile* profile,
    content::WebContents* contents,
    bool allow_uncommitted_url = false);

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_BROWSER_TARGET_POLICY_H_
