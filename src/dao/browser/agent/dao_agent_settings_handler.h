// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_SETTINGS_HANDLER_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_SETTINGS_HANDLER_H_

#include <cstddef>
#include <string>
#include <string_view>

#include "base/containers/flat_map.h"
#include "base/functional/callback.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_ui_message_handler.h"

class PrefService;
class Profile;

namespace dao {

inline constexpr int kDaoAgentSettingsMigrationVersion = 2;
inline constexpr size_t kMaxDaoAgentUsageTools = 128;
inline constexpr size_t kMaxDaoAgentUsageToolNameBytes = 128;
inline constexpr char kDaoAgentMemoryEnabledSetting[] =
    "dao_agent_memory_enabled";
inline constexpr char kDaoDreamEnabledSetting[] = "dao_dream_enabled";
inline constexpr char kDaoDreamDebugSetting[] = "dao_dream_debug";
inline constexpr char kDaoDreamExcludedDomainsSetting[] =
    "dao_dream_excluded_domains";

bool IsManagedDaoAgentSetting(std::string_view key);
bool IsDaoTabClassificationConfigured(PrefService* prefs);
base::DictValue BuildDaoAgentSettingsSnapshot(PrefService* prefs);
base::DictValue BuildDaoAgentUsageStats(PrefService* prefs);
bool MigrateLegacyDaoAgentUsageStats(PrefService* prefs,
                                     const base::Value* legacy_value);
void RecordDaoAgentApiUsage(PrefService* prefs, double api_calls,
                            double prompt_tokens, double completion_tokens,
                            double estimated_cost);
void RecordDaoAgentToolUsage(PrefService* prefs, std::string_view tool_name);
void ResetDaoAgentUsageStats(PrefService* prefs, base::Time last_reset);
bool SetDaoAgentSetting(PrefService* prefs, std::string_view key,
                        const base::Value& value);
base::DictValue
MigrateLegacyDaoAgentSettings(PrefService* prefs,
                              const base::DictValue& legacy_values);

// Shared by dao://agent and dao://settings so both origins observe one
// Profile-scoped source of truth.
class DaoAgentSettingsHandler : public content::WebUIMessageHandler {
public:
  DaoAgentSettingsHandler();
  DaoAgentSettingsHandler(const DaoAgentSettingsHandler&) = delete;
  DaoAgentSettingsHandler& operator=(const DaoAgentSettingsHandler&) = delete;
  ~DaoAgentSettingsHandler() override;

  // content::WebUIMessageHandler:
  void RegisterMessages() override;
  void OnJavascriptAllowed() override;
  void OnJavascriptDisallowed() override;

  // Reuse the resident Agent's configured model without opening a chat.
  static void ClassifyTabs(Profile* profile, base::DictValue request,
                           base::OnceCallback<void(base::DictValue)> callback);

private:
  using ConnectionTestCallback = base::OnceCallback<void(base::DictValue)>;

  void HandleGetSettings(const base::ListValue& args);
  void HandleMigrateLegacySettings(const base::ListValue& args);
  void HandleSetSetting(const base::ListValue& args);
  void HandleGetMemorySummary(const base::ListValue& args);
  void HandleClearAllMemory(const base::ListValue& args);
  void HandleGetWorkspaceSummary(const base::ListValue& args);
  void HandleOpenWorkspace(const base::ListValue& args);
  void HandleStartManualDream(const base::ListValue& args);
  void HandleGetUsageStats(const base::ListValue& args);
  void HandleResetUsageStats(const base::ListValue& args);
  void HandleRecordApiUsage(const base::ListValue& args);
  void HandleRecordToolUsage(const base::ListValue& args);
  // Settings asks a registered dao://agent handler of the same profile to
  // run the test with pi-ai, then relays the page's result back.
  void HandleRegisterConnectionTester(const base::ListValue& args);
  void HandleTestConnection(const base::ListValue& args);
  void HandleConnectionTestResult(const base::ListValue& args);
  void StartConnectionTest(base::DictValue config,
                           ConnectionTestCallback callback);
  void UnregisterConnectionTester();
  void HandleTabClassificationResult(const base::ListValue& args);
  void FinishTabClassification(std::string request_id, base::DictValue result);
  void CancelTabClassifications();
  void OnSettingsChanged();
  void OnUsageStatsChanged();
  PrefService* GetPrefs();

  PrefChangeRegistrar pref_change_registrar_;
  base::flat_map<std::string, ConnectionTestCallback>
      pending_connection_tests_;
  base::flat_map<std::string, ConnectionTestCallback>
      pending_tab_classifications_;
  int next_connection_test_id_ = 0;
  base::WeakPtrFactory<DaoAgentSettingsHandler> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_SETTINGS_HANDLER_H_
