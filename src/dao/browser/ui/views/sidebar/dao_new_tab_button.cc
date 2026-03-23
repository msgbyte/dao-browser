// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/sidebar/dao_new_tab_button.h"

#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_command_bar_view.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/animation/ink_drop_highlight.h"
#include "ui/views/background.h"
#include "ui/views/controls/highlight_path_generator.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kItemHeight = 40;
constexpr int kCornerRadius = 12;
}  // namespace

// A simple view that draws a Lucide Plus icon at 16x16.
class DaoPlusIconView : public views::View {
  METADATA_HEADER(DaoPlusIconView, views::View)

 public:
  DaoPlusIconView() {
    SetPreferredSize(gfx::Size(16, 16));
  }

  void OnPaint(gfx::Canvas* canvas) override {
    gfx::RectF icon_rect(0, 0, 16, 16);
    icon_rect.Inset(1);
    DrawLucideIcon(canvas, LucideIcon::kPlus, icon_rect, dao::kTextSecondary);
  }
};

BEGIN_METADATA(DaoPlusIconView)
END_METADATA

BEGIN_METADATA(DaoNewTabButton)
END_METADATA

DaoNewTabButton::DaoNewTabButton(Browser* browser)
    : Button(base::BindRepeating(&DaoNewTabButton::OnNewTabClicked,
                                 base::Unretained(this))),
      browser_(browser) {
  // Match DaoTabItemView layout: same padding and spacing.
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, 10), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  // Plus icon (same 16x16 as favicon in tab items).
  plus_icon_ = AddChildView(std::make_unique<DaoPlusIconView>());

  auto* text_label =
      AddChildView(std::make_unique<views::Label>(u"New Tab"));
  text_label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  text_label->SetFontList(gfx::FontList(
      {"sans-serif"}, gfx::Font::FontStyle::NORMAL, 13,
      gfx::Font::Weight::SEMIBOLD));
  text_label->SetEnabledColor(dao::kTextSecondary);
  text_label->SetSubpixelRenderingEnabled(false);
  layout->SetFlexForView(text_label, 1);

  // InkDrop ripple matching tab items.
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::ON);
  views::InkDrop::Get(this)->SetBaseColor(dao::kInkDropBase);
  views::InkDrop::Get(this)->SetVisibleOpacity(dao::kInkDropOpacity);
  views::InkDrop::Get(this)->SetCreateHighlightCallback(base::BindRepeating(
      [](views::Button* host) {
        auto highlight = std::make_unique<views::InkDropHighlight>(
            gfx::SizeF(host->size()), dao::kInkDropBase);
        highlight->set_visible_opacity(dao::kInkDropOpacity);
        return highlight;
      },
      this));
  views::InstallRoundRectHighlightPathGenerator(this, gfx::Insets(),
                                                 kCornerRadius);
  SetInstallFocusRingOnFocus(false);

  SetPreferredSize(gfx::Size(0, kItemHeight));
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

void DaoNewTabButton::UpdateBackground() {
  if (highlighted_) {
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
