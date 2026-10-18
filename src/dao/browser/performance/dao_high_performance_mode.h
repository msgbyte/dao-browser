// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PERFORMANCE_DAO_HIGH_PERFORMANCE_MODE_H_
#define DAO_BROWSER_PERFORMANCE_DAO_HIGH_PERFORMANCE_MODE_H_

#include "base/callback_list.h"
#include "content/common/content_export.h"
#include "content/public/browser/web_contents_user_data.h"

namespace dao {

// Tab-scoped state shared by browser UI and Chromium's scheduling policies.
class CONTENT_EXPORT DaoHighPerformanceMode
    : public content::WebContentsUserData<DaoHighPerformanceMode> {
 public:
  ~DaoHighPerformanceMode() override;

  static bool IsEnabled(const content::WebContents* contents);
  static void SetEnabled(content::WebContents* contents, bool enabled);
  // Reapply inherited state after an embedding or tab-mode change.
  static void Refresh(content::WebContents* contents);
  static base::CallbackListSubscription AddObserver(
      content::WebContents* contents,
      base::RepeatingCallback<void(bool)> callback);

 private:
  friend class content::WebContentsUserData<DaoHighPerformanceMode>;
  explicit DaoHighPerformanceMode(content::WebContents* contents);

  bool enabled_ = false;
  base::RepeatingCallbackList<void(bool)> observers_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace dao

#endif  // DAO_BROWSER_PERFORMANCE_DAO_HIGH_PERFORMANCE_MODE_H_
