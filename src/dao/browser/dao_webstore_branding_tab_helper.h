// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_DAO_WEBSTORE_BRANDING_TAB_HELPER_H_
#define DAO_BROWSER_DAO_WEBSTORE_BRANDING_TAB_HELPER_H_

#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace dao {

// Injects a branding script into Chrome Web Store pages to replace
// "Add to Chrome" / "Remove from Chrome" with "Add to Dao" / "Remove from Dao".
class DaoWebstoreBrandingTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<DaoWebstoreBrandingTabHelper> {
 public:
  ~DaoWebstoreBrandingTabHelper() override;

  DaoWebstoreBrandingTabHelper(const DaoWebstoreBrandingTabHelper&) = delete;
  DaoWebstoreBrandingTabHelper& operator=(const DaoWebstoreBrandingTabHelper&) =
      delete;

  // content::WebContentsObserver:
  void DocumentOnLoadCompletedInPrimaryMainFrame() override;

 private:
  friend class content::WebContentsUserData<DaoWebstoreBrandingTabHelper>;
  explicit DaoWebstoreBrandingTabHelper(content::WebContents* web_contents);

  void MaybeInjectBrandingScript();

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace dao

#endif  // DAO_BROWSER_DAO_WEBSTORE_BRANDING_TAB_HELPER_H_
