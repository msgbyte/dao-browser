// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_settings_handler.h"

#include <array>
#include <cmath>
#include <set>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "content/public/browser/web_ui.h"
#include "dao/browser/agent/dao_dream_domain_utils.h"
#include "dao/browser/dao_pref_names.h"

namespace dao {
namespace {

constexpr char kSettingsChangedEvent[] = "dao-agent-settings-changed";
constexpr size_t kMaxSettingValueBytes = 256 * 1024;
constexpr size_t kMaxSettingsBytes = 512 * 1024;

constexpr char kUsageStatsApiCalls[] = "apiCalls";
constexpr char kUsageStatsToolCalls[] = "toolCalls";
constexpr char kUsageStatsPromptTokens[] = "promptTokens";
constexpr char kUsageStatsCompletionTokens[] = "completionTokens";
constexpr char kUsageStatsTotalTokens[] = "totalTokens";
constexpr char kUsageStatsEstimatedCost[] = "estimatedCost";
constexpr char kUsageStatsLastReset[] = "lastReset";

constexpr std::array<std::string_view, 17> kManagedStringSettings = {
    "dao_agent_providers",      "dao_agent_active_provider",
    "dao_agent_api_key",        "dao_agent_base_url",
    "dao_agent_model",          "dao_agent_soul",
    "dao_disabled_tools",       "dao_tool_call_show_details",
    "dao_agent_debug_mode",     "dao_resume_last_session",
    "dao_resume_stale_hours",   "dao_proactive_enabled",
    "dao_page_context_enabled", "dao_conversation_enabled",
    "dao_proactive_threshold",  "dao_search_source",
    "dao_jina_api_key",
};

bool ParseBooleanSetting(const base::Value &value, bool *parsed) {
  if (!value.is_string()) {
    return false;
  }
  if (value.GetString() == "true") {
    *parsed = true;
    return true;
  }
  if (value.GetString() == "false") {
    *parsed = false;
    return true;
  }
  return false;
}

std::string SerializeDreamExcludedDomains(PrefService *prefs) {
  std::string json;
  base::JSONWriter::Write(prefs->GetList(prefs::kDaoDreamExcludedDomains),
                          &json);
  return json;
}

bool SetDreamExcludedDomains(PrefService *prefs, const base::Value &value) {
  if (!value.is_string() || value.GetString().size() > kMaxSettingValueBytes) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(value.GetString(), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return false;
  }
  std::set<std::string> normalized;
  for (const base::Value &entry : parsed->GetList()) {
    if (!entry.is_string()) {
      return false;
    }
    std::string domain = NormalizeDreamExcludedDomain(entry.GetString());
    if (domain.empty()) {
      return false;
    }
    normalized.insert(std::move(domain));
  }
  base::ListValue domains;
  for (const std::string &domain : normalized) {
    domains.Append(domain);
  }
  prefs->SetList(prefs::kDaoDreamExcludedDomains, std::move(domains));
  return true;
}

std::optional<double> ReadNonNegativeFiniteNumber(const base::Value *value) {
  if (!value) {
    return std::nullopt;
  }
  const double number = value->is_int()
                            ? static_cast<double>(value->GetInt())
                            : (value->is_double() ? value->GetDouble() : -1.0);
  return number >= 0.0 && std::isfinite(number) ? std::optional<double>(number)
                                                : std::nullopt;
}

base::DictValue NewUsageStats(base::Time last_reset) {
  base::DictValue stats;
  stats.Set(kUsageStatsApiCalls, 0.0);
  stats.Set(kUsageStatsToolCalls, base::DictValue());
  stats.Set(kUsageStatsPromptTokens, 0.0);
  stats.Set(kUsageStatsCompletionTokens, 0.0);
  stats.Set(kUsageStatsTotalTokens, 0.0);
  stats.Set(kUsageStatsEstimatedCost, 0.0);
  stats.Set(kUsageStatsLastReset,
            static_cast<double>(last_reset.InMillisecondsSinceUnixEpoch()));
  return stats;
}

bool IsValidUsageToolName(std::string_view tool_name) {
  return !tool_name.empty() &&
         tool_name.size() <= kMaxDaoAgentUsageToolNameBytes &&
         base::IsStringUTF8(tool_name);
}

bool NormalizeUsageStats(const base::DictValue &source,
                         base::DictValue *normalized) {
  const std::optional<double> api_calls =
      ReadNonNegativeFiniteNumber(source.Find(kUsageStatsApiCalls));
  const std::optional<double> prompt_tokens =
      ReadNonNegativeFiniteNumber(source.Find(kUsageStatsPromptTokens));
  const std::optional<double> completion_tokens =
      ReadNonNegativeFiniteNumber(source.Find(kUsageStatsCompletionTokens));
  const std::optional<double> total_tokens =
      ReadNonNegativeFiniteNumber(source.Find(kUsageStatsTotalTokens));
  const std::optional<double> estimated_cost =
      ReadNonNegativeFiniteNumber(source.Find(kUsageStatsEstimatedCost));
  const std::optional<double> last_reset =
      ReadNonNegativeFiniteNumber(source.Find(kUsageStatsLastReset));
  const base::DictValue *tool_calls = source.FindDict(kUsageStatsToolCalls);
  if (!api_calls || !prompt_tokens || !completion_tokens || !total_tokens ||
      !estimated_cost || !last_reset || !tool_calls ||
      *total_tokens != *prompt_tokens + *completion_tokens ||
      tool_calls->size() > kMaxDaoAgentUsageTools) {
    return false;
  }

  base::DictValue normalized_tools;
  for (const auto [tool_name, count] : *tool_calls) {
    const std::optional<double> tool_count =
        ReadNonNegativeFiniteNumber(&count);
    if (!IsValidUsageToolName(tool_name) || !tool_count) {
      return false;
    }
    normalized_tools.Set(tool_name, *tool_count);
  }

  normalized->Set(kUsageStatsApiCalls, *api_calls);
  normalized->Set(kUsageStatsToolCalls, std::move(normalized_tools));
  normalized->Set(kUsageStatsPromptTokens, *prompt_tokens);
  normalized->Set(kUsageStatsCompletionTokens, *completion_tokens);
  normalized->Set(kUsageStatsTotalTokens, *total_tokens);
  normalized->Set(kUsageStatsEstimatedCost, *estimated_cost);
  normalized->Set(kUsageStatsLastReset, *last_reset);
  return true;
}

void StoreUsageStats(ScopedDictPrefUpdate *update, base::DictValue stats) {
  update->clear();
  update->Merge(std::move(stats));
}

base::DictValue ReadUsageStatsOrDefault(PrefService *prefs) {
  base::DictValue normalized;
  if (NormalizeUsageStats(prefs->GetDict(prefs::kDaoAgentUsageStats),
                          &normalized)) {
    return normalized;
  }
  return NewUsageStats(base::Time::Now());
}

} // namespace

bool IsManagedDaoAgentSetting(std::string_view key) {
  for (std::string_view candidate : kManagedStringSettings) {
    if (candidate == key) {
      return true;
    }
  }
  return key == kDaoAgentMemoryEnabledSetting ||
         key == kDaoDreamEnabledSetting || key == kDaoDreamDebugSetting ||
         key == kDaoDreamExcludedDomainsSetting;
}

base::DictValue BuildDaoAgentSettingsSnapshot(PrefService *prefs) {
  base::DictValue snapshot;
  snapshot.Set("migrationVersion",
               prefs->GetInteger(prefs::kDaoAgentSettingsMigrationVersion));

  base::DictValue values = prefs->GetDict(prefs::kDaoAgentSettings).Clone();
  values.Set(kDaoAgentMemoryEnabledSetting,
             prefs->GetBoolean(prefs::kDaoAgentMemoryEnabled) ? "true"
                                                              : "false");
  values.Set(kDaoDreamEnabledSetting,
             prefs->GetBoolean(prefs::kDaoDreamEnabled) ? "true" : "false");
  values.Set(kDaoDreamDebugSetting,
             prefs->GetBoolean(prefs::kDaoDreamDebug) ? "true" : "false");
  values.Set(kDaoDreamExcludedDomainsSetting,
             SerializeDreamExcludedDomains(prefs));
  snapshot.Set("values", std::move(values));
  snapshot.Set("usageStats", BuildDaoAgentUsageStats(prefs));
  return snapshot;
}

base::DictValue BuildDaoAgentUsageStats(PrefService *prefs) {
  return prefs ? ReadUsageStatsOrDefault(prefs) : base::DictValue();
}

bool MigrateLegacyDaoAgentUsageStats(PrefService *prefs,
                                     const base::Value *legacy_value) {
  if (!prefs || !legacy_value || !legacy_value->is_string() ||
      !prefs->GetDict(prefs::kDaoAgentUsageStats).empty()) {
    return false;
  }
  std::optional<base::Value> parsed =
      base::JSONReader::Read(legacy_value->GetString(), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  base::DictValue normalized;
  if (!NormalizeUsageStats(parsed->GetDict(), &normalized)) {
    return false;
  }
  ScopedDictPrefUpdate update(prefs, prefs::kDaoAgentUsageStats);
  StoreUsageStats(&update, std::move(normalized));
  return true;
}

void RecordDaoAgentApiUsage(PrefService *prefs, double api_calls,
                            double prompt_tokens, double completion_tokens,
                            double estimated_cost) {
  if (!prefs || !std::isfinite(api_calls) || !std::isfinite(prompt_tokens) ||
      !std::isfinite(completion_tokens) || !std::isfinite(estimated_cost) ||
      api_calls < 0.0 || prompt_tokens < 0.0 || completion_tokens < 0.0 ||
      estimated_cost < 0.0) {
    return;
  }

  base::DictValue stats = ReadUsageStatsOrDefault(prefs);
  const double next_api_calls =
      *stats.FindDouble(kUsageStatsApiCalls) + api_calls;
  const double next_prompt_tokens =
      *stats.FindDouble(kUsageStatsPromptTokens) + prompt_tokens;
  const double next_completion_tokens =
      *stats.FindDouble(kUsageStatsCompletionTokens) + completion_tokens;
  const double next_total_tokens = next_prompt_tokens + next_completion_tokens;
  const double next_estimated_cost =
      *stats.FindDouble(kUsageStatsEstimatedCost) + estimated_cost;
  if (!std::isfinite(next_api_calls) || !std::isfinite(next_prompt_tokens) ||
      !std::isfinite(next_completion_tokens) ||
      !std::isfinite(next_total_tokens) ||
      !std::isfinite(next_estimated_cost)) {
    return;
  }
  stats.Set(kUsageStatsApiCalls, next_api_calls);
  stats.Set(kUsageStatsPromptTokens, next_prompt_tokens);
  stats.Set(kUsageStatsCompletionTokens, next_completion_tokens);
  stats.Set(kUsageStatsTotalTokens, next_total_tokens);
  stats.Set(kUsageStatsEstimatedCost, next_estimated_cost);
  ScopedDictPrefUpdate update(prefs, prefs::kDaoAgentUsageStats);
  StoreUsageStats(&update, std::move(stats));
}

void RecordDaoAgentToolUsage(PrefService *prefs, std::string_view tool_name) {
  if (!prefs || !IsValidUsageToolName(tool_name)) {
    return;
  }
  base::DictValue stats = ReadUsageStatsOrDefault(prefs);
  base::DictValue *tool_calls = stats.FindDict(kUsageStatsToolCalls);
  const base::Value *existing_value = tool_calls->Find(tool_name);
  if (!existing_value && tool_calls->size() >= kMaxDaoAgentUsageTools) {
    return;
  }
  const double existing_count =
      existing_value ? *existing_value->GetIfDouble() : 0.0;
  if (!std::isfinite(existing_count + 1.0)) {
    return;
  }
  tool_calls->Set(tool_name, existing_count + 1.0);
  ScopedDictPrefUpdate update(prefs, prefs::kDaoAgentUsageStats);
  StoreUsageStats(&update, std::move(stats));
}

void ResetDaoAgentUsageStats(PrefService *prefs, base::Time last_reset) {
  if (!prefs || last_reset.is_null() ||
      last_reset.InMillisecondsSinceUnixEpoch() < 0) {
    return;
  }
  ScopedDictPrefUpdate update(prefs, prefs::kDaoAgentUsageStats);
  StoreUsageStats(&update, NewUsageStats(last_reset));
}

bool SetDaoAgentSetting(PrefService *prefs, std::string_view key,
                        const base::Value &value) {
  if (!prefs || !IsManagedDaoAgentSetting(key)) {
    return false;
  }
  if (key == kDaoAgentMemoryEnabledSetting || key == kDaoDreamEnabledSetting ||
      key == kDaoDreamDebugSetting) {
    bool enabled = false;
    if (!ParseBooleanSetting(value, &enabled)) {
      return false;
    }
    const char *pref_name =
        key == kDaoAgentMemoryEnabledSetting
            ? prefs::kDaoAgentMemoryEnabled
            : (key == kDaoDreamEnabledSetting ? prefs::kDaoDreamEnabled
                                              : prefs::kDaoDreamDebug);
    prefs->SetBoolean(pref_name, enabled);
    return true;
  }
  if (key == kDaoDreamExcludedDomainsSetting) {
    return SetDreamExcludedDomains(prefs, value);
  }

  ScopedDictPrefUpdate update(prefs, prefs::kDaoAgentSettings);
  if (value.is_none()) {
    update->Remove(key);
    return true;
  }
  if (!value.is_string() || value.GetString().size() > kMaxSettingValueBytes) {
    return false;
  }
  update->Set(key, value.GetString());
  return true;
}

base::DictValue
MigrateLegacyDaoAgentSettings(PrefService *prefs,
                              const base::DictValue &legacy_values) {
  if (!prefs || prefs->GetInteger(prefs::kDaoAgentSettingsMigrationVersion) >=
                    kDaoAgentSettingsMigrationVersion) {
    return prefs ? BuildDaoAgentSettingsSnapshot(prefs) : base::DictValue();
  }

  if (prefs->GetInteger(prefs::kDaoAgentSettingsMigrationVersion) < 1) {
    size_t total_bytes = 0;
    {
      ScopedDictPrefUpdate update(prefs, prefs::kDaoAgentSettings);
      for (const auto [key, value] : legacy_values) {
        if (!IsManagedDaoAgentSetting(key) || !value.is_string() ||
            key == kDaoAgentMemoryEnabledSetting ||
            key == kDaoDreamEnabledSetting || key == kDaoDreamDebugSetting ||
            key == kDaoDreamExcludedDomainsSetting || update->contains(key)) {
          continue;
        }
        total_bytes += value.GetString().size();
        if (value.GetString().size() > kMaxSettingValueBytes ||
            total_bytes > kMaxSettingsBytes) {
          continue;
        }
        update->Set(key, value.GetString());
      }
    }
  }
  MigrateLegacyDaoAgentUsageStats(prefs, legacy_values.Find("dao_agent_stats"));
  prefs->SetInteger(prefs::kDaoAgentSettingsMigrationVersion,
                    kDaoAgentSettingsMigrationVersion);
  return BuildDaoAgentSettingsSnapshot(prefs);
}

DaoAgentSettingsHandler::DaoAgentSettingsHandler() = default;
DaoAgentSettingsHandler::~DaoAgentSettingsHandler() = default;

void DaoAgentSettingsHandler::RegisterMessages() {
  web_ui()->RegisterMessageCallback(
      "getDaoAgentSettings",
      base::BindRepeating(&DaoAgentSettingsHandler::HandleGetSettings,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "migrateDaoAgentSettings",
      base::BindRepeating(&DaoAgentSettingsHandler::HandleMigrateLegacySettings,
                          base::Unretained(this)));
  web_ui()->RegisterMessageCallback(
      "setDaoAgentSetting",
      base::BindRepeating(&DaoAgentSettingsHandler::HandleSetSetting,
                          base::Unretained(this)));
}

void DaoAgentSettingsHandler::OnJavascriptAllowed() {
  PrefService *prefs = GetPrefs();
  if (!prefs || pref_change_registrar_.prefs()) {
    return;
  }
  pref_change_registrar_.Init(prefs);
  const auto callback = base::BindRepeating(
      &DaoAgentSettingsHandler::OnSettingsChanged, base::Unretained(this));
  pref_change_registrar_.Add(prefs::kDaoAgentSettings, callback);
  pref_change_registrar_.Add(prefs::kDaoAgentSettingsMigrationVersion,
                             callback);
  pref_change_registrar_.Add(prefs::kDaoAgentUsageStats, callback);
  pref_change_registrar_.Add(prefs::kDaoAgentMemoryEnabled, callback);
  pref_change_registrar_.Add(prefs::kDaoDreamEnabled, callback);
  pref_change_registrar_.Add(prefs::kDaoDreamDebug, callback);
  pref_change_registrar_.Add(prefs::kDaoDreamExcludedDomains, callback);
}

void DaoAgentSettingsHandler::OnJavascriptDisallowed() {
  pref_change_registrar_.Reset();
}

void DaoAgentSettingsHandler::HandleGetSettings(const base::ListValue &args) {
  AllowJavascript();
  if (args.size() != 1 || !args[0].is_string() || !GetPrefs()) {
    return;
  }
  ResolveJavascriptCallback(args[0], BuildDaoAgentSettingsSnapshot(GetPrefs()));
}

void DaoAgentSettingsHandler::HandleMigrateLegacySettings(
    const base::ListValue &args) {
  AllowJavascript();
  if (args.size() != 2 || !args[0].is_string() || !args[1].is_dict() ||
      !GetPrefs()) {
    return;
  }
  ResolveJavascriptCallback(
      args[0], MigrateLegacyDaoAgentSettings(GetPrefs(), args[1].GetDict()));
}

void DaoAgentSettingsHandler::HandleSetSetting(const base::ListValue &args) {
  AllowJavascript();
  if (args.size() != 3 || !args[0].is_string() || !args[1].is_string() ||
      !GetPrefs()) {
    return;
  }
  ResolveJavascriptCallback(
      args[0], SetDaoAgentSetting(GetPrefs(), args[1].GetString(), args[2]));
}

void DaoAgentSettingsHandler::OnSettingsChanged() {
  if (IsJavascriptAllowed() && GetPrefs()) {
    FireWebUIListener(kSettingsChangedEvent,
                      BuildDaoAgentSettingsSnapshot(GetPrefs()));
  }
}

PrefService *DaoAgentSettingsHandler::GetPrefs() {
  Profile *profile = Profile::FromWebUI(web_ui());
  return profile ? profile->GetOriginalProfile()->GetPrefs() : nullptr;
}

} // namespace dao
