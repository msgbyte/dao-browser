// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_load_progress_controller.h"

#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_load_progress_view.h"

namespace dao {

DaoLoadProgressController::DaoLoadProgressController(
    TabStripModel* tab_strip_model,
    DaoLoadProgressView* view)
    : tab_strip_model_(tab_strip_model), view_(view) {
  tab_strip_model_->AddObserver(this);
  AttachToWebContents(tab_strip_model_->GetActiveWebContents());
}

DaoLoadProgressController::~DaoLoadProgressController() {
  if (tab_strip_model_) {
    tab_strip_model_->RemoveObserver(this);
  }
  // WebContentsObserver auto-detaches in its destructor.
}

void DaoLoadProgressController::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (selection.active_tab_changed()) {
    AttachToWebContents(selection.new_contents);
  }
}

void DaoLoadProgressController::AttachToWebContents(
    content::WebContents* new_contents) {
  // Detach from previous (no-op if same).
  Observe(new_contents);

  if (!new_contents) {
    view_->HideImmediately();
    return;
  }

  if (new_contents->IsLoading()) {
    // Sync the view to the new tab's current progress without animating.
    view_->StartLoading();
    view_->SetTargetProgress(new_contents->GetLoadProgress(),
                             /*animate=*/false);
  } else {
    view_->HideImmediately();
  }
}

void DaoLoadProgressController::LoadProgressChanged(double progress) {
  view_->SetTargetProgress(progress, /*animate=*/true);
}

void DaoLoadProgressController::DidStartLoading() {
  view_->StartLoading();
}

void DaoLoadProgressController::DidStopLoading() {
  view_->FinishLoading();
}

void DaoLoadProgressController::WebContentsDestroyed() {
  view_->HideImmediately();
}

}  // namespace dao
