// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_mcp_control_banner_view.h"

#include <memory>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/mcp/dao_mcp_service.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_system_dialog.h"
#include "dao/browser/ui/views/dao_toast_view.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"

namespace dao {
namespace {

constexpr int kBubbleWidth = 320;
constexpr int kItemSpacing = 8;

std::u16string SanitizeLabel(std::u16string text) {
  text = base::CollapseWhitespace(text, true);
  base::i18n::SanitizeUserSuppliedString(&text);
  return text;
}

std::u16string SanitizeLabel(std::string_view text) {
  return SanitizeLabel(base::UTF8ToUTF16(text));
}

std::unique_ptr<views::Label> CreateLabel(std::u16string text,
                                          SkColor color) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

}  // namespace

BEGIN_METADATA(DaoMcpControlBannerView)
END_METADATA

DaoMcpControlBannerView::DaoMcpControlBannerView(
    views::View* anchor_view,
    Browser* browser,
    content::WebContents* target)
    : views::BubbleDialogDelegate(anchor_view,
                                  views::BubbleBorder::TOP_LEFT),
      browser_(browser) {
  const DaoMcpServiceStatus status = DaoMcpService::Get()->GetStatus();
  const DaoMcpClientInfo client = status.client.value_or(DaoMcpClientInfo());
  SetTitle(l10n_util::GetStringFUTF16(IDS_DAO_MCP_CONTROL_TITLE,
                                      SanitizeLabel(client.name)));
  SetShowCloseButton(true);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_fixed_width(kBubbleWidth);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kItemSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_CLIENT,
                                 SanitizeLabel(client.name),
                                 SanitizeLabel(client.version)),
      TextPrimary()));
  const std::u16string pid =
      client.verified_pid
          ? base::NumberToString16(*client.verified_pid)
          : l10n_util::GetStringUTF16(
                IDS_DAO_MCP_APPROVAL_PROCESS_UNAVAILABLE);
  AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_PROCESS, pid),
      TextSecondary()));
  AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(
          IDS_DAO_MCP_CONTROL_TARGET,
          SanitizeLabel(target ? target->GetTitle() : std::u16string())),
      TextSecondary()));
  latest_tool_label_ =
      AddChildView(CreateLabel(std::u16string(), TextSecondary()));
  latest_tool_label_->SetVisible(false);
  UpdateLatestToolCall(status);
  AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(
          IDS_DAO_MCP_CONTROL_TAB_COUNT,
          base::NumberToString16(
              DaoMcpService::Get()->GetControlledTargetCount())),
      TextSecondary()));

  stop_button_ = AddChildView(CreateDaoDialogButton(
      base::BindRepeating(&DaoMcpControlBannerView::OnStopPressed,
                          base::Unretained(this)),
      l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_STOP), std::nullopt,
      ui::ButtonStyle::kTonal));
}

DaoMcpControlBannerView::~DaoMcpControlBannerView() = default;

bool DaoMcpControlBannerView::UpdateLatestToolCall(
    const DaoMcpServiceStatus& status) {
  if (status.latest_tool_call_serial == latest_tool_call_serial_) {
    return false;
  }
  latest_tool_call_serial_ = status.latest_tool_call_serial;
  latest_tool_label_->SetVisible(status.latest_tool_name.has_value());
  if (status.latest_tool_name) {
    latest_tool_label_->SetText(l10n_util::GetStringFUTF16(
        IDS_DAO_MCP_CONTROL_LATEST_TOOL,
        SanitizeLabel(*status.latest_tool_name)));
  }
  return true;
}

views::View* DaoMcpControlBannerView::GetContentsView() {
  return this;
}

views::Widget* DaoMcpControlBannerView::GetWidget() {
  return views::View::GetWidget();
}

const views::Widget* DaoMcpControlBannerView::GetWidget() const {
  return views::View::GetWidget();
}

void DaoMcpControlBannerView::OnStopPressed() {
  if (browser_) {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->dao_toast()) {
      browser_view->dao_toast()->ShowToast(
          l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_REVOKED));
    }
  }
  DaoMcpService::Get()->StopControl();
}

}  // namespace dao
