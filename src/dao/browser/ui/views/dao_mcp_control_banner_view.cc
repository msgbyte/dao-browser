// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_mcp_control_banner_view.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
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
#include "ui/views/controls/scroll_view.h"
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

std::unique_ptr<views::Label> CreateLabel(std::u16string text, SkColor color) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  return label;
}

std::vector<DaoMcpConnectionInfo> GetWindowConnections(BrowserWindowInterface* browser) {
  auto connections = DaoMcpService::Get()->GetConnections();
  std::erase_if(connections, [browser](const auto& connection) {
    return connection.browser_window.get() != browser;
  });
  return connections;
}

// Page titles and URLs refresh in place; only grant changes rebuild the rows.
bool HasSamePermissions(const DaoMcpConnectionInfo& before,
                        const DaoMcpConnectionInfo& after) {
  return before.connection_id == after.connection_id &&
         before.client.name == after.client.name &&
         before.client.version == after.client.version &&
         before.can_create_tabs == after.can_create_tabs &&
         std::ranges::equal(before.tabs, after.tabs,
                            [](const auto& a, const auto& b) {
                              return a.target.tab_id == b.target.tab_id;
                            });
}

void UpdateLatestToolLabel(views::Label* label,
                          const DaoMcpConnectionInfo& connection) {
  label->SetVisible(connection.latest_tool_name.has_value());
  if (connection.latest_tool_name) {
    label->SetText(l10n_util::GetStringFUTF16(
        IDS_DAO_MCP_CONTROL_LATEST_TOOL,
        SanitizeLabel(*connection.latest_tool_name)));
  }
}

}  // namespace

BEGIN_METADATA(DaoMcpControlBannerView)
END_METADATA

DaoMcpControlBannerView::DaoMcpControlBannerView(views::View* anchor_view,
                                               BrowserWindowInterface* browser)
    : views::BubbleDialogDelegate(anchor_view, views::BubbleBorder::TOP_LEFT),
      browser_(browser) {
  SetTitle(
      l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_BUTTON_ACCESSIBLE_NAME));
  SetShowCloseButton(true);
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  set_fixed_width(kBubbleWidth);

  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kItemSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto* scroll = AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, 400);
  auto list = std::make_unique<views::View>();
  list->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), kItemSpacing));
  connections_ = GetWindowConnections(browser);
  for (const auto& connection : connections_) {
    list->AddChildView(CreateLabel(
        l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_CLIENT,
                                   SanitizeLabel(connection.client.name),
                                   SanitizeLabel(connection.client.version)),
        TextPrimary()));
    list->AddChildView(CreateLabel(
        l10n_util::GetStringUTF16(connection.can_create_tabs
                                      ? IDS_DAO_MCP_CONTROL_CREATE_ALLOWED
                                      : IDS_DAO_MCP_CONTROL_PAGE_ONLY),
        TextSecondary()));
    auto* latest_tool =
        list->AddChildView(CreateLabel(std::u16string(), TextSecondary()));
    latest_tool_labels_.push_back(latest_tool);
    UpdateLatestToolLabel(latest_tool, connection);
    list->AddChildView(
        CreateLabel(l10n_util::GetStringFUTF16(
                        IDS_DAO_MCP_CONTROL_TAB_COUNT,
                        base::NumberToString16(connection.tabs.size())),
                    TextSecondary()));
    for (const auto& tab : connection.tabs) {
      auto* title = list->AddChildView(
          CreateLabel(SanitizeLabel(tab.title), TextPrimary()));
      auto* url = list->AddChildView(
          CreateLabel(SanitizeLabel(tab.target.url), TextSecondary()));
      tab_labels_.push_back({title, url});
      list->AddChildView(CreateDaoDialogButton(
          base::BindRepeating(
              [](std::string connection_id, std::string tab_id) {
                DaoMcpService::Get()->RevokeTab(connection_id, tab_id);
              },
              connection.connection_id, tab.target.tab_id),
          l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_REVOKE_TAB),
          std::nullopt, ui::ButtonStyle::kTonal));
    }
    auto* stop = list->AddChildView(CreateDaoDialogButton(
        base::BindRepeating(&DaoMcpControlBannerView::OnStopPressed,
                            base::Unretained(this), connection.connection_id),
        l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_STOP), std::nullopt,
        ui::ButtonStyle::kTonal));
    if (!stop_button_) {
      stop_button_ = stop;
    }
  }
  scroll->SetContents(std::move(list));
}

DaoMcpControlBannerView::~DaoMcpControlBannerView() = default;

bool DaoMcpControlBannerView::UpdateConnections() {
  auto connections = GetWindowConnections(browser_);
  if (!std::ranges::equal(connections_, connections, HasSamePermissions)) {
    return false;
  }
  bool changed = false;
  size_t tab_label_index = 0;
  for (size_t i = 0; i < connections.size(); ++i) {
    if (connections_[i].latest_tool_name != connections[i].latest_tool_name) {
      UpdateLatestToolLabel(latest_tool_labels_[i], connections[i]);
      changed = true;
    }
    for (size_t j = 0; j < connections[i].tabs.size(); ++j) {
      const DaoMcpTabGrant& before = connections_[i].tabs[j];
      const DaoMcpTabGrant& after = connections[i].tabs[j];
      auto& [title, url] = tab_labels_[tab_label_index++];
      if (before.title != after.title ||
          before.target.url != after.target.url) {
        title->SetText(SanitizeLabel(after.title));
        url->SetText(SanitizeLabel(after.target.url));
        changed = true;
      }
    }
  }
  connections_ = std::move(connections);
  if (changed) {
    SizeToContents();
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

void DaoMcpControlBannerView::OnStopPressed(std::string connection_id) {
  if (browser_) {
    BrowserView* browser_view =
        BrowserView::GetBrowserViewForBrowser(browser_);
    if (browser_view && browser_view->dao_toast()) {
      browser_view->dao_toast()->ShowToast(
          l10n_util::GetStringUTF16(IDS_DAO_MCP_CONTROL_REVOKED));
    }
  }
  DaoMcpService::Get()->StopControl(connection_id);
}

}  // namespace dao
