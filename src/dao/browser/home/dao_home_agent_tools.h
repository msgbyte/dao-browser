// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_AGENT_TOOLS_H_
#define DAO_BROWSER_HOME_DAO_HOME_AGENT_TOOLS_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_types.h"

namespace dao {

class DaoHomeProjectService;

// Dispatches the contextual Home tool pack against one Profile service. The
// owning WebUI handler is responsible for revalidating the exact active Home
// tab before every call; this class revalidates project and revision state.
class DaoHomeAgentTools {
 public:
  using Callback = base::OnceCallback<void(base::Value)>;
  using OwnerValidator = base::RepeatingCallback<bool()>;
  using ConnectorRunner = base::RepeatingCallback<void(std::string draft_id,
                                                       std::string connector_id,
                                                       base::Value input,
                                                       Callback callback)>;
  using PreviewRunner =
      base::RepeatingCallback<void(std::string draft_id,
                                   std::string entry,
                                   HomePreviewRequirements requirements,
                                   Callback callback)>;

  explicit DaoHomeAgentTools(DaoHomeProjectService* service);
  ~DaoHomeAgentTools();

  DaoHomeAgentTools(const DaoHomeAgentTools&) = delete;
  DaoHomeAgentTools& operator=(const DaoHomeAgentTools&) = delete;

  void Execute(std::string name, base::DictValue arguments, Callback callback);
  void Execute(std::string name,
               base::DictValue arguments,
               scoped_refptr<DaoHomeMutationLease> authorization,
               scoped_refptr<DaoHomeMutationLease> turn_authorization,
               OwnerValidator owner_validator,
               std::string agent_turn_id,
               Callback callback);
  void SetConnectorRunner(ConnectorRunner connector_runner);
  void SetPreviewRunner(PreviewRunner preview_runner);

 private:
  void ExecuteWithSnapshot(
      std::string name,
      base::DictValue arguments,
      scoped_refptr<DaoHomeMutationLease> authorization,
      scoped_refptr<DaoHomeMutationLease> turn_authorization,
      OwnerValidator owner_validator,
      std::string agent_turn_id,
      Callback callback,
      HomeSnapshot snapshot);
  void RunPreview(HomeDraft draft,
                  HomePreviewRequirements requirements,
                  Callback callback,
                  scoped_refptr<DaoHomeMutationLease> authorization,
                  OwnerValidator owner_validator,
                  std::string agent_turn_id);

  raw_ptr<DaoHomeProjectService> service_;
  ConnectorRunner connector_runner_;
  PreviewRunner preview_runner_;
  base::WeakPtrFactory<DaoHomeAgentTools> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_AGENT_TOOLS_H_
