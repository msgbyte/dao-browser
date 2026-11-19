// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/performance/dao_high_performance_tab_helper.h"

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/performance/dao_high_performance_mode.h"
#include "url/gurl.h"

namespace dao {

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoHighPerformanceTabHelper);

DaoHighPerformanceTabHelper::DaoHighPerformanceTabHelper(
    content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<DaoHighPerformanceTabHelper>(*contents) {
  pref_change_registrar_.Init(
      Profile::FromBrowserContext(contents->GetBrowserContext())->GetPrefs());
  pref_change_registrar_.Add(
      prefs::kDaoHighPerformanceHosts,
      base::BindRepeating(&DaoHighPerformanceTabHelper::OnPreferenceChanged,
                          base::Unretained(this)));
  ApplySitePreference();
}

DaoHighPerformanceTabHelper::~DaoHighPerformanceTabHelper() = default;

bool DaoHighPerformanceTabHelper::IsAlwaysEnabled(Profile* profile,
                                                  const GURL& url) {
  return profile && url.SchemeIsHTTPOrHTTPS() && url.has_host() &&
         profile->GetPrefs()
             ->GetDict(prefs::kDaoHighPerformanceHosts)
             .FindBool(url.host())
             .value_or(false);
}

void DaoHighPerformanceTabHelper::SetAlwaysEnabled(Profile* profile,
                                                   const GURL& url,
                                                   bool enabled) {
  if (!profile || !url.SchemeIsHTTPOrHTTPS() || !url.has_host()) {
    return;
  }
  ScopedDictPrefUpdate update(profile->GetPrefs(),
                              prefs::kDaoHighPerformanceHosts);
  if (enabled) {
    update->Set(url.host(), true);
  } else {
    update->Remove(url.host());
  }
}

void DaoHighPerformanceTabHelper::ReadyToCommitNavigation(
    content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument()) {
    return;
  }
  // Cached pages need the destination state before their pageshow callback.
  const bool previous_site_enabled = DaoHighPerformanceMode::SetSiteEnabled(
      web_contents(), IsAlwaysEnabled(Profile::FromBrowserContext(
                                          web_contents()->GetBrowserContext()),
                                      navigation->GetURL()));
  if (!pending_navigation_id_) {
    previous_site_enabled_ = previous_site_enabled;
  }
  pending_navigation_id_ = navigation->GetNavigationId();
  DaoHighPerformanceMode::PrepareForPageActivation(
      navigation->GetRenderFrameHost());
}

void DaoHighPerformanceTabHelper::DidFinishNavigation(
    content::NavigationHandle* navigation) {
  if (pending_navigation_id_ != navigation->GetNavigationId()) {
    return;
  }
  pending_navigation_id_.reset();
  if (!navigation->HasCommitted()) {
    DaoHighPerformanceMode::SetSiteEnabled(web_contents(),
                                           previous_site_enabled_);
  }
}

void DaoHighPerformanceTabHelper::PrimaryPageChanged(content::Page& page) {
  pending_navigation_id_.reset();
  ApplySitePreference();
}

void DaoHighPerformanceTabHelper::OnPreferenceChanged() {
  const bool enabled = IsAlwaysEnabled(
      Profile::FromBrowserContext(web_contents()->GetBrowserContext()),
      web_contents()->GetLastCommittedURL());
  // Changes for other hosts must not reset a manual override on this page.
  if (enabled != site_preference_enabled_) {
    ApplySitePreference();
  }
}

void DaoHighPerformanceTabHelper::ApplySitePreference() {
  site_preference_enabled_ = IsAlwaysEnabled(
      Profile::FromBrowserContext(web_contents()->GetBrowserContext()),
      web_contents()->GetLastCommittedURL());
  DaoHighPerformanceMode::SetSiteEnabled(web_contents(),
                                         site_preference_enabled_);
}

}  // namespace dao
