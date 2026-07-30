// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_mcp_control_banner_view.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_lucide_icons.h"
#include "dao/browser/ui/views/dao_system_dialog.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/background.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {
namespace {

constexpr int kBannerHeight = 52;
constexpr int kHorizontalPadding = 14;
constexpr int kItemSpacing = 10;
constexpr int kIconSize = 20;

std::u16string SanitizeLabel(std::u16string text) {
  text = base::CollapseWhitespace(text, true);
  base::i18n::SanitizeUserSuppliedString(&text);
  return text;
}

std::u16string SanitizeLabel(std::string_view text) {
  return SanitizeLabel(base::UTF8ToUTF16(text));
}

}  // namespace

BEGIN_METADATA(DaoMcpControlBannerView)
END_METADATA

DaoMcpControlBannerView::DaoMcpControlBannerView(Browser* browser)
    : browser_(browser) {
  SetPreferredSize(gfx::Size(0, kBannerHeight));
  SetVisible(false);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets::VH(0, kHorizontalPadding), kItemSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);

  icon_ = AddChildView(std::make_unique<views::ImageView>());
  icon_->SetPreferredSize(gfx::Size(kIconSize, kIconSize));

  auto* labels = AddChildView(std::make_unique<views::View>());
  auto* labels_layout =
      labels->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  labels_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(labels, 1);

  title_label_ = labels->AddChildView(std::make_unique<views::Label>());
  title_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  target_label_ = labels->AddChildView(std::make_unique<views::Label>());
  target_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  target_label_->SetElideBehavior(gfx::ELIDE_TAIL);

  stop_button_ = AddChildView(CreateDaoDialogButton(
      base::BindRepeating(&DaoMcpControlBannerView::OnStopPressed,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_STOP), std::nullopt,
      ui::ButtonStyle::kTonal));
  UpdateColors();

  service_subscription_ = DaoMcpService::Get()->AddObserver(base::BindRepeating(
      &DaoMcpControlBannerView::OnServiceStatusChanged,
      base::Unretained(this)));
  if (browser_ && browser_->tab_strip_model()) {
    browser_->tab_strip_model()->AddObserver(this);
  }
  RefreshFromService();
}

DaoMcpControlBannerView::~DaoMcpControlBannerView() = default;

void DaoMcpControlBannerView::SetConnection(
    std::string_view client_name,
    std::u16string_view tab_title) {
  title_label_->SetText(l10n_util::GetStringFUTF16(
      IDS_DAO_MCP_CONTROL_TITLE, SanitizeLabel(client_name)));
  target_label_->SetText(l10n_util::GetStringFUTF16(
      IDS_DAO_MCP_CONTROL_TARGET,
      SanitizeLabel(std::u16string(tab_title))));
  SetVisible(true);
  PreferredSizeChanged();
}

void DaoMcpControlBannerView::ClearConnection() {
  Observe(nullptr);
  SetVisible(false);
  title_label_->SetText(std::u16string());
  target_label_->SetText(std::u16string());
  PreferredSizeChanged();
}

void DaoMcpControlBannerView::TitleWasSet(content::NavigationEntry*) {
  RefreshFromService();
}

void DaoMcpControlBannerView::PrimaryPageChanged(content::Page&) {
  RefreshFromService();
}

void DaoMcpControlBannerView::WebContentsDestroyed() {
  ClearConnection();
}

void DaoMcpControlBannerView::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange& change,
    const TabStripSelectionChange&) {
  if (change.type() == TabStripModelChange::kRemoved) {
    for (const auto& removed : change.GetRemove()->contents) {
      if (removed.contents == web_contents()) {
        ClearConnection();
        return;
      }
    }
  }
  RefreshFromService();
}

void DaoMcpControlBannerView::OnThemeChanged() {
  views::View::OnThemeChanged();
  UpdateColors();
}

void DaoMcpControlBannerView::OnServiceStatusChanged(
    const DaoMcpServiceStatus&) {
  RefreshFromService();
}

void DaoMcpControlBannerView::RefreshFromService() {
  DaoMcpService* service = DaoMcpService::Get();
  DaoMcpServiceStatus status = service->GetStatus();
  if (status.state != DaoMcpStatus::kLeaseActive || !status.client ||
      service->GetAuthorizedBrowser() != browser_) {
    ClearConnection();
    return;
  }

  content::WebContents* target = service->GetAuthorizedTarget();
  if (!target) {
    ClearConnection();
    return;
  }
  if (web_contents() != target) {
    Observe(target);
  }
  SetConnection(status.client->name, target->GetTitle());
}

void DaoMcpControlBannerView::UpdateColors() {
  SetBackground(views::CreateSolidBackground(PopupBackground()));
  icon_->SetImage(ui::ImageModel::FromImageSkia(
      CreateLucideImageSkia(LucideIcon::kBot, kIconSize, TextPrimary())));
  title_label_->SetEnabledColor(TextPrimary());
  target_label_->SetEnabledColor(TextSecondary(browser_));
  SchedulePaint();
}

void DaoMcpControlBannerView::OnStopPressed() {
  DaoMcpService::Get()->StopControl();
  BrowserView* browser_view =
      browser_ ? BrowserView::GetBrowserViewForBrowser(browser_) : nullptr;
  if (browser_view && browser_view->dao_toast()) {
    browser_view->dao_toast()->ShowToast(
        l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_REVOKED));
  }
}

}  // namespace dao
