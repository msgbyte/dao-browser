// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_more_menu.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {

namespace {
constexpr int kMenuCornerRadius = 8;

// A menu-style button with hover effect.
class MenuItemButton : public views::LabelButton {
 public:
  MenuItemButton(const std::u16string& text,
                 views::Button::PressedCallback callback)
      : LabelButton(std::move(callback), text) {
    SetInstallFocusRingOnFocus(false);
    SetAccessibleName(text);
    label()->SetFontList(gfx::FontList({"system-ui"}, gfx::Font::NORMAL, 13,
                                        gfx::Font::Weight::NORMAL));
    SetEnabledTextColors(SkColorSetRGB(50, 50, 50));
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 12)));
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    LabelButton::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        SkColorSetARGB(20, 0, 0, 0), kMenuCornerRadius));
    SchedulePaint();
  }

  void OnMouseExited(const ui::MouseEvent& event) override {
    LabelButton::OnMouseExited(event);
    SetBackground(nullptr);
    SchedulePaint();
  }
};

}  // namespace

BEGIN_METADATA(DaoControlCenterMoreMenu)
END_METADATA

DaoControlCenterMoreMenu::DaoControlCenterMoreMenu(
    DaoControlCenterPopup* popup)
    : popup_(popup) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(8, 0), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  // Back button
  auto back_btn = std::make_unique<views::LabelButton>(
      base::BindRepeating(&DaoControlCenterMoreMenu::OnBackClicked,
                          base::Unretained(this)),
      u"\u2190 Back");
  back_btn->SetInstallFocusRingOnFocus(false);
  back_btn->SetAccessibleName(u"Back");
  back_btn->SetEnabledTextColors(SkColorSetRGB(100, 100, 100));
  back_btn->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 12)));
  AddChildView(std::move(back_btn));

  // Clear Cache button
  {
    auto btn = std::make_unique<MenuItemButton>(
        u"Clear Cache",
        base::BindRepeating(&DaoControlCenterMoreMenu::OnClearCacheClicked,
                            base::Unretained(this)));
    clear_cache_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }

  // Clear Cookies button
  {
    auto btn = std::make_unique<MenuItemButton>(
        u"Clear Cookies",
        base::BindRepeating(&DaoControlCenterMoreMenu::OnClearCookiesClicked,
                            base::Unretained(this)));
    clear_cookies_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }
}

DaoControlCenterMoreMenu::~DaoControlCenterMoreMenu() = default;

void DaoControlCenterMoreMenu::OnBackClicked() {
  if (popup_) {
    popup_->ShowMainPanel();
  }
}

void DaoControlCenterMoreMenu::OnClearCacheClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }

  auto* profile = popup_->browser()->profile();
  auto* remover = profile->GetBrowsingDataRemover();
  if (!remover) {
    return;
  }

  // Disable button during operation
  clear_cache_button_->SetEnabled(false);
  clear_cache_button_->SetText(u"Clearing...");

  remover->RemoveAndReply(
      base::Time(), base::Time::Max(),
      content::BrowsingDataRemover::DATA_TYPE_CACHE,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      nullptr);

  // Re-enable after a short delay (remover is async but we don't need
  // precise completion tracking for this simple UI)
  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<DaoControlCenterMoreMenu> self) {
            if (self && self->clear_cache_button_) {
              self->clear_cache_button_->SetEnabled(true);
              self->clear_cache_button_->SetText(u"Clear Cache");
            }
          },
          weak_factory_.GetWeakPtr()),
      base::Seconds(2));
}

void DaoControlCenterMoreMenu::OnClearCookiesClicked() {
  if (!popup_ || !popup_->browser()) {
    return;
  }

  auto* profile = popup_->browser()->profile();
  auto* remover = profile->GetBrowsingDataRemover();
  if (!remover) {
    return;
  }

  // Disable button during operation
  clear_cookies_button_->SetEnabled(false);
  clear_cookies_button_->SetText(u"Clearing...");

  remover->RemoveAndReply(
      base::Time(), base::Time::Max(),
      content::BrowsingDataRemover::DATA_TYPE_COOKIES,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      nullptr);

  base::SequencedTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<DaoControlCenterMoreMenu> self) {
            if (self && self->clear_cookies_button_) {
              self->clear_cookies_button_->SetEnabled(true);
              self->clear_cookies_button_->SetText(
                  u"Clear Cookies");
            }
          },
          weak_factory_.GetWeakPtr()),
      base::Seconds(2));
}

}  // namespace dao
