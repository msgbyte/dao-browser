// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_MCP_APPROVAL_DIALOG_H_
#define DAO_BROWSER_UI_VIEWS_DAO_MCP_APPROVAL_DIALOG_H_

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "dao/browser/mcp/dao_mcp_service.h"
#include "ui/views/window/dialog_delegate.h"

class BrowserWindowInterface;
namespace views {
class Label;
class RadioButton;
}

namespace dao {

class DaoMcpApprovalDialog final : public views::DialogDelegate {
 public:
  using ConsentCallback = base::OnceCallback<void(bool allowed, bool remember)>;

  // Asks before the built-in Dao Agent starts its first background task,
  // since pages in a task window use the user's sign-ins. "Don't ask again"
  // maps to the "start background tasks without asking" AI setting. Runs
  // `callback` with false when `browser` cannot show the dialog.
  static void ShowAgentSpaceConsent(BrowserWindowInterface* browser,
                                    const std::u16string& task_name,
                                    ConsentCallback callback);

  DaoMcpApprovalDialog(const DaoMcpApprovalRequest& request,
                       BrowserWindowInterface* browser,
                       base::OnceCallback<void(bool)> callback);
  DaoMcpApprovalDialog(
      const DaoMcpApprovalRequest& request,
      BrowserWindowInterface* browser,
      base::OnceCallback<void(std::optional<std::string>)> callback);
  DaoMcpApprovalDialog(const std::u16string& task_name,
                       ConsentCallback callback);
  ~DaoMcpApprovalDialog() override;

  DaoMcpApprovalDialog(const DaoMcpApprovalDialog&) = delete;
  DaoMcpApprovalDialog& operator=(const DaoMcpApprovalDialog&) = delete;

  void DismissWithoutResult();
  base::WeakPtr<DaoMcpApprovalDialog> GetWeakPtr();

 private:
  std::unique_ptr<views::View> BuildContents(
      const DaoMcpApprovalRequest& request,
      BrowserWindowInterface* browser);
  void FilterTabChoices(std::u16string_view query);
  void Resolve(bool allowed);

  struct TabChoice {
    raw_ptr<views::RadioButton> radio;
    raw_ptr<views::Label> url;
    std::string tab_id;
    std::u16string search_text;
  };

  base::OnceCallback<void(std::optional<std::string>)> callback_;
  std::optional<std::string> explicit_tab_id_;
  std::vector<TabChoice> choices_;
  raw_ptr<views::Label> no_matching_tabs_ = nullptr;
  base::CallbackListSubscription search_subscription_;
  ConsentCallback consent_callback_;
  // Background-space requests can be remembered for the reported client.
  std::string remember_client_name_;
  bool remember_ = false;
  base::WeakPtrFactory<DaoMcpApprovalDialog> weak_factory_{this};
};

class DaoMcpApprovalDialogController final : public DaoMcpApprovalDelegate {
 public:
  static DaoMcpApprovalDialogController* Get();

  DaoMcpApprovalDialogController(const DaoMcpApprovalDialogController&) =
      delete;
  DaoMcpApprovalDialogController& operator=(
      const DaoMcpApprovalDialogController&) = delete;

  void RequestApproval(const DaoMcpApprovalRequest& request,
                       BrowserWindowInterface* browser,
                       std::string_view connection_id,
                       base::OnceCallback<void(bool)> callback) override;
  void RequestTabAccess(
      const DaoMcpApprovalRequest& request,
      BrowserWindowInterface* browser,
      std::string_view approval_id,
      base::OnceCallback<void(std::optional<std::string>)> callback) override;
  void CancelApproval(std::string_view connection_id) override;

  size_t queued_approval_count_for_testing() const {
    return pending_requests_.size();
  }

 private:
  friend class base::NoDestructor<DaoMcpApprovalDialogController>;

  DaoMcpApprovalDialogController();
  ~DaoMcpApprovalDialogController() override;

  struct PendingRequest {
    PendingRequest(
        const DaoMcpApprovalRequest& request,
        BrowserWindowInterface* browser,
        std::string connection_id,
        base::OnceCallback<void(std::optional<std::string>)> callback);
    ~PendingRequest();
    PendingRequest(PendingRequest&&);
    PendingRequest& operator=(PendingRequest&&);

    PendingRequest(const PendingRequest&) = delete;
    PendingRequest& operator=(const PendingRequest&) = delete;

    DaoMcpApprovalRequest approval;
    base::WeakPtr<BrowserWindowInterface> browser;
    std::string connection_id;
    base::OnceCallback<void(std::optional<std::string>)> callback;
  };

  void ShowNextApproval();
  void OnDialogResult(
      std::string connection_id,
      base::OnceCallback<void(std::optional<std::string>)> callback,
      std::optional<std::string> tab_id);

  std::deque<PendingRequest> pending_requests_;
  std::string pending_connection_id_;
  base::WeakPtr<DaoMcpApprovalDialog> pending_dialog_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_MCP_APPROVAL_DIALOG_H_
