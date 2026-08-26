// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_browser_target_policy.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "content/public/browser/web_contents.h"
#include "url/gurl.h"

namespace dao {
namespace {

base::unexpected<DaoToolError> TargetGone() {
  return base::unexpected(MakeDaoToolError(
      DaoToolErrorCode::kTargetGone,
      "The selected MCP browser target is no longer available."));
}

base::unexpected<DaoToolError> TargetForbidden() {
  return base::unexpected(MakeDaoToolError(
      DaoToolErrorCode::kTargetForbidden,
      "The selected browser target is not eligible for MCP automation."));
}

}  // namespace

base::expected<void, DaoToolError> ValidateExternalTargetUrl(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() && url.spec() != "about:blank") {
    return TargetForbidden();
  }
  return base::ok();
}

base::expected<void, DaoToolError> ValidateExternalTarget(
    Browser* browser,
    Profile* profile,
    content::WebContents* contents,
    bool allow_uncommitted_url) {
  if (!browser || !profile || !contents || browser->profile() != profile ||
      contents->GetBrowserContext() != profile) {
    return TargetGone();
  }

  // An explicitly different tab owner is a stale session, even if both
  // windows share a profile. Non-tab WebContents are classified by the policy
  // below so internal surfaces such as the Agent WebUI remain forbidden.
  Browser* target_owner = chrome::FindBrowserWithTab(contents);
  if (target_owner && target_owner != browser) {
    return TargetGone();
  }

  if (!browser->is_type_normal() || profile->IsOffTheRecord() ||
      profile->IsGuestSession()) {
    return TargetForbidden();
  }
  if (!allow_uncommitted_url || !contents->GetLastCommittedURL().is_empty()) {
    if (auto url_result =
            ValidateExternalTargetUrl(contents->GetLastCommittedURL());
        !url_result.has_value()) {
      return url_result;
    }
  }

  if (target_owner != browser) {
    return TargetGone();
  }
  return base::ok();
}

}  // namespace dao
