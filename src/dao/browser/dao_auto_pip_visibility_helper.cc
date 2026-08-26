// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/dao_auto_pip_visibility_helper.h"

#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/picture_in_picture/picture_in_picture_window_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "content/public/browser/media_session.h"
#include "dao/browser/pip/dao_pip_interceptor.h"
#include "url/gurl.h"

namespace dao {

DaoAutoPipVisibilityHelper::DaoAutoPipVisibilityHelper(
    content::WebContents* web_contents)
    : content::WebContentsObserver(web_contents),
      content::WebContentsUserData<DaoAutoPipVisibilityHelper>(*web_contents) {}

DaoAutoPipVisibilityHelper::~DaoAutoPipVisibilityHelper() = default;

bool DaoAutoPipVisibilityHelper::IsAutoPictureInPictureAllowed() const {
  Profile* profile =
      Profile::FromBrowserContext(web_contents()->GetBrowserContext());
  const GURL& url = web_contents()->GetLastCommittedURL();
  return HostContentSettingsMapFactory::GetForProfile(profile)
             ->GetContentSetting(
                 url, url, ContentSettingsType::AUTO_PICTURE_IN_PICTURE) !=
         CONTENT_SETTING_BLOCK;
}

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
    if (!IsAutoPictureInPictureAllowed()) {
      return;
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

    // Intercept bilibili for Document PiP with player area element.
    if (DaoPipInterceptor::ShouldIntercept(web_contents())) {
      auto* pip_interceptor =
          DaoPipInterceptor::FromWebContents(web_contents());
      if (pip_interceptor) {
        pip_interceptor->TriggerDocumentPip(
            base::BindOnce(
                &DaoAutoPipVisibilityHelper::OnDocumentPipResult,
                weak_factory_.GetWeakPtr()));
        triggered_pip_ = true;
        return;
      }
    }

    session->EnterPictureInPicture();
    triggered_pip_ = true;
  } else if (visibility == content::Visibility::VISIBLE) {
    if (!triggered_pip_) {
      return;
    }
    triggered_pip_ = false;

    // Close Document PiP if active on bilibili.
    if (DaoPipInterceptor::ShouldIntercept(web_contents())) {
      auto* pip_interceptor =
          DaoPipInterceptor::FromWebContents(web_contents());
      if (pip_interceptor) {
        pip_interceptor->CloseDocumentPip();
      }
    }

    PictureInPictureWindowManager::GetInstance()->ExitPictureInPicture();
  }
}

void DaoAutoPipVisibilityHelper::OnDocumentPipResult(bool success) {
  if (!success) {
    content::MediaSession* session =
        content::MediaSession::GetIfExists(web_contents());
    if (session) {
      session->EnterPictureInPicture();
    }
  }
}

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoAutoPipVisibilityHelper);

}  // namespace dao
