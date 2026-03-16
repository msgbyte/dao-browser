// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_
#define DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class Textfield;
}

namespace dao {

class DaoCommandBarView : public views::View,
                          public views::TextfieldController {
  METADATA_HEADER(DaoCommandBarView, views::View)

 public:
  explicit DaoCommandBarView(Browser* browser);
  DaoCommandBarView(const DaoCommandBarView&) = delete;
  DaoCommandBarView& operator=(const DaoCommandBarView&) = delete;
  ~DaoCommandBarView() override;

  // Show for Cmd+L: pre-fills current URL, Esc just hides.
  void Show();
  // Show for Cmd+T / new tab button: empty textfield, highlights the
  // new-tab button.  Esc dismisses without creating a tab.  Enter creates
  // a new tab and navigates.
  void ShowForNewTab();
  void Hide();

  // views::View:
  void Layout(PassKey) override;
  void OnPaint(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

  // views::TextfieldController:
  bool HandleKeyEvent(views::Textfield* sender,
                      const ui::KeyEvent& key_event) override;

 private:
  void Navigate(const std::u16string& text);
  static bool LooksLikeURL(const std::u16string& text);
  void DeferredRequestFocus();

  void CancelNewTab();
  void SetNewTabButtonHighlight(bool highlighted);
  void SetWebContentEventProcessing(bool enabled);

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> card_container_ = nullptr;
  raw_ptr<views::Textfield> textfield_ = nullptr;

  // When true, we are in "pre-new-tab" mode: no tab has been created yet.
  bool is_new_tab_mode_ = false;

  base::WeakPtrFactory<DaoCommandBarView> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_COMMAND_BAR_VIEW_H_
