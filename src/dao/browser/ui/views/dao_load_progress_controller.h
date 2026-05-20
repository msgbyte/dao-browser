// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
#define DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"

class TabStripModel;

namespace content {
class WebContents;
}

namespace dao {

class DaoLoadProgressView;

// Owns the plumbing that drives DaoLoadProgressView from the active tab's
// real load events.
class DaoLoadProgressController : public TabStripModelObserver,
                                  public content::WebContentsObserver {
 public:
  DaoLoadProgressController(TabStripModel* tab_strip_model,
                            DaoLoadProgressView* view);
  DaoLoadProgressController(const DaoLoadProgressController&) = delete;
  DaoLoadProgressController& operator=(const DaoLoadProgressController&) =
      delete;
  ~DaoLoadProgressController() override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void LoadProgressChanged(double progress) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void WebContentsDestroyed() override;

 private:
  // Re-point at `new_contents`; resync the view to its current state.
  void AttachToWebContents(content::WebContents* new_contents);

  raw_ptr<TabStripModel> tab_strip_model_;
  raw_ptr<DaoLoadProgressView> view_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_LOAD_PROGRESS_CONTROLLER_H_
