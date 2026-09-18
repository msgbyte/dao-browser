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
  showing_loading_ui_ = new_contents && new_contents->ShouldShowLoadingUI();

  if (showing_loading_ui_) {
    // Sync the view to the new tab's current progress without animating.
    view_->StartLoading();
    view_->SetTargetProgress(new_contents->GetLoadProgress(),
                             /*animate=*/false);
  } else {
    view_->HideImmediately();
  }
}

void DaoLoadProgressController::OnTabChangedAt(tabs::TabInterface* tab,
                                               int index,
                                               TabChangeType change_type) {
  if (change_type != TabChangeType::kLoadingOnly ||
      index != tab_strip_model_->active_index() || !web_contents()) {
    return;
  }

  // Unlike DidStartLoading/DidStopLoading, this also reports transitions
  // between a background subframe load and a navigation that needs UI.
  const bool show_loading_ui = web_contents()->ShouldShowLoadingUI();
  if (showing_loading_ui_ == show_loading_ui) {
    return;
  }
  showing_loading_ui_ = show_loading_ui;
  if (showing_loading_ui_) {
    view_->StartLoading();
  } else {
    view_->FinishLoading();
  }
}

void DaoLoadProgressController::LoadProgressChanged(double progress) {
  if (showing_loading_ui_) {
    view_->SetTargetProgress(progress, /*animate=*/true);
  }
}

void DaoLoadProgressController::WebContentsDestroyed() {
  showing_loading_ui_ = false;
  view_->HideImmediately();
}

}  // namespace dao
