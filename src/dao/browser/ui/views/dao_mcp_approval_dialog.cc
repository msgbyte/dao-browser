// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_mcp_approval_dialog.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/profiles/profile_view_utils.h"
#include "components/constrained_window/constrained_window_views.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_system_dialog.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace dao {
namespace {

constexpr int kDialogWidth = 480;
constexpr int kContentSpacing = 12;
constexpr int kDetailSpacing = 6;

std::u16string SanitizeLabel(std::u16string text) {
  text = base::CollapseWhitespace(text, true);
  base::i18n::SanitizeUserSuppliedString(&text);
  return text;
}

std::u16string SanitizeLabel(std::string_view text) {
  return SanitizeLabel(base::UTF8ToUTF16(text));
}

std::unique_ptr<views::Label> CreateLabel(std::u16string text,
                                          SkColor color,
                                          bool multiline) {
  auto label = std::make_unique<views::Label>(std::move(text));
  label->SetAutoColorReadabilityEnabled(false);
  label->SetEnabledColor(color);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  label->SetMultiLine(multiline);
  label->SetAllowCharacterBreak(true);
  label->SetElideBehavior(gfx::ELIDE_TAIL);
  if (multiline) {
    label->SetMaximumWidth(kDialogWidth);
  }
  return label;
}

bool IsEligibleApprovalBrowser(Browser* browser) {
  return browser && browser->is_type_normal() && browser->profile() &&
         !browser->profile()->IsOffTheRecord() &&
         !browser->profile()->IsGuestSession() && browser->window();
}

}  // namespace

DaoMcpApprovalDialog::DaoMcpApprovalDialog(
    const DaoMcpClientInfo& client,
    Browser* browser,
    base::OnceCallback<void(bool)> callback)
    : callback_(std::move(callback)) {
  SetTitle(l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_TITLE));
  SetShowCloseButton(true);
  SetModalType(ui::mojom::ModalType::kWindow);
  SetOwnedByWidget(OwnedByWidgetPassKey());
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_ALLOW));
  SetButtonLabel(ui::mojom::DialogButton::kCancel,
                 l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_DENY));
  SetButtonStyle(ui::mojom::DialogButton::kOk,
                 ui::ButtonStyle::kProminent);
  SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetAcceptCallback(base::BindOnce(&DaoMcpApprovalDialog::Resolve,
                                   base::Unretained(this), true));
  SetCancelCallback(base::BindOnce(&DaoMcpApprovalDialog::Resolve,
                                   base::Unretained(this), false));
  SetCloseCallback(base::BindOnce(&DaoMcpApprovalDialog::Resolve,
                                  base::Unretained(this), false));
  ConfigureDaoSystemDialog(
      this, {.show_enter_for_default = false,
             .show_esc_for_cancel = true,
             .center_in_web_contents = true});
  SetContentsView(BuildContents(client, browser));
}

DaoMcpApprovalDialog::~DaoMcpApprovalDialog() {
  Resolve(false);
}

void DaoMcpApprovalDialog::DismissWithoutResult() {
  callback_.Reset();
  if (views::Widget* widget = GetWidget()) {
    widget->CloseNow();
  }
}

base::WeakPtr<DaoMcpApprovalDialog> DaoMcpApprovalDialog::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

std::unique_ptr<views::View> DaoMcpApprovalDialog::BuildContents(
    const DaoMcpClientInfo& client,
    Browser* browser) {
  auto contents = std::make_unique<views::View>();
  auto* layout =
      contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kContentSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  contents->AddChildView(CreateLabel(
      l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_EXPLANATION),
      TextPrimary(), true));
  contents->AddChildView(CreateLabel(
      l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_WARNING), TextSecondary(),
      true));

  auto* details = contents->AddChildView(std::make_unique<views::View>());
  auto* details_layout =
      details->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kDetailSpacing));
  details_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  details->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_CLIENT,
                                 SanitizeLabel(client.name),
                                 SanitizeLabel(client.version)),
      TextPrimary(), false));
  const std::u16string pid =
      client.verified_pid
          ? base::NumberToString16(*client.verified_pid)
          : l10n_util::GetStringUTF16(
                IDS_DAO_MCP_APPROVAL_PROCESS_UNAVAILABLE);
  details->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_PROCESS, pid),
      TextSecondary(), false));

  std::u16string window_title =
      browser ? browser->GetWindowTitleForCurrentTab(false) : std::u16string();
  window_title = SanitizeLabel(std::move(window_title));
  details->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_WINDOW, window_title),
      TextSecondary(), false));

  std::u16string profile_name;
  if (browser && browser->profile()) {
    if (ProfileAttributesEntry* entry =
            GetProfileAttributesFromProfile(browser->profile())) {
      profile_name = entry->GetLocalProfileName();
    }
    if (profile_name.empty()) {
      profile_name = browser->profile()->GetPath().BaseName().AsUTF16Unsafe();
    }
  }
  details->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_PROFILE,
                                 SanitizeLabel(std::move(profile_name))),
      TextSecondary(), false));

  contents->SetPreferredSize(
      gfx::Size(kDialogWidth, contents->GetPreferredSize().height()));
  return contents;
}

void DaoMcpApprovalDialog::Resolve(bool allowed) {
  if (callback_) {
    std::move(callback_).Run(allowed);
  }
}

// static
DaoMcpApprovalDialogController* DaoMcpApprovalDialogController::Get() {
  static base::NoDestructor<DaoMcpApprovalDialogController> instance;
  return instance.get();
}

DaoMcpApprovalDialogController::DaoMcpApprovalDialogController() = default;
DaoMcpApprovalDialogController::~DaoMcpApprovalDialogController() = default;

void DaoMcpApprovalDialogController::RequestApproval(
    const DaoMcpClientInfo& client,
    Browser* browser,
    std::string_view connection_id,
    base::OnceCallback<void(bool)> callback) {
  if (pending_dialog_) {
    pending_dialog_->DismissWithoutResult();
  }
  pending_dialog_.reset();
  pending_connection_id_.clear();

  if (!IsEligibleApprovalBrowser(browser)) {
    std::move(callback).Run(false);
    return;
  }

  std::string owned_connection_id(connection_id);
  auto dialog = std::make_unique<DaoMcpApprovalDialog>(
      client, browser,
      base::BindOnce(&DaoMcpApprovalDialogController::OnDialogResult,
                     base::Unretained(this), owned_connection_id,
                     std::move(callback)));
  pending_dialog_ = dialog->GetWeakPtr();
  pending_connection_id_ = std::move(owned_connection_id);
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), browser->window()->GetNativeWindow());
  widget->Show();
}

void DaoMcpApprovalDialogController::CancelApproval(
    std::string_view connection_id) {
  if (connection_id != pending_connection_id_) {
    return;
  }
  if (pending_dialog_) {
    pending_dialog_->DismissWithoutResult();
  }
  pending_dialog_.reset();
  pending_connection_id_.clear();
}

void DaoMcpApprovalDialogController::OnDialogResult(
    std::string connection_id,
    base::OnceCallback<void(bool)> callback,
    bool allowed) {
  if (connection_id != pending_connection_id_) {
    return;
  }
  pending_dialog_.reset();
  pending_connection_id_.clear();
  std::move(callback).Run(allowed);
}

}  // namespace dao
