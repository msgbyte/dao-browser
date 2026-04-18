// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PIP_DAO_PIP_INTERCEPTOR_H_
#define DAO_BROWSER_PIP_DAO_PIP_INTERCEPTOR_H_

#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"

namespace dao {

// Intercepts Picture-in-Picture requests on configured sites (see
// pip_site_rules.json) and redirects them to Document PiP with a specific
// DOM element instead of just the video.
class DaoPipInterceptor
    : public content::WebContentsObserver,
      public content::WebContentsUserData<DaoPipInterceptor> {
 public:
  ~DaoPipInterceptor() override;

  // Returns true if the WebContents URL matches a configured PiP site rule.
  static bool ShouldIntercept(content::WebContents* web_contents);

  // Trigger Document PiP via JS injection with user activation.
  // Used by auto PiP paths (minimize, tab switch).
  // Calls |callback| with true on success, false on failure.
  void TriggerDocumentPip(base::OnceCallback<void(bool)> callback);

  // Close any active Document PiP window via JS.
  void CloseDocumentPip();

  // content::WebContentsObserver:
  void DocumentOnLoadCompletedInPrimaryMainFrame() override;

 private:
  friend class content::WebContentsUserData<DaoPipInterceptor>;
  explicit DaoPipInterceptor(content::WebContents* web_contents);

  void MaybeInjectPipOverride();
  void OnTriggerResult(base::OnceCallback<void(bool)> callback,
                       base::Value result);

  bool document_pip_active_ = false;

  base::WeakPtrFactory<DaoPipInterceptor> weak_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace dao

#endif  // DAO_BROWSER_PIP_DAO_PIP_INTERCEPTOR_H_
