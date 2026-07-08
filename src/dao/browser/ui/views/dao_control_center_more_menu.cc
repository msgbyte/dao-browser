// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_control_center_more_menu.h"

#include <memory>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/scoped_observation.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_variant.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "url/gurl.h"

namespace dao {

namespace {
constexpr int kMenuCornerRadius = 8;

class BrowsingDataRemovalObserver
    : public content::BrowsingDataRemover::Observer {
 public:
  static content::BrowsingDataRemover::Observer* Create(
      content::BrowsingDataRemover* remover,
      base::OnceClosure completion_callback) {
    return new BrowsingDataRemovalObserver(remover,
                                           std::move(completion_callback));
  }

  void OnBrowsingDataRemoverDone(uint64_t failed_data_types) override {
    remover_observation_.Reset();
    if (completion_callback_) {
      std::move(completion_callback_).Run();
    }
    delete this;
  }

 private:
  BrowsingDataRemovalObserver(content::BrowsingDataRemover* remover,
                              base::OnceClosure completion_callback)
      : completion_callback_(std::move(completion_callback)) {
    remover_observation_.Observe(remover);
  }

  base::OnceClosure completion_callback_;
  base::ScopedObservation<content::BrowsingDataRemover,
                          content::BrowsingDataRemover::Observer>
      remover_observation_{this};
};

std::unique_ptr<content::BrowsingDataFilterBuilder>
CreateActiveSiteBrowsingDataFilter(Browser* browser) {
  if (!browser || !browser->tab_strip_model()) {
    return nullptr;
  }
  content::WebContents* web_contents =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!web_contents) {
    return nullptr;
  }

  const GURL url = web_contents->GetVisibleURL();
  if (!url.has_host()) {
    return nullptr;
  }

  std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  if (domain.empty()) {
    domain = url.host();
  }
  if (domain.empty()) {
    return nullptr;
  }

  auto filter_builder = content::BrowsingDataFilterBuilder::Create(
      content::BrowsingDataFilterBuilder::Mode::kDelete);
  filter_builder->AddRegisterableDomain(domain);
  return filter_builder;
}

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
    ApplyTheme();
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(8, 12)));
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }

  void ApplyTheme() {
    const SkColor text_color = ControlCenterLabelColor();
    SetTextColor(views::Button::STATE_NORMAL, ui::ColorVariant(text_color));
    SetTextColor(views::Button::STATE_HOVERED, ui::ColorVariant(text_color));
    SetTextColor(views::Button::STATE_PRESSED, ui::ColorVariant(text_color));
    SetTextColor(views::Button::STATE_DISABLED,
                 ui::ColorVariant(ControlCenterSecondaryTextColor()));
  }

  void OnMouseEntered(const ui::MouseEvent& event) override {
    LabelButton::OnMouseEntered(event);
    SetBackground(views::CreateRoundedRectBackground(
        ControlCenterHoverBg(), kMenuCornerRadius));
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
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_BACK_BUTTON_LABEL));
  back_btn->SetInstallFocusRingOnFocus(false);
  back_btn->SetAccessibleName(l10n_util::GetStringUTF16(
      IDS_DAO_CONTROL_CENTER_BACK_BUTTON_ACCESSIBLE_NAME));
  back_btn->SetEnabledTextColors(ControlCenterLabelColor());
  back_btn->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(4, 12)));
  AddChildView(std::move(back_btn));

  // Share button
  {
    auto btn = std::make_unique<MenuItemButton>(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_SHARE),
        base::BindRepeating(&DaoControlCenterMoreMenu::OnShareClicked,
                            base::Unretained(this)));
    share_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }

  // Clear Cache button
  {
    auto btn = std::make_unique<MenuItemButton>(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_CLEAR_CACHE),
        base::BindRepeating(&DaoControlCenterMoreMenu::OnClearCacheClicked,
                            base::Unretained(this)));
    clear_cache_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }

  // Clear Cookies button
  {
    auto btn = std::make_unique<MenuItemButton>(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_CLEAR_COOKIES),
        base::BindRepeating(&DaoControlCenterMoreMenu::OnClearCookiesClicked,
                            base::Unretained(this)));
    clear_cookies_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }
}

DaoControlCenterMoreMenu::~DaoControlCenterMoreMenu() = default;

void DaoControlCenterMoreMenu::ApplyTheme() {
  for (views::View* child : children()) {
    if (auto* button = views::AsViewClass<views::LabelButton>(child)) {
      button->SetEnabledTextColors(ControlCenterLabelColor());
      button->SetTextColor(
          views::Button::STATE_DISABLED,
          ui::ColorVariant(ControlCenterSecondaryTextColor()));
    }
  }
}

void DaoControlCenterMoreMenu::OnBackClicked() {
  if (popup_) {
    popup_->ShowMainPanel();
  }
}

void DaoControlCenterMoreMenu::OnShareClicked() {
  if (popup_) {
    popup_->ShareCurrentPage(share_button_ ? share_button_->GetBoundsInScreen()
                                           : GetBoundsInScreen());
  }
}

void DaoControlCenterMoreMenu::OnClearCacheClicked() {
  ClearActiveSiteBrowsingData(
      content::BrowsingDataRemover::DATA_TYPE_CACHE,
      clear_cache_button_, IDS_DAO_CONTROL_CENTER_CLEAR_CACHE);
}

void DaoControlCenterMoreMenu::OnClearCookiesClicked() {
  ClearActiveSiteBrowsingData(
      content::BrowsingDataRemover::DATA_TYPE_COOKIES,
      clear_cookies_button_, IDS_DAO_CONTROL_CENTER_CLEAR_COOKIES);
}

void DaoControlCenterMoreMenu::ClearActiveSiteBrowsingData(
    uint64_t remove_mask,
    views::LabelButton* button,
    int idle_string_id) {
  if (!popup_ || !popup_->browser() || !button || !button->GetEnabled()) {
    return;
  }

  auto filter_builder = CreateActiveSiteBrowsingDataFilter(popup_->browser());
  if (!filter_builder) {
    return;
  }

  auto* profile = popup_->browser()->profile();
  auto* remover = profile->GetBrowsingDataRemover();
  if (!remover) {
    return;
  }

  button->SetEnabled(false);
  button->SetText(
      l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_CLEARING));

  auto* observer = BrowsingDataRemovalObserver::Create(
      remover, base::BindOnce(
                   &DaoControlCenterMoreMenu::OnClearButtonOperationFinished,
                   weak_factory_.GetWeakPtr(), button, idle_string_id));
  remover->RemoveWithFilterAndReply(
      base::Time(), base::Time::Max(),
      remove_mask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      std::move(filter_builder), observer);
}

void DaoControlCenterMoreMenu::OnClearButtonOperationFinished(
    views::LabelButton* button,
    int idle_string_id) {
  if (!button) {
    return;
  }
  button->SetEnabled(true);
  button->SetText(l10n_util::GetStringUTF16(idle_string_id));
}

}  // namespace dao
