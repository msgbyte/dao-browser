// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_SERVICE_H_
#define DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_SERVICE_H_

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/observer_list.h"
#include "base/observer_list_types.h"
#include "base/scoped_multi_source_observation.h"
#include "base/scoped_observation.h"
#include "base/sequence_checker.h"
#include "base/supports_user_data.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "content/public/browser/download_manager.h"
#include "content/public/common/javascript_dialog_type.h"
#include "dao/browser/agent_space/dao_agent_space.h"
#include "dao/browser/agent_space/dao_hidden_browser.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

class BrowserWindowInterface;
class GlobalBrowserCollection;
class Profile;

namespace content {
class WebContents;
}  // namespace content

namespace dao {

class DaoAgentScriptRun;
struct DaoBrowserToolDefinition;

// Who is calling a space tool. `key` scopes cancellation to one Agent UI or
// one external connection; spaces themselves are shared by every caller of
// the same client type so short-lived CLI connections can resume them.
struct DaoAgentSpaceClient {
  DaoToolClient type = DaoToolClient::kDaoAgent;
  std::string key;
  std::string label;
};

// Owns the Agent Spaces of one profile and routes browser tools into them.
class DaoAgentSpaceService : public base::SupportsUserData::Data,
                             public BrowserCollectionObserver,
                             public content::DownloadManager::Observer {
 public:
  using ResultCallback = base::OnceCallback<void(DaoBrowserToolResult)>;

  class Observer : public base::CheckedObserver {
   public:
    virtual void OnAgentSpacesChanged() = 0;
  };

  static constexpr size_t kMaxActiveSpaces = 4;
  static constexpr int kMaxTabsPerSpace = 8;
  static constexpr size_t kMaxClosedRecords = 8;

  explicit DaoAgentSpaceService(Profile* profile);
  ~DaoAgentSpaceService() override;

  DaoAgentSpaceService(const DaoAgentSpaceService&) = delete;
  DaoAgentSpaceService& operator=(const DaoAgentSpaceService&) = delete;

  // Returns nullptr for off-the-record and guest profiles.
  static DaoAgentSpaceService* GetForProfile(Profile* profile);
  // Stops every open space of `client` in all loaded profiles, e.g. when the
  // user turns off local agent access.
  static void StopSpacesForClient(DaoToolClient client);
  static bool IsAgentSpaceBrowser(const BrowserWindowInterface* browser);
  // True while an Agent Space window is registered and not on screen.
  static bool IsHiddenAgentSpaceBrowser(BrowserWindowInterface* browser);
  static DaoAgentSpace* FindSpaceForBrowser(
      const BrowserWindowInterface* browser);

  static bool IsSpaceTool(std::string_view name);
  // True for space tools and for browser tools that name a space_id.
  static bool ShouldRoute(std::string_view name,
                          const base::DictValue& arguments);

  // Executes a space tool, or a browser tool inside the named space.
  void ExecuteTool(const DaoAgentSpaceClient& client,
                   DaoBrowserToolCall call,
                   ResultCallback callback);
  void CancelTool(const DaoAgentSpaceClient& client,
                  std::string_view request_id);
  void CancelClientTools(std::string_view client_key);

  // Hands a running space to the user when a page needs something only they
  // can answer, such as a permission prompt or a file picker.
  void HandOffForPage(DaoAgentSpace* space,
                      content::WebContents* contents,
                      DaoAgentSpaceHandoffReason reason);
  // Stops in-flight calls that are blocked behind a page's confirm or prompt.
  void OnSpaceDialogOpened(DaoAgentSpace* space);

  // User controls, driven by the sidebar and the task window.
  bool TakeOver(std::string_view space_id);
  bool HandBack(std::string_view space_id);
  bool Stop(std::string_view space_id);
  bool ShowWindow(std::string_view space_id, std::string_view tab_id = {});
  bool HideWindow(std::string_view space_id);
  bool Dismiss(std::string_view space_id);

  DaoAgentSpace* FindSpace(std::string_view space_id);
  std::vector<DaoAgentSpace*> GetSpaces();
  base::ListValue SerializeSpaces();
  size_t GetActiveSpaceCount() const;

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);
  // Called by spaces; coalesced so frequent steps do not flood the UI.
  void NotifySpaceChanged();

  // BrowserCollectionObserver:
  void OnBrowserClosed(BrowserWindowInterface* browser) override;

  // content::DownloadManager::Observer:
  void OnDownloadCreated(content::DownloadManager* manager,
                         download::DownloadItem* item) override;
  void ManagerGoingDown(content::DownloadManager* manager) override;

  void SetIdleTimeoutForTesting(base::TimeDelta timeout);

 private:
  friend class DaoAgentSpace;

  static void RegisterSpaceBrowser(const BrowserWindowInterface* browser,
                                   DaoAgentSpace* space);
  static DaoAgentSpace* FindSpaceForContents(content::WebContents* contents);
  static void OnHiddenDialogOpened(content::WebContents* contents,
                                   content::JavaScriptDialogType type,
                                   const std::u16string& message,
                                   const std::u16string& default_prompt);
  static void OnHiddenDialogClosed(content::WebContents* contents);
  static void OnHiddenPageNeed(content::WebContents* contents,
                               DaoHiddenPageNeed need);
  static base::FilePath GetHiddenDownloadDirectory(
      content::WebContents* contents);
  static void UnregisterSpaceBrowser(const BrowserWindowInterface* browser);

  struct PendingCall;
  struct PendingHelp;

  void ExecuteSpaceTool(const DaoAgentSpaceClient& client,
                        const DaoBrowserToolDefinition& definition,
                        DaoBrowserToolCall call,
                        ResultCallback callback);
  void CreateSpace(const DaoAgentSpaceClient& client,
                   const base::DictValue& arguments,
                   ResultCallback callback);
  void ListSpaces(const DaoAgentSpaceClient& client, ResultCallback callback);
  void SetStatus(const DaoAgentSpaceClient& client,
                 const base::DictValue& arguments,
                 ResultCallback callback);
  void RequestUserHelp(const DaoAgentSpaceClient& client,
                       std::string request_id,
                       const base::DictValue& arguments,
                       ResultCallback callback);
  void FinishSpace(const DaoAgentSpaceClient& client,
                   const base::DictValue& arguments,
                   ResultCallback callback);
  void HandleDialog(const DaoAgentSpaceClient& client,
                    const base::DictValue& arguments,
                    ResultCallback callback);
  void GetSiteSkills(const base::DictValue& arguments, ResultCallback callback);
  void RunScript(const DaoAgentSpaceClient& client,
                 std::string request_id,
                 const base::DictValue& arguments,
                 ResultCallback callback);
  void OnScriptDone(std::string run_id,
                    ResultCallback callback,
                    DaoBrowserToolResult result);
  // `waited_for_commit` is set once a fresh tab's first navigation had its
  // chance to commit, so the call never waits twice.
  void ExecuteInSpace(const DaoAgentSpaceClient& client,
                      DaoBrowserToolCall call,
                      ResultCallback callback,
                      bool waited_for_commit = false);
  void OnSpaceToolComplete(std::string internal_id,
                           DaoBrowserToolResult result);
  void OnHelpTimeout(std::string space_id);
  void ResolveHelp(std::string_view space_id, DaoBrowserToolResult result);

  // Resolves an accessible, open space or fills `error`.
  DaoAgentSpace* ResolveSpace(const DaoAgentSpaceClient& client,
                              const base::DictValue& arguments,
                              DaoToolError* error);
  void CancelSpaceCalls(std::string_view space_id, const DaoToolError& error);
  void CloseSpace(DaoAgentSpace* space,
                  DaoAgentSpaceState state,
                  std::string summary,
                  const std::vector<std::string>& keep_tab_ids,
                  bool window_closing = false);
  void PruneClosedRecords();
  // Keeps closed spaces across restarts.
  void SaveHistory();
  void LoadHistory();
  void CheckIdleSpaces();
  // Tells the user a space needs them: a toast in their window and a system
  // notification that opens the task, since they may be in another app.
  void AlertUser(DaoAgentSpace* space, const std::u16string& message);
  void CloseUserAlert(std::string_view space_id);
  void FlushNotifications();

  const raw_ptr<Profile> profile_;
  std::map<std::string, std::unique_ptr<DaoAgentSpace>, std::less<>> spaces_;
  std::vector<std::string> space_order_;
  std::map<std::string, std::unique_ptr<PendingCall>, std::less<>> calls_;
  std::map<std::string, std::unique_ptr<PendingHelp>, std::less<>> helps_;
  // Keyed by "script:<client key>:<request id>".
  std::map<std::string, std::unique_ptr<DaoAgentScriptRun>, std::less<>>
      scripts_;
  int next_space_number_ = 1;
  base::TimeDelta idle_timeout_;
  base::RepeatingTimer idle_timer_;
  base::OneShotTimer notify_timer_;
  base::ObserverList<Observer> observers_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_observation_{this};
  // The profile's downloads, plus those of clean spaces' temporary profiles.
  base::ScopedMultiSourceObservation<content::DownloadManager,
                                     content::DownloadManager::Observer>
      download_manager_observations_{this};
  SEQUENCE_CHECKER(sequence_checker_);
  base::WeakPtrFactory<DaoAgentSpaceService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_SPACE_DAO_AGENT_SPACE_SERVICE_H_
