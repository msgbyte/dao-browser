// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_ITEM_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_ITEM_VIEW_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "ui/gfx/animation/throb_animation.h"
#include "ui/views/controls/button/button.h"

class Browser;

namespace content {
class WebContents;
}

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace dao {

class DaoAudioButton;

class DaoTabItemView : public views::Button {
  METADATA_HEADER(DaoTabItemView, views::Button)

 public:
  DaoTabItemView(Browser* browser,
                 content::WebContents* contents,
                 int model_index,
                 bool is_active,
                 base::RepeatingClosure on_click,
                 base::RepeatingClosure on_close);
  DaoTabItemView(const DaoTabItemView&) = delete;
  DaoTabItemView& operator=(const DaoTabItemView&) = delete;
  ~DaoTabItemView() override;

  int model_index() const { return model_index_; }

  // Update tab display state (called from TabChangedAt).
  void UpdateAudioState(content::WebContents* contents);
  void UpdateTab(content::WebContents* contents);
  void UpdateAgentLockState(content::WebContents* contents);

 protected:
  // views::Button:
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnMouseEntered(const ui::MouseEvent& event) override;
  void OnMouseExited(const ui::MouseEvent& event) override;
  void OnMouseMoved(const ui::MouseEvent& event) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;
  void OnMouseReleased(const ui::MouseEvent& event) override;

  // gfx::AnimationDelegate (inherited via views::View):
  void AnimationProgressed(const gfx::Animation* animation) override;

 private:
  bool IsPointInCloseButton(const gfx::Point& point) const;
  void UpdateFavicon(content::WebContents* contents);
  void OnCloseClicked();
  void OnAudioButtonClicked();

  raw_ptr<Browser> browser_;
  int model_index_;
  raw_ptr<views::ImageView> favicon_ = nullptr;
  raw_ptr<DaoAudioButton> audio_button_ = nullptr;
  raw_ptr<views::Label> title_label_ = nullptr;
  raw_ptr<views::View> close_button_ = nullptr;
  base::RepeatingClosure close_callback_;
  bool is_audible_ = false;
  bool is_muted_ = false;
  bool close_button_pressed_ = false;
  bool is_agent_locked_ = false;
  gfx::ThrobAnimation agent_lock_throb_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_SIDEBAR_DAO_TAB_ITEM_VIEW_H_
