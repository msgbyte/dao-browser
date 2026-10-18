// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/performance/dao_high_performance_mode.h"

#include <utility>

#include "content/browser/web_contents/web_contents_impl.h"

namespace dao {

WEB_CONTENTS_USER_DATA_KEY_IMPL(DaoHighPerformanceMode);

DaoHighPerformanceMode::DaoHighPerformanceMode(content::WebContents* contents)
    : content::WebContentsUserData<DaoHighPerformanceMode>(*contents) {}

DaoHighPerformanceMode::~DaoHighPerformanceMode() = default;

bool DaoHighPerformanceMode::IsEnabled(const content::WebContents* contents) {
  const auto* root = contents ? const_cast<content::WebContents*>(contents)
                                    ->GetOutermostWebContents()
                              : nullptr;
  const auto* state = root ? FromWebContents(root) : nullptr;
  return state && state->enabled_;
}

void DaoHighPerformanceMode::SetEnabled(content::WebContents* contents,
                                        bool enabled) {
  contents = contents->GetOutermostWebContents();
  auto* state = GetOrCreateForWebContents(contents);
  if (state->enabled_ == enabled) {
    return;
  }
  state->enabled_ = enabled;
  Refresh(contents);
}

void DaoHighPerformanceMode::Refresh(content::WebContents* contents) {
  if (contents->IsBeingDestroyed()) {
    return;
  }
  static_cast<content::WebContentsImpl*>(contents)
      ->OnHighPerformanceModeChanged();
  if (auto* state = FromWebContents(contents)) {
    state->observers_.Notify(IsEnabled(contents));
  }
  for (auto* inner : contents->GetInnerWebContents()) {
    Refresh(inner);
  }
}

base::CallbackListSubscription DaoHighPerformanceMode::AddObserver(
    content::WebContents* contents,
    base::RepeatingCallback<void(bool)> callback) {
  return GetOrCreateForWebContents(contents)->observers_.Add(
      std::move(callback));
}

}  // namespace dao
