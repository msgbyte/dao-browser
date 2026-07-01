// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_SITE_CENTER_POPUP_H_
#define DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_SITE_CENTER_POPUP_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/native_theme/native_theme.h"
#include "ui/native_theme/native_theme_observer.h"
#include "ui/views/view.h"

class Browser;

namespace views {
class ImageView;
class Label;
}  // namespace views

namespace dao {

class DaoControlCenterExtensionsSection;

// Mini Dao-only page-scoped site center. It anchors to the site-center icon
// inside the Little Dao URL pill and intentionally omits the normal-window
// Mini Dao extraction action.
class DaoMiniDaoSiteCenterPopup : public views::View,
                                  public TabStripModelObserver,
                                  public content::WebContentsObserver,
                                  public ui::NativeThemeObserver {
  METADATA_HEADER(DaoMiniDaoSiteCenterPopup, views::View)

 public:
  DaoMiniDaoSiteCenterPopup(
      Browser* browser,
      base::RepeatingCallback<views::View*()> anchor_view_callback);
  DaoMiniDaoSiteCenterPopup(const DaoMiniDaoSiteCenterPopup&) = delete;
  DaoMiniDaoSiteCenterPopup& operator=(
      const DaoMiniDaoSiteCenterPopup&) = delete;
  ~DaoMiniDaoSiteCenterPopup() override;

  void ShowAt(const gfx::Point& anchor_bottom_right);
  void Hide();
  void ShowMainPanel();
  void ShowQrPanel();

  Browser* browser() const { return browser_; }
  views::View* GetAnchorViewForExtensions() const;

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

  void ApplyTheme();
  void BuildMainPanel();
  void BuildQrPanel();
  void RefreshSiteHeader();
  void OnPageInfoClicked();
  void OnShareClicked();
  void OnQrClicked();
  void OnMoreClicked();
  void OnBackClicked();
  std::string GetActiveUrlSpec() const;

  raw_ptr<Browser> browser_;
  base::RepeatingCallback<views::View*()> anchor_view_callback_;
  gfx::Point anchor_;
  raw_ptr<views::View> card_ = nullptr;
  raw_ptr<views::View> main_panel_ = nullptr;
  raw_ptr<views::View> qr_panel_ = nullptr;
  raw_ptr<views::Label> domain_label_ = nullptr;
  raw_ptr<views::Label> status_label_ = nullptr;
  raw_ptr<views::ImageView> qr_image_ = nullptr;
  raw_ptr<views::Label> qr_url_label_ = nullptr;
  raw_ptr<DaoControlCenterExtensionsSection> extensions_section_ = nullptr;
  raw_ptr<views::View> separator_ = nullptr;

  base::ScopedObservation<ui::NativeTheme, ui::NativeThemeObserver>
      native_theme_observation_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_LITTLE_DAO_DAO_MINI_DAO_SITE_CENTER_POPUP_H_
