// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_browser_target_policy.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
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
    BrowserWindowInterface* browser,
    Profile* profile,
    content::WebContents* contents,
    bool allow_uncommitted_url) {
  if (!browser || !profile || !contents || browser->GetProfile() != profile ||
      contents->GetBrowserContext() != profile) {
    return TargetGone();
  }

  // An explicitly different tab owner is a stale session, even if both
  // windows share a profile. Non-tab WebContents are classified by the policy
  // below so internal surfaces such as the Agent WebUI remain forbidden.
  BrowserWindowInterface* target_owner =
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(contents);
  if (target_owner && target_owner != browser) {
    return TargetGone();
  }

  if (!(browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) ||
      profile->IsOffTheRecord() || profile->IsGuestSession()) {
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
