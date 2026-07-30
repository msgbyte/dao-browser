// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_TAB_TOOLS_H_
#define DAO_BROWSER_AUTOMATION_DAO_TAB_TOOLS_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace dao {

class DaoBrowserAutomationSession;

class DaoTabTools {
 public:
  using ResultCallback = base::OnceCallback<void(DaoBrowserToolResult)>;

  DaoTabTools();
  ~DaoTabTools();

  static bool Handles(std::string_view name);

  void Execute(std::string request_id,
               DaoBrowserAutomationSession* session,
               DaoToolClient client,
               std::string_view name,
               const base::DictValue& arguments,
               ResultCallback callback);
  bool Cancel(std::string_view request_id, DaoToolError error);

 private:
  class PendingClose;

  DaoBrowserToolResult ExecuteSync(DaoBrowserAutomationSession* session,
                                   DaoToolClient client,
                                   std::string_view name,
                                   const base::DictValue& arguments);
  void ExecuteClose(std::string request_id,
                    DaoBrowserAutomationSession* session,
                    DaoToolClient client,
                    const base::DictValue& arguments,
                    ResultCallback callback);
  void CompleteClose(std::string request_id, DaoBrowserToolResult result);

  std::map<std::string, std::unique_ptr<PendingClose>, std::less<>>
      pending_closes_;
  base::WeakPtrFactory<DaoTabTools> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_TAB_TOOLS_H_
