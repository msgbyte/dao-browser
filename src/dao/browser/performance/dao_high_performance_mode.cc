// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/performance/dao_high_performance_mode.h"

#include <utility>

#include "content/browser/renderer_host/render_view_host_impl.h"
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
  return state && (state->enabled_ || state->site_enabled_);
}

void DaoHighPerformanceMode::SetEnabled(content::WebContents* contents,
                                        bool enabled) {
  contents = contents->GetOutermostWebContents();
  auto* state = GetOrCreateForWebContents(contents);
  const bool was_enabled = IsEnabled(contents);
  state->enabled_ = enabled;
  // Manual controls can turn the mode off until the next site navigation.
  state->site_enabled_ = false;
  if (was_enabled != IsEnabled(contents)) {
    Refresh(contents);
  }
}

bool DaoHighPerformanceMode::SetSiteEnabled(content::WebContents* contents,
                                            bool enabled) {
  auto* state = GetOrCreateForWebContents(contents);
  const bool was_enabled = IsEnabled(contents);
  const bool previous_site_enabled =
      std::exchange(state->site_enabled_, enabled);
  if (was_enabled != IsEnabled(contents)) {
    Refresh(contents);
  }
  return previous_site_enabled;
}

void DaoHighPerformanceMode::PrepareForPageActivation(
    content::RenderFrameHost* main_frame) {
  auto* contents = static_cast<content::WebContentsImpl*>(
      content::WebContents::FromRenderFrameHost(main_frame));
  const auto visibility = contents->GetPageVisibilityState();
  // BFCache removes the destination from its entries before ReadyToCommit.
  main_frame->ForEachRenderFrameHost([&](content::RenderFrameHost* frame) {
    if (&frame->GetPage() == &main_frame->GetPage()) {
      static_cast<content::RenderViewHostImpl*>(frame->GetRenderViewHost())
          ->SetFrameTreeVisibility(visibility);
    }
  });
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
