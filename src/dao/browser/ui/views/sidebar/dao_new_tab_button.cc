// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_new_tab_button.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

BEGIN_METADATA(DaoNewTabButton)
END_METADATA

DaoNewTabButton::DaoNewTabButton(Browser* browser)
    : Button(base::BindRepeating(&DaoNewTabButton::OnNewTabClicked,
                                 base::Unretained(this))),
      browser_(browser) {
  SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::TLBR(0, 6, 0, 10), 6));

  auto* plus_label = AddChildView(std::make_unique<views::Label>(u"+"));
  plus_label->SetFontList(gfx::FontList(
      {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 13,
      gfx::Font::Weight::MEDIUM));
  plus_label->SetEnabledColor(dao::kTextSecondary);
  plus_label->SetSubpixelRenderingEnabled(false);

  auto* text_label =
      AddChildView(std::make_unique<views::Label>(u"New Tab"));
  text_label->SetFontList(gfx::FontList(
      {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 12,
      gfx::Font::Weight::NORMAL));
  text_label->SetEnabledColor(dao::kTextSecondary);
  text_label->SetSubpixelRenderingEnabled(false);

  SetInstallFocusRingOnFocus(false);
  SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 6)));

  SetPreferredSize(gfx::Size(0, 40));
  SetAccessibleName(u"New Tab");
}

DaoNewTabButton::~DaoNewTabButton() = default;

void DaoNewTabButton::SetHighlighted(bool highlighted) {
  if (highlighted_ == highlighted) {
    return;
  }
  highlighted_ = highlighted;
  UpdateBackground();
}

void DaoNewTabButton::OnMouseEntered(const ui::MouseEvent& event) {
  Button::OnMouseEntered(event);
  hovered_ = true;
  UpdateBackground();
}

void DaoNewTabButton::OnMouseExited(const ui::MouseEvent& event) {
  Button::OnMouseExited(event);
  hovered_ = false;
  UpdateBackground();
}

void DaoNewTabButton::UpdateBackground() {
  constexpr int kCornerRadius = 12;
  if (highlighted_ || hovered_) {
    SetBackground(views::CreateRoundedRectBackground(
        dao::kActiveTabBackground, kCornerRadius));
  } else {
    SetBackground(nullptr);
  }
  SchedulePaint();
}

void DaoNewTabButton::OnNewTabClicked() {
  // Don't create a tab yet — just show the command bar.
  // The tab will be created when the user types a URL and presses Enter.
  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(browser_);
  if (browser_view && browser_view->dao_command_bar()) {
    browser_view->dao_command_bar()->ShowForNewTab();
  }
}

}  // namespace dao
