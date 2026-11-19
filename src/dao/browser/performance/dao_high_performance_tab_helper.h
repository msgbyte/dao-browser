// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PERFORMANCE_DAO_HIGH_PERFORMANCE_TAB_HELPER_H_
#define DAO_BROWSER_PERFORMANCE_DAO_HIGH_PERFORMANCE_TAB_HELPER_H_

#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

class GURL;
class Profile;

namespace dao {

// Applies the profile's saved host preference to each committed page.
class DaoHighPerformanceTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<DaoHighPerformanceTabHelper> {
 public:
  ~DaoHighPerformanceTabHelper() override;

  static bool IsAlwaysEnabled(Profile* profile, const GURL& url);
  static void SetAlwaysEnabled(Profile* profile, const GURL& url, bool enabled);

 private:
  friend class content::WebContentsUserData<DaoHighPerformanceTabHelper>;
  explicit DaoHighPerformanceTabHelper(content::WebContents* contents);

  void PrimaryPageChanged(content::Page& page) override;
  void OnPreferenceChanged();
  void ApplySitePreference();

  bool site_preference_enabled_ = false;
  PrefChangeRegistrar pref_change_registrar_;

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace dao

#endif  // DAO_BROWSER_PERFORMANCE_DAO_HIGH_PERFORMANCE_TAB_HELPER_H_
