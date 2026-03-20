// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/dao_auto_pip_visibility_helper.h"

#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/media_session.h"

namespace dao {

DaoAutoPipVisibilityHelper::DaoAutoPipVisibilityHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<DaoAutoPipVisibilityHelper>(*web_contents) {}

DaoAutoPipVisibilityHelper::~DaoAutoPipVisibilityHelper() = default;

void DaoAutoPipVisibilityHelper::OnVisibilityChanged(
    content::Visibility visibility) {
  if (visibility == content::Visibility::HIDDEN ||
      visibility == content::Visibility::OCCLUDED) {
    // Only trigger if this is the active tab (tab switch is handled by
    // AutoPictureInPictureTabHelper already).
    Browser* browser = chrome::FindBrowserWithTab(web_contents());
    if (!browser) {
      return;
    }
    int active_index = browser->tab_strip_model()->active_index();
    int our_index =
        browser->tab_strip_model()->GetIndexOfWebContents(web_contents());
    if (active_index != our_index) {
      return;  // Not the active tab, skip (tab switch handled elsewhere).
    }

    // Check if already in PiP.
    if (triggered_pip_) {
      return;
    }
    if (PictureInPictureWindowManager::GetInstance()->GetWebContents()) {
      return;
    }

    // Check if audio is playing.
    if (!web_contents()->IsCurrentlyAudible()) {
      return;
    }

    // Trigger PiP via MediaSession.
    content::MediaSession* session =
        content::MediaSession::GetIfExists(web_contents());
    if (!session) {
      return;
    }

    session->EnterPictureInPicture();
    triggered_pip_ = true;
  } else if (visibility == content::Visibility::VISIBLE) {
    if (!triggered_pip_) {
      return;
    }
    triggered_pip_ = false;
    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoAutoPipVisibilityHelper);

}  // namespace dao
