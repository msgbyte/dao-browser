// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/ui/views/dao_mcp_approval_dialog.h"

#include <memory>
#include <utility>

#include "base/functional/bind.h"
#include "base/i18n/rtl.h"
#include "base/i18n/time_formatting.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_entry.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/profiles/profile_view_utils.h"
#include "chrome/browser/ui/window_metadata/window_metadata_controller.h"
#include "components/constrained_window/constrained_window_views.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/strings/grit/dao_strings.h"
#include "dao/browser/ui/views/dao_colors.h"
#include "dao/browser/ui/views/dao_system_dialog.h"
#include "ui/base/base_window.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/mojom/dialog_button.mojom.h"
#include "ui/base/mojom/ui_base_types.mojom-shared.h"
#include "ui/gfx/text_constants.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/radio_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/non_client_view.h"
#include "url/gurl.h"

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
  } else {
    label->SetMaximumWidthSingleLine(kDialogWidth);
    label->SetCustomTooltipText(label->GetText());
  }
  return label;
}

bool IsEligibleApprovalBrowser(BrowserWindowInterface* browser) {
  return browser &&
         (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL) &&
         browser->GetProfile() && !browser->GetProfile()->IsOffTheRecord() &&
         !browser->GetProfile()->IsGuestSession() && browser->GetWindow();
}

}  // namespace

DaoMcpApprovalDialog::DaoMcpApprovalDialog(
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser,
    base::OnceCallback<void(bool)> callback)
    : DaoMcpApprovalDialog(request,
                           browser,
                           base::BindOnce(
                               [](base::OnceCallback<void(bool)> callback,
                                  std::optional<std::string> result) {
                                 std::move(callback).Run(result.has_value());
                               },
                               std::move(callback))) {}

DaoMcpApprovalDialog::DaoMcpApprovalDialog(
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser,
    base::OnceCallback<void(std::optional<std::string>)> callback)
    : callback_(std::move(callback)) {
  if (request.tab_target) {
    explicit_tab_id_ = request.tab_target->tab_id;
  }
  int title_id = IDS_DAO_MCP_APPROVAL_TITLE;
  if (request.background_space) {
    title_id = IDS_DAO_MCP_APPROVAL_SPACE_TITLE;
  } else if (request.tab_target) {
    title_id = IDS_DAO_MCP_APPROVAL_TAB_TITLE;
  } else if (!request.tab_choices.empty()) {
    title_id = IDS_DAO_MCP_APPROVAL_TAB_PICKER_TITLE;
  }
  SetTitle(l10n_util::GetStringUTF16(title_id));
  SetShowCloseButton(true);
  SetModalType(ui::mojom::ModalType::kWindow);
  SetOwnedByWidget(OwnedByWidgetPassKey());
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 l10n_util::GetStringUTF16(
                     request.background_space
                         ? IDS_DAO_MCP_APPROVAL_SPACE_ALLOW_ONE_HOUR
                         : IDS_DAO_MCP_APPROVAL_ALLOW));
  SetButtonLabel(ui::mojom::DialogButton::kCancel,
                 l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_DENY));
  SetButtonStyle(ui::mojom::DialogButton::kOk,
                 ui::ButtonStyle::kProminent);
  SetButtonStyle(ui::mojom::DialogButton::kCancel, ui::ButtonStyle::kDefault);
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
  SetContentsView(BuildContents(request, browser));
}

// static
void DaoMcpApprovalDialog::ShowAgentSpaceConsent(
    BrowserWindowInterface* browser,
    const std::u16string& task_name,
    ConsentCallback callback) {
  if (!IsEligibleApprovalBrowser(browser)) {
    std::move(callback).Run(false, false);
    return;
  }
  browser->GetWindow()->Activate();
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::make_unique<DaoMcpApprovalDialog>(task_name, std::move(callback)),
      browser->GetWindow()->GetNativeWindow());
  widget->Show();
}

DaoMcpApprovalDialog::DaoMcpApprovalDialog(const std::u16string& task_name,
                                           ConsentCallback callback)
    : consent_callback_(std::move(callback)) {
  SetTitle(l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACE_CONSENT_TITLE));
  SetShowCloseButton(true);
  SetModalType(ui::mojom::ModalType::kWindow);
  SetOwnedByWidget(OwnedByWidgetPassKey());
  SetButtons(static_cast<int>(ui::mojom::DialogButton::kOk) |
             static_cast<int>(ui::mojom::DialogButton::kCancel));
  SetButtonLabel(ui::mojom::DialogButton::kOk,
                 l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACE_CONSENT_ALLOW));
  SetButtonLabel(ui::mojom::DialogButton::kCancel,
                 l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACE_CONSENT_DENY));
  SetButtonStyle(ui::mojom::DialogButton::kOk, ui::ButtonStyle::kProminent);
  SetDefaultButton(static_cast<int>(ui::mojom::DialogButton::kNone));
  SetAcceptCallback(base::BindOnce(&DaoMcpApprovalDialog::Resolve,
                                   base::Unretained(this), true));
  SetCancelCallback(base::BindOnce(&DaoMcpApprovalDialog::Resolve,
                                   base::Unretained(this), false));
  SetCloseCallback(base::BindOnce(&DaoMcpApprovalDialog::Resolve,
                                  base::Unretained(this), false));
  ConfigureDaoSystemDialog(this, {.show_enter_for_default = false,
                                  .show_esc_for_cancel = true,
                                  .center_in_web_contents = true});

  auto contents = std::make_unique<views::View>();
  contents
      ->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kContentSpacing))
      ->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kStretch);
  contents->AddChildView(
      CreateLabel(l10n_util::GetStringFUTF16(IDS_DAO_AGENT_SPACE_CONSENT_BODY,
                                             SanitizeLabel(task_name)),
                  TextPrimary(), true));
  contents->AddChildView(
      CreateLabel(l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACE_CONSENT_DETAIL),
                  TextSecondary(), true));
  auto* checkbox = contents->AddChildView(std::make_unique<views::Checkbox>(
      l10n_util::GetStringUTF16(IDS_DAO_AGENT_SPACE_CONSENT_REMEMBER)));
  checkbox->SetCallback(base::BindRepeating(
      [](DaoMcpApprovalDialog* dialog, views::Checkbox* checkbox) {
        dialog->remember_ = checkbox->GetChecked();
      },
      base::Unretained(this), base::Unretained(checkbox)));
  contents->SetPreferredSize(
      gfx::Size(kDialogWidth, contents->GetPreferredSize().height()));
  ConfigureDaoSystemDialogContent(contents.get());
  SetContentsView(std::move(contents));
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
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser) {
  auto contents = std::make_unique<views::View>();
  auto* layout =
      contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kContentSpacing));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  auto details = std::make_unique<views::View>();
  auto* details_layout =
      details->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(),
          kDetailSpacing));
  details_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);

  contents->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_REQUESTER,
                               SanitizeLabel(request.client.name)),
      TextPrimary(), false));
  details->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_CLIENT,
                                 SanitizeLabel(request.client.name),
                                 SanitizeLabel(request.client.version)),
      TextPrimary(), false));

  details->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(
          IDS_DAO_MCP_APPROVAL_TIME,
          base::TimeFormatShortDateAndTimeWithTimeZone(request.requested_at)),
      TextSecondary(), true));
  if (!request.background_space) {
    std::u16string window_title = browser
                                      ? WindowMetadataController::From(browser)
                                            ->GetWindowTitleForCurrentTab(false)
                                      : std::u16string();
    window_title = SanitizeLabel(std::move(window_title));
    details->AddChildView(CreateLabel(
        l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_WINDOW, window_title),
        TextSecondary(), false));
  }
  if (request.tab_target) {
    auto* target = contents->AddChildView(std::make_unique<views::View>());
    auto* target_layout =
        target->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(),
            kDetailSpacing));
    target_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    target->AddChildView(CreateLabel(
        l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_TAB,
                                 SanitizeLabel(request.tab_title)),
        TextPrimary(), false));
    const GURL url(request.tab_target->url);
    std::string site = url.spec();
    if (url.SchemeIsHTTPOrHTTPS()) {
      site = url.host();
      if (url.has_port()) {
        site += ':';
        site += url.port();
      }
    }
    target->AddChildView(
        CreateLabel(SanitizeLabel(site), TextSecondary(), false));
    details->AddChildView(CreateLabel(
        l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_TAB_URL,
                                 SanitizeLabel(request.tab_target->url)),
        TextSecondary(), false));
  }

  if (!request.tab_choices.empty()) {
    SetButtonEnabled(ui::mojom::DialogButton::kOk, false);
    auto* scroll = contents->AddChildView(std::make_unique<views::ScrollView>());
    scroll->ClipHeightTo(0, 200);
    auto list = std::make_unique<views::View>();
    list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(),
        kDetailSpacing));
    for (const auto& choice : request.tab_choices) {
      auto* radio = list->AddChildView(
          std::make_unique<views::RadioButton>(SanitizeLabel(choice.title), 1));
      radio->SetCallback(base::BindRepeating(
          [](DaoMcpApprovalDialog* dialog) {
            dialog->SetButtonEnabled(ui::mojom::DialogButton::kOk, true);
          },
          base::Unretained(this)));
      list->AddChildView(CreateLabel(SanitizeLabel(choice.target.url),
                                     TextSecondary(), false));
      choices_.emplace_back(radio, choice.target.tab_id);
    }
    scroll->SetContents(std::move(list));
  }

  auto* reason_scroll =
      contents->AddChildView(std::make_unique<views::ScrollView>());
  reason_scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  reason_scroll->ClipHeightTo(0, 120);
  reason_scroll
      ->SetContents(CreateLabel(
          l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_REASON,
                                   SanitizeLabel(request.reason)),
          TextPrimary(), true))
      ->SetSubpixelRenderingEnabled(false);
  contents->AddChildView(CreateLabel(
      l10n_util::GetStringUTF16(
          request.background_space ? IDS_DAO_MCP_APPROVAL_SPACE_EXPLANATION
          : (request.tab_target || !request.tab_choices.empty())
              ? IDS_DAO_MCP_APPROVAL_TAB_EXPLANATION
              : IDS_DAO_MCP_APPROVAL_EXPLANATION),
      TextSecondary(), true));

  std::u16string profile_name;
  if (browser && browser->GetProfile()) {
    if (ProfileAttributesEntry* entry =
            GetProfileAttributesFromProfile(browser->GetProfile())) {
      profile_name = entry->GetLocalProfileName();
    }
    if (profile_name.empty()) {
      profile_name = browser->GetProfile()->GetPath().BaseName().AsUTF16Unsafe();
    }
  }
  const bool has_multiple_profiles =
      g_browser_process->profile_manager() &&
      g_browser_process->profile_manager()
              ->GetProfileAttributesStorage()
              .GetNumberOfProfiles() > 1;
  auto* profile_container = has_multiple_profiles ? contents.get() : details.get();
  profile_container->AddChildView(CreateLabel(
      l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_PROFILE,
                               SanitizeLabel(std::move(profile_name))),
      TextSecondary(), false));

  if (request.background_space) {
    remember_client_name_ = request.client.name;
    auto* checkbox = contents->AddChildView(std::make_unique<views::Checkbox>(
        l10n_util::GetStringFUTF16(IDS_DAO_MCP_APPROVAL_SPACE_REMEMBER,
                                   SanitizeLabel(request.client.name))));
    checkbox->SetCallback(base::BindRepeating(
        [](DaoMcpApprovalDialog* dialog, views::Checkbox* checkbox) {
          dialog->remember_ = checkbox->GetChecked();
          dialog->SetButtonLabel(
              ui::mojom::DialogButton::kOk,
              l10n_util::GetStringUTF16(
                  dialog->remember_
                      ? IDS_DAO_MCP_APPROVAL_SPACE_ALLOW_ALWAYS
                      : IDS_DAO_MCP_APPROVAL_SPACE_ALLOW_ONE_HOUR));
        },
        base::Unretained(this), base::Unretained(checkbox)));
  }

  auto* details_row = contents->AddChildView(std::make_unique<views::View>());
  details_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto* details_button = details_row->AddChildView(CreateDaoDialogButton(
      views::Button::PressedCallback(),
      l10n_util::GetStringUTF16(IDS_DAO_MCP_APPROVAL_SHOW_DETAILS), std::nullopt,
      ui::ButtonStyle::kText));
  auto* details_view = contents->AddChildView(std::move(details));
  details_view->SetVisible(false);
  details_button->GetViewAccessibility().SetIsCollapsed();
  details_button->SetCallback(base::BindRepeating(
      [](views::View* contents, views::View* details, views::MdTextButton* button) {
        const bool expanded = !details->GetVisible();
        details->SetVisible(expanded);
        button->SetText(l10n_util::GetStringUTF16(
            expanded ? IDS_DAO_MCP_APPROVAL_HIDE_DETAILS
                     : IDS_DAO_MCP_APPROVAL_SHOW_DETAILS));
        if (expanded) {
          button->GetViewAccessibility().SetIsExpanded();
        } else {
          button->GetViewAccessibility().SetIsCollapsed();
        }
        contents->SetPreferredSize(std::nullopt);
        contents->SetPreferredSize(
            gfx::Size(kDialogWidth, contents->GetHeightForWidth(kDialogWidth)));
        if (views::Widget* widget = contents->GetWidget()) {
          widget->SetSize(widget->non_client_view()->GetPreferredSize());
        }
      },
      base::Unretained(contents.get()), base::Unretained(details_view),
      base::Unretained(details_button)));
  contents->SetPreferredSize(
      gfx::Size(kDialogWidth, contents->GetHeightForWidth(kDialogWidth)));
  ConfigureDaoSystemDialogContent(contents.get());
  return contents;
}

void DaoMcpApprovalDialog::Resolve(bool allowed) {
  if (consent_callback_) {
    std::move(consent_callback_).Run(allowed, allowed && remember_);
    return;
  }
  if (!callback_) {
    return;
  }
  if (allowed && remember_ && !remember_client_name_.empty()) {
    if (PrefService* local_state = g_browser_process->local_state()) {
      ScopedListPrefUpdate update(local_state,
                                  prefs::kDaoAgentSpaceAllowedClients);
      if (!update->contains(remember_client_name_)) {
        update->Append(remember_client_name_);
      }
    }
  }
  std::optional<std::string> result;
  if (allowed) {
    if (explicit_tab_id_) {
      result = explicit_tab_id_;
    } else if (choices_.empty()) {
      result = std::string();
    } else {
      for (const auto& [radio, tab_id] : choices_) {
        if (radio->GetChecked()) {
          result = tab_id;
          break;
        }
      }
    }
  }
  std::move(callback_).Run(std::move(result));
}

// static
DaoMcpApprovalDialogController* DaoMcpApprovalDialogController::Get() {
  static base::NoDestructor<DaoMcpApprovalDialogController> instance;
  return instance.get();
}

DaoMcpApprovalDialogController::DaoMcpApprovalDialogController() = default;
DaoMcpApprovalDialogController::~DaoMcpApprovalDialogController() = default;

DaoMcpApprovalDialogController::PendingRequest::PendingRequest(
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser,
    std::string connection_id,
    base::OnceCallback<void(std::optional<std::string>)> callback)
    : approval(request),
      browser(browser->GetWeakPtr()),
      connection_id(std::move(connection_id)),
      callback(std::move(callback)) {}

DaoMcpApprovalDialogController::PendingRequest::~PendingRequest() = default;
DaoMcpApprovalDialogController::PendingRequest::PendingRequest(
    PendingRequest&&) = default;
DaoMcpApprovalDialogController::PendingRequest&
DaoMcpApprovalDialogController::PendingRequest::operator=(PendingRequest&&) =
    default;

void DaoMcpApprovalDialogController::RequestApproval(
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser,
    std::string_view connection_id,
    base::OnceCallback<void(bool)> callback) {
  if (!IsEligibleApprovalBrowser(browser)) {
    std::move(callback).Run(false);
    return;
  }
  pending_requests_.emplace_back(
      request, browser, std::string(connection_id),
      base::BindOnce(
          [](base::OnceCallback<void(bool)> callback,
             std::optional<std::string> result) {
            std::move(callback).Run(result.has_value());
          },
          std::move(callback)));
  ShowNextApproval();
}

void DaoMcpApprovalDialogController::RequestTabAccess(
    const DaoMcpApprovalRequest& request,
    BrowserWindowInterface* browser,
    std::string_view connection_id,
    base::OnceCallback<void(std::optional<std::string>)> callback) {
  if (!IsEligibleApprovalBrowser(browser) ||
      (!request.tab_target && request.tab_choices.empty())) {
    std::move(callback).Run(std::nullopt);
    return;
  }

  pending_requests_.emplace_back(request, browser, std::string(connection_id),
                                 std::move(callback));
  ShowNextApproval();
}

void DaoMcpApprovalDialogController::ShowNextApproval() {
  if (pending_dialog_ || pending_requests_.empty()) {
    return;
  }

  PendingRequest request = std::move(pending_requests_.front());
  pending_requests_.pop_front();
  BrowserWindowInterface* browser = request.browser.get();
  if (!IsEligibleApprovalBrowser(browser)) {
    std::move(request.callback).Run(std::nullopt);
    ShowNextApproval();
    return;
  }

  auto dialog = std::make_unique<DaoMcpApprovalDialog>(
      request.approval, browser,
      base::BindOnce(&DaoMcpApprovalDialogController::OnDialogResult,
                     base::Unretained(this), request.connection_id,
                     std::move(request.callback)));
  pending_dialog_ = dialog->GetWeakPtr();
  pending_connection_id_ = std::move(request.connection_id);
  browser->GetWindow()->Activate();
  views::Widget* widget = constrained_window::CreateBrowserModalDialogViews(
      std::move(dialog), browser->GetWindow()->GetNativeWindow());
  widget->Show();
}

void DaoMcpApprovalDialogController::CancelApproval(
    std::string_view connection_id) {
  if (connection_id == pending_connection_id_) {
    if (pending_dialog_) {
      pending_dialog_->DismissWithoutResult();
    }
    pending_dialog_.reset();
    pending_connection_id_.clear();
    ShowNextApproval();
    return;
  }

  for (auto request = pending_requests_.begin();
       request != pending_requests_.end(); ++request) {
    if (request->connection_id == connection_id) {
      pending_requests_.erase(request);
      return;
    }
  }
}

void DaoMcpApprovalDialogController::OnDialogResult(
    std::string connection_id,
    base::OnceCallback<void(std::optional<std::string>)> callback,
    std::optional<std::string> tab_id) {
  if (connection_id != pending_connection_id_) {
    return;
  }
  pending_dialog_.reset();
  pending_connection_id_.clear();
  std::move(callback).Run(std::move(tab_id));
  ShowNextApproval();
}

}  // namespace dao
