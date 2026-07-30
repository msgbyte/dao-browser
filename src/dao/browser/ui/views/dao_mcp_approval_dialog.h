// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_UI_VIEWS_DAO_MCP_APPROVAL_DIALOG_H_
#define DAO_BROWSER_UI_VIEWS_DAO_MCP_APPROVAL_DIALOG_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "dao/browser/mcp/dao_mcp_service.h"
#include "ui/views/window/dialog_delegate.h"

class Browser;

namespace dao {

class DaoMcpApprovalDialog final : public views::DialogDelegate {
 public:
  DaoMcpApprovalDialog(const DaoMcpClientInfo& client,
                       Browser* browser,
                       base::OnceCallback<void(bool)> callback);
  ~DaoMcpApprovalDialog() override;

  DaoMcpApprovalDialog(const DaoMcpApprovalDialog&) = delete;
  DaoMcpApprovalDialog& operator=(const DaoMcpApprovalDialog&) = delete;

  void DismissWithoutResult();
  base::WeakPtr<DaoMcpApprovalDialog> GetWeakPtr();

 private:
  std::unique_ptr<views::View> BuildContents(const DaoMcpClientInfo& client,
                                             Browser* browser);
  void Resolve(bool allowed);

  base::OnceCallback<void(bool)> callback_;
  base::WeakPtrFactory<DaoMcpApprovalDialog> weak_factory_{this};
};

class DaoMcpApprovalDialogController final : public DaoMcpApprovalDelegate {
 public:
  static DaoMcpApprovalDialogController* Get();

  DaoMcpApprovalDialogController(const DaoMcpApprovalDialogController&) =
      delete;
  DaoMcpApprovalDialogController& operator=(
      const DaoMcpApprovalDialogController&) = delete;

  void RequestApproval(const DaoMcpClientInfo& client,
                       Browser* browser,
                       std::string_view connection_id,
                       base::OnceCallback<void(bool)> callback) override;
  void CancelApproval(std::string_view connection_id) override;

 private:
  friend class base::NoDestructor<DaoMcpApprovalDialogController>;

  DaoMcpApprovalDialogController();
  ~DaoMcpApprovalDialogController() override;

  void OnDialogResult(std::string connection_id,
                      base::OnceCallback<void(bool)> callback,
                      bool allowed);

  std::string pending_connection_id_;
  base::WeakPtr<DaoMcpApprovalDialog> pending_dialog_;
};

}  // namespace dao

#endif  // DAO_BROWSER_UI_VIEWS_DAO_MCP_APPROVAL_DIALOG_H_
