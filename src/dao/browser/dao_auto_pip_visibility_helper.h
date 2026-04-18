// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_DAO_AUTO_PIP_VISIBILITY_HELPER_H_
#define DAO_BROWSER_DAO_AUTO_PIP_VISIBILITY_HELPER_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace dao {

// Watches for window visibility changes (minimize, hide) and triggers
// auto Picture-in-Picture for the active tab's playing video.
// Complements Chromium's AutoPictureInPictureTabHelper which only handles
// tab switching.
class DaoAutoPipVisibilityHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<DaoAutoPipVisibilityHelper> {
 public:
  ~DaoAutoPipVisibilityHelper() override;

  // content::WebContentsObserver:
  void OnVisibilityChanged(content::Visibility visibility) override;

 private:
  friend class content::WebContentsUserData<DaoAutoPipVisibilityHelper>;
  explicit DaoAutoPipVisibilityHelper(content::WebContents* web_contents);

  void OnDocumentPipResult(bool success);

  bool triggered_pip_ = false;

  base::WeakPtrFactory<DaoAutoPipVisibilityHelper> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace dao

#endif  // DAO_BROWSER_DAO_AUTO_PIP_VISIBILITY_HELPER_H_
