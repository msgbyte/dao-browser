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
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "build/build_config.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/browsing_data_filter_builder.h"
#include "content/public/browser/browsing_data_remover.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/download_request_utils.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_control_center_popup.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_variant.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_class_properties.h"
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
CreateActiveSiteBrowsingDataFilter(BrowserWindowInterface* browser) {
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

#if BUILDFLAG(IS_MAC)
  // Share button
  {
    auto btn = std::make_unique<MenuItemButton>(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_SHARE),
        base::BindRepeating(&DaoControlCenterMoreMenu::OnShareClicked,
                            base::Unretained(this)));
    share_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }

#endif
  {
    auto btn = std::make_unique<MenuItemButton>(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_SCREENSHOT),
        base::BindRepeating(&DaoControlCenterMoreMenu::OnScreenshotClicked,
                            base::Unretained(this)));
    screenshot_button_ = btn.get();
    AddChildView(static_cast<views::View*>(btn.release()));
  }

  cleanup_separator_ = AddChildView(std::make_unique<views::View>());
  cleanup_separator_->SetPreferredSize(gfx::Size(0, 1));
  cleanup_separator_->SetProperty(views::kMarginsKey, gfx::Insets::VH(6, 12));

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
  cleanup_separator_->SetBackground(
      views::CreateSolidBackground(SeparatorColor()));
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

void DaoControlCenterMoreMenu::OnScreenshotClicked() {
  if (!popup_ || !popup_->browser() || !screenshot_button_->GetEnabled()) {
    return;
  }
  auto* browser = popup_->browser();
  auto* contents = browser->tab_strip_model()->GetActiveWebContents();
  if (!contents) {
    return;
  }

  Observe(contents);
  popup_->Hide();
  if (browser->GetProfile()->GetPrefs()->GetBoolean(
          ::prefs::kDisableScreenshots) ||
      !screenshot_client_.AttachTo(contents)) {
    FinishScreenshot(/*failed=*/true);
    return;
  }

  screenshot_button_->SetEnabled(false);
  auto* browser_view = BrowserView::GetBrowserViewForBrowser(browser);
  if (browser_view && browser_view->dao_toast()) {
    browser_view->dao_toast()->ShowToast(
        l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_SCREENSHOT_CAPTURING));
    browser_view->InvalidateLayout();
  }
  screenshot_timeout_.Start(
      FROM_HERE, base::Seconds(30),
      base::BindOnce(&DaoControlCenterMoreMenu::FinishScreenshot,
                     weak_factory_.GetWeakPtr(), /*failed=*/true));
  // Omitting clip lets Chromium capture the full document at its current width.
  screenshot_client_.SendCommand(
      "Page.captureScreenshot",
      base::DictValue()
          .Set("format", "png")
          .Set("fromSurface", true)
          .Set("captureBeyondViewport", true),
      base::BindOnce(&DaoControlCenterMoreMenu::OnScreenshotCaptured,
                     weak_factory_.GetWeakPtr()),
      /*max_response_bytes=*/128 * 1024 * 1024);
}

void DaoControlCenterMoreMenu::OnScreenshotCaptured(
    DaoDevToolsClient::CommandResult result) {
  if (!web_contents()) {
    return;
  }
  const std::string* data = result.has_value() && result->is_dict()
                                ? result->GetDict().FindString("data")
                                : nullptr;
  if (!data || data->empty() ||
      popup_->browser()->GetProfile()->GetPrefs()->GetBoolean(
          ::prefs::kDisableScreenshots)) {
    FinishScreenshot(/*failed=*/true);
    return;
  }

  const GURL data_url("data:image/png;base64," + *data);
  if (!data_url.is_valid()) {
    FinishScreenshot(/*failed=*/true);
    return;
  }
  auto params =
      content::DownloadRequestUtils::CreateDownloadForWebContentsMainFrame(
          web_contents(), data_url,
          net::DefineNetworkTrafficAnnotation("dao_full_page_screenshot", R"(
            semantics {
              sender: "Dao Full Page Screenshot"
              description:
                "Saves a locally captured full-page PNG through the browser's "
                "download manager. No network request is made."
              trigger: "User selects Full Page Screenshot in site controls."
              data: "An image of the current loaded webpage."
              destination: LOCAL
            }
            policy {
              cookies_allowed: NO
              setting: "Available from site controls."
              chrome_policy {
                DisableScreenshots { DisableScreenshots: true }
              }
            })"));
  const std::string host(web_contents()->GetLastCommittedURL().host());
  params->set_suggested_name(base::UTF8ToUTF16(
      host.empty() ? "screenshot.png" : "screenshot-" + host + ".png"));
  auto* manager = web_contents()->GetBrowserContext()->GetDownloadManager();
  FinishScreenshot(/*failed=*/false);
  manager->DownloadUrl(std::move(params));
}

void DaoControlCenterMoreMenu::FinishScreenshot(bool failed) {
  const bool show_failure =
      failed && web_contents() && popup_ && popup_->browser() &&
      popup_->browser()->tab_strip_model()->GetActiveWebContents() ==
          web_contents();
  // Clear the target before detaching, which can synchronously cancel a
  // command.
  Observe(nullptr);
  screenshot_timeout_.Stop();
  screenshot_client_.Detach();
  screenshot_button_->SetEnabled(true);
  if (show_failure) {
    auto* browser_view =
        BrowserView::GetBrowserViewForBrowser(popup_->browser());
    if (browser_view && browser_view->dao_toast()) {
      browser_view->dao_toast()->ShowToast(
          l10n_util::GetStringUTF16(IDS_DAO_CONTROL_CENTER_SCREENSHOT_FAILED));
      browser_view->InvalidateLayout();
    }
  }
}

void DaoControlCenterMoreMenu::PrimaryPageChanged(content::Page& page) {
  FinishScreenshot(/*failed=*/true);
}

void DaoControlCenterMoreMenu::WebContentsDestroyed() {
  FinishScreenshot(/*failed=*/false);
}

void DaoControlCenterMoreMenu::OnClearCacheClicked() {
  ClearActiveSiteBrowsingData(
      content::BrowsingDataRemover::DATA_TYPE_CACHE,
      clear_cache_button_, IDS_DAO_CONTROL_CENTER_CLEAR_CACHE,
      IDS_DAO_CONTROL_CENTER_CACHE_CLEARED_TOAST);
}

void DaoControlCenterMoreMenu::OnClearCookiesClicked() {
  ClearActiveSiteBrowsingData(
      content::BrowsingDataRemover::DATA_TYPE_COOKIES,
      clear_cookies_button_, IDS_DAO_CONTROL_CENTER_CLEAR_COOKIES,
      IDS_DAO_CONTROL_CENTER_COOKIES_CLEARED_TOAST);
}

void DaoControlCenterMoreMenu::ClearActiveSiteBrowsingData(
    uint64_t remove_mask,
    views::LabelButton* button,
    int idle_string_id,
    int toast_string_id) {
  if (!popup_ || !popup_->browser() || !button || !button->GetEnabled()) {
    return;
  }

  auto filter_builder = CreateActiveSiteBrowsingDataFilter(popup_->browser());
  if (!filter_builder) {
    return;
  }

  auto* profile = popup_->browser()->GetProfile();
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
                   weak_factory_.GetWeakPtr(), button, idle_string_id,
                   toast_string_id));
  remover->RemoveWithFilterAndReply(
      base::Time(), base::Time::Max(),
      remove_mask,
      content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
      std::move(filter_builder), observer);
}

void DaoControlCenterMoreMenu::OnClearButtonOperationFinished(
    views::LabelButton* button,
    int idle_string_id,
    int toast_string_id) {
  if (button) {
    button->SetEnabled(true);
    button->SetText(l10n_util::GetStringUTF16(idle_string_id));
  }

  if (!popup_ || !popup_->browser()) {
    return;
  }

  BrowserView* browser_view =
      BrowserView::GetBrowserViewForBrowser(popup_->browser());
  popup_->Hide();

  if (!browser_view || !browser_view->dao_toast()) {
    return;
  }

  browser_view->dao_toast()->ShowToast(
      l10n_util::GetStringUTF16(toast_string_id));
  browser_view->InvalidateLayout();
}

}  // namespace dao
