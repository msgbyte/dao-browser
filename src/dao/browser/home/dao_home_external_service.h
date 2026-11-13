// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_EXTERNAL_SERVICE_H_
#define DAO_BROWSER_HOME_DAO_HOME_EXTERNAL_SERVICE_H_

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/threading/sequence_bound.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "dao/browser/home/dao_home_types.h"
#include "dao/browser/local_ipc/dao_local_ipc_transport.h"

class BrowserWindowInterface;
class Profile;
namespace content {
class WebContents;
}
namespace views {
class Widget;
}

namespace dao {

class DaoHomeUI;
class DaoLocalIpcRuntimeFiles;

// Independent, Profile-scoped Home editing. This service never dispatches MCP
// tools or starts an internal Agent turn.
class DaoHomeExternalService {
 public:
  using Reply = base::OnceCallback<void(base::DictValue)>;
  using ApprovalForTesting = base::RepeatingCallback<bool(Profile*)>;

  static DaoHomeExternalService* Get();
  void Initialize(base::FilePath user_data_dir);
  void Shutdown();

  base::Value GetContext(content::WebContents* contents) const;
  base::Value GetEditor(Profile* profile) const;
  void StopForProfile(Profile* profile);
  bool CanAccessDraft(content::WebContents* contents,
                      const std::string& draft_id) const;
  void StopForContents(content::WebContents* contents);

  void SetApprovalForTesting(ApprovalForTesting approval);
  void ExecuteForTesting(std::string method,
                         base::DictValue params,
                         Reply reply);

 private:
  friend class base::NoDestructor<DaoHomeExternalService>;
  struct Session;
  struct Connection;
  struct Approval;

  DaoHomeExternalService();
  ~DaoHomeExternalService();
  void OnRuntimePrepared(base::expected<void, DaoLocalIpcError> result);
  void OnListening(base::expected<uint16_t, int> result);
  void OnRuntimePublished(base::expected<void, DaoLocalIpcError> result);
  void OnAccepted(uint64_t connection, std::optional<base::ProcessId> pid);
  void OnRequest(uint64_t connection,
                 DaoLocalIpcRequest request,
                 size_t wire_bytes);
  void OnDisconnected(uint64_t connection);
  void Dispatch(uint64_t connection,
                std::string client,
                std::string method,
                base::DictValue params,
                Reply reply);
  void OpenSession(uint64_t connection,
                   std::string client,
                   base::DictValue params,
                   Reply reply);
  void FinishApproval(bool allowed);
  void FinishApprovalIfCurrent(uint64_t generation, bool allowed);
  void StopSession(const std::string& token, bool revoked = true);
  std::string SessionErrorCode(const std::string& token) const;
  Session* FindSession(const std::string& token);
  Session* FindOwner(content::WebContents* contents) const;
  Session* FindEditor(Profile* profile) const;
  void SendReply(uint64_t connection, std::string id, base::DictValue result);
  void GetStatus(Session& session, base::DictValue params, Reply reply);
  void Stage(Session& session, base::DictValue params, Reply reply);
  void StartPreview(Session& session,
                    std::string draft_id,
                    Reply reply,
                    std::optional<std::string> connector_id = std::nullopt);
  bool EnsurePreviewWindow(Session& session);
  void ContinuePreview(std::string token);
  void OnDraftLoaded(std::string token,
                     base::expected<HomeDraft, HomeError> result);
  void OnSourceTested(std::string token,
                      std::string connector_id,
                      base::Value result);
  void OnPreviewed(std::string token, base::Value result);
  void FinishPreview(Session& session, base::DictValue result);
  void InvalidatePreview(const std::string& token);
  void Publish(Session& session, base::DictValue params, Reply reply);
  void Rollback(Session& session, base::DictValue params, Reply reply);
  void Screenshot(Session& session, Reply reply);
  void NotifyOwner(Session& session);
  void NotifyEditors(Profile* profile);

  base::FilePath user_data_dir_;
  scoped_refptr<DaoLocalIpcRuntimeFiles> runtime_files_;
  scoped_refptr<base::SequencedTaskRunner> runtime_runner_;
  base::SequenceBound<DaoLocalIpcTransport> transport_;
  std::map<uint64_t, std::unique_ptr<Connection>> connections_;
  std::map<std::string, std::unique_ptr<Session>> sessions_;
  std::map<std::string, base::Time> revoked_sessions_;
  std::unique_ptr<Approval> approval_;
  uint64_t approval_generation_ = 0;
  ApprovalForTesting approval_for_testing_;
  bool running_ = false;
  base::WeakPtrFactory<DaoHomeExternalService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_EXTERNAL_SERVICE_H_
