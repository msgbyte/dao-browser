// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_PLUGINS_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_PLUGINS_H_

#include <string>
#include <string_view>

#include "base/hash/sha1.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "url/gurl.h"

namespace dao {

// The sole registration point for bundled optional plugins. The settings UI
// and Agent consume this same manifest; no remote code is loaded.
struct DaoAgentPluginManifest {
  const char* id;
  const char* name;
  const char* tool;
  const char* enabled_key;
  const char* url_key;
  const char* token_key;
  const char* permission_key;
};

inline constexpr DaoAgentPluginManifest kDaoAgentPlugins[] = {
    {"jev", "Jev", "run_browser_task", "dao_plugin_jev_enabled",
     "dao_plugin_jev_url", "dao_plugin_jev_token",
     "dao_plugin_jev_permission"},
};

inline std::string DaoAgentPluginSetting(const base::DictValue& settings,
                                         std::string_view key) {
  const std::string* value = settings.FindString(key);
  return value ? *value : "";
}

// Hashes only the plugin's own configuration, so unrelated settings writes do
// not invalidate a running task. The hash travels only over the trusted WebUI
// bridge, never to a model.
inline std::string DaoAgentPluginRevision(
    const base::DictValue& settings,
    const DaoAgentPluginManifest& plugin) {
  base::ListValue state;
  for (const char* key : {plugin.enabled_key, plugin.url_key,
                          plugin.token_key, plugin.permission_key}) {
    state.Append(DaoAgentPluginSetting(settings, key));
  }
  return base::HexEncode(
      base::SHA1HashString(base::WriteJson(state).value_or("")));
}

inline base::ListValue GetDaoAgentPlugins(
    const base::DictValue* settings = nullptr) {
  base::ListValue plugins;
  for (const auto& plugin : kDaoAgentPlugins) {
    auto entry = base::DictValue()
                     .Set("id", plugin.id)
                     .Set("name", plugin.name)
                     .Set("tool", plugin.tool)
                     .Set("enabledKey", plugin.enabled_key)
                     .Set("urlKey", plugin.url_key)
                     .Set("tokenKey", plugin.token_key)
                     .Set("permissionKey", plugin.permission_key);
    if (settings) {
      entry.Set("revision", DaoAgentPluginRevision(*settings, plugin));
    }
    plugins.Append(std::move(entry));
  }
  return plugins;
}

inline bool IsDaoAgentPluginAuthorized(const base::DictValue& settings,
                                       const base::DictValue& context,
                                       std::string_view tool = {}) {
  const std::string* id = context.FindString("id");
  const std::string* revision = context.FindString("revision");
  if (!id || !revision) {
    return false;
  }
  for (const auto& plugin : kDaoAgentPlugins) {
    if (*id != plugin.id) {
      continue;
    }
    const std::string token = DaoAgentPluginSetting(settings, plugin.token_key);
    const GURL url(DaoAgentPluginSetting(settings, plugin.url_key));
    // NUL, CR and LF would trip net's request header CHECK when sent.
    if (DaoAgentPluginRevision(settings, plugin) != *revision ||
        DaoAgentPluginSetting(settings, plugin.enabled_key) != "true" ||
        DaoAgentPluginSetting(settings, plugin.permission_key) != "true" ||
        token.find_first_not_of(" \t\r\n") == std::string::npos ||
        token.find_first_of(std::string_view("\0\r\n", 3)) !=
            std::string::npos ||
        !url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.has_username() ||
        url.has_password() || url.has_ref()) {
      return false;
    }
    if (const std::string* disabled =
            settings.FindString("dao_disabled_tools")) {
      auto parsed = base::JSONReader::Read(*disabled, base::JSON_PARSE_RFC);
      if (!parsed || !parsed->is_list()) {
        return false;
      }
      for (const auto& name : parsed->GetList()) {
        if (name.is_string() && name.GetString() == tool) {
          return false;
        }
      }
    }
    return true;
  }
  return false;
}

inline bool IsDaoAgentPluginSetting(std::string_view key) {
  for (const auto& plugin : kDaoAgentPlugins) {
    for (const char* field : {plugin.enabled_key, plugin.url_key,
                              plugin.token_key, plugin.permission_key}) {
      if (key == field) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_PLUGINS_H_
