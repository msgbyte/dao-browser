// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_POPUP_H_
#define DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_POPUP_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

class Browser;

namespace gfx {
class Rect;
}

namespace dao {

class DaoControlCenterExtensionsSection;
class DaoControlCenterUtilitySection;
class DaoControlCenterQrView;
class DaoControlCenterMoreMenu;

// A floating popup anchored below the control center button.
// Contains extensions grid, utility buttons (share, QR, lock, more).
// Has a transparent overlay that closes the popup when clicked.
class DaoControlCenterPopup : public views::View,
                               public TabStripModelObserver,
                               public content::WebContentsObserver,
                               public ui::NativeThemeObserver {
  METADATA_HEADER(DaoControlCenterPopup, views::View)

 public:
  explicit DaoControlCenterPopup(Browser* browser);
  DaoControlCenterPopup(const DaoControlCenterPopup&) = delete;
  DaoControlCenterPopup& operator=(const DaoControlCenterPopup&) = delete;
  ~DaoControlCenterPopup() override;

  void ShowAt(const gfx::Point& anchor_bottom_right);
  void Hide();

  // Show the QR code sub-panel.
  void ShowQrView();
  // Show the more-menu sub-panel.
  void ShowMoreMenu();
  // Return to the main panel from a sub-panel.
  void ShowMainPanel();
  // Show the native share picker for the active page.
  void ShareCurrentPage(const gfx::Rect& anchor_rect);

  Browser* browser() const { return browser_; }

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

  // content::WebContentsObserver:
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;

  // ui::NativeThemeObserver:
  void OnNativeThemeUpdated(ui::NativeTheme* observed_theme) override;

  // views::View:
  void Layout(PassKey) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  static constexpr int kCardWidth = 320;
  static constexpr int kCardCornerRadius = 12;
  static constexpr int kCardPadding = 12;

  raw_ptr<Browser> browser_;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<DaoControlCenterExtensionsSection> extensions_section_ = nullptr;
  raw_ptr<DaoControlCenterUtilitySection> utility_section_ = nullptr;
  raw_ptr<DaoControlCenterQrView> qr_view_ = nullptr;
  raw_ptr<DaoControlCenterMoreMenu> more_menu_ = nullptr;

  // Anchor point: bottom-right of the button, in parent (BrowserView) coords.
  gfx::Point anchor_;

  void ApplyTheme();

  raw_ptr<views::View> separator_ = nullptr;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};

  base::WeakPtrFactory<DaoControlCenterPopup> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_CONTROL_CENTER_POPUP_H_
