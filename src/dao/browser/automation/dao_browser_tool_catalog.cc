// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_browser_tool_catalog.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/types/expected.h"
#include "chrome/grit/dao_agent_resources.h"
#include "dao/browser/automation/dao_tool_schema_validator.h"
#include "ui/base/resource/resource_bundle.h"

namespace dao {
namespace {

constexpr int kCatalogVersion = 1;

std::optional<DaoBrowserToolGroup> ParseGroup(std::string_view group) {
  if (group == "page") {
    return DaoBrowserToolGroup::kPage;
  }
  if (group == "tabs") {
    return DaoBrowserToolGroup::kTabs;
  }
  if (group == "devtools") {
    return DaoBrowserToolGroup::kDevTools;
  }
  return std::nullopt;
}

std::optional<DaoBrowserToolSideEffect> ParseSideEffect(
    std::string_view side_effect) {
  if (side_effect == "read") {
    return DaoBrowserToolSideEffect::kRead;
  }
  if (side_effect == "interaction") {
    return DaoBrowserToolSideEffect::kInteraction;
  }
  if (side_effect == "destructive") {
    return DaoBrowserToolSideEffect::kDestructive;
  }
  return std::nullopt;
}

std::optional<DaoToolClient> ParseClient(std::string_view client) {
  if (client == "dao_agent") {
    return DaoToolClient::kDaoAgent;
  }
  if (client == "mcp") {
    return DaoToolClient::kMcp;
  }
  return std::nullopt;
}

bool HasClient(const DaoBrowserToolDefinition& definition,
               DaoToolClient client) {
  return std::ranges::find(definition.clients, client) !=
         definition.clients.end();
}

std::optional<std::vector<DaoBrowserToolDefinition>> ParseCatalog(
    std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }

  const base::DictValue& root = parsed->GetDict();
  if (root.FindInt("version") != kCatalogVersion) {
    return std::nullopt;
  }
  const base::ListValue* raw_tools = root.FindList("tools");
  if (!raw_tools || raw_tools->empty()) {
    return std::nullopt;
  }

  std::set<std::string> names;
  std::vector<DaoBrowserToolDefinition> tools;
  tools.reserve(raw_tools->size());
  for (const base::Value& raw_tool : *raw_tools) {
    if (!raw_tool.is_dict()) {
      return std::nullopt;
    }
    const base::DictValue& dict = raw_tool.GetDict();
    const std::string* name = dict.FindString("name");
    const std::string* description = dict.FindString("description");
    const std::string* raw_group = dict.FindString("group");
    const std::string* raw_side_effect = dict.FindString("sideEffect");
    const base::ListValue* raw_clients = dict.FindList("clients");
    const base::DictValue* input_schema = dict.FindDict("inputSchema");
    const std::optional<int> timeout_ms = dict.FindInt("timeoutMs");
    if (!name || name->empty() || !description || description->empty() ||
        !raw_group || !raw_side_effect || !raw_clients ||
        raw_clients->empty() || !input_schema || !timeout_ms ||
        *timeout_ms <= 0 || !names.insert(*name).second) {
      return std::nullopt;
    }

    std::optional<DaoBrowserToolGroup> group = ParseGroup(*raw_group);
    std::optional<DaoBrowserToolSideEffect> side_effect =
        ParseSideEffect(*raw_side_effect);
    if (!group || !side_effect || !IsSupportedToolSchema(*input_schema)) {
      return std::nullopt;
    }

    DaoBrowserToolDefinition definition;
    definition.name = *name;
    definition.description = *description;
    definition.input_schema = input_schema->Clone();
    definition.group = *group;
    definition.side_effect = *side_effect;
    definition.timeout = base::Milliseconds(*timeout_ms);

    for (const base::Value& raw_client : *raw_clients) {
      const std::string* client_name = raw_client.GetIfString();
      if (!client_name) {
        return std::nullopt;
      }
      std::optional<DaoToolClient> client = ParseClient(*client_name);
      if (!client || std::ranges::find(definition.clients, *client) !=
                         definition.clients.end()) {
        return std::nullopt;
      }
      definition.clients.push_back(*client);
    }

    if (const base::Value* output_schema = dict.Find("outputSchema")) {
      const base::DictValue* output_schema_dict = output_schema->GetIfDict();
      if (!output_schema_dict || !IsSupportedToolSchema(*output_schema_dict)) {
        return std::nullopt;
      }
      definition.output_schema = output_schema_dict->Clone();
    }
    tools.push_back(std::move(definition));
  }
  return tools;
}

}  // namespace

DaoBrowserToolDefinition::DaoBrowserToolDefinition() = default;

DaoBrowserToolDefinition::~DaoBrowserToolDefinition() = default;

DaoBrowserToolDefinition::DaoBrowserToolDefinition(
    DaoBrowserToolDefinition&&) noexcept = default;

DaoBrowserToolDefinition& DaoBrowserToolDefinition::operator=(
    DaoBrowserToolDefinition&&) noexcept = default;

DaoBrowserToolCatalog::Cache::Cache() = default;

DaoBrowserToolCatalog::Cache::~Cache() = default;

base::expected<const DaoBrowserToolCatalog*, DaoToolError>
DaoBrowserToolCatalog::Cache::GetOrLoad(
    base::FunctionRef<LoadResult()> loader) {
  base::AutoLock auto_lock(lock_);
  if (!catalog_) {
    LoadResult loaded = loader();
    if (!loaded.has_value()) {
      return base::unexpected(std::move(loaded.error()));
    }
    catalog_ = std::move(loaded.value());
  }
  return catalog_.get();
}

const DaoBrowserToolCatalog* DaoBrowserToolCatalog::Get() {
  auto catalog = TryGet();
  CHECK(catalog.has_value()) << catalog.error().message;
  return catalog.value();
}

// static
base::expected<const DaoBrowserToolCatalog*, DaoToolError>
DaoBrowserToolCatalog::TryGet() {
  static base::NoDestructor<Cache> cache;
  return cache->GetOrLoad([]() { return LoadFromResourceBundle(); });
}

// static
DaoBrowserToolCatalog::LoadResult DaoBrowserToolCatalog::CreateFromJson(
    std::string_view json) {
  std::optional<std::vector<DaoBrowserToolDefinition>> parsed =
      ParseCatalog(json);
  if (!parsed) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kInternalError,
        "The embedded browser tool catalog is malformed or unsupported."));
  }
  return std::unique_ptr<DaoBrowserToolCatalog>(
      new DaoBrowserToolCatalog(std::move(*parsed)));
}

// static
DaoBrowserToolCatalog::LoadResult
DaoBrowserToolCatalog::LoadFromResourceBundle() {
  if (!ui::ResourceBundle::HasSharedInstance()) {
    return base::unexpected(
        MakeDaoToolError(DaoToolErrorCode::kInternalError,
                         "The browser tool catalog was requested before "
                         "resources initialized."));
  }
  std::string json =
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_DAO_AGENT_BROWSER_TOOL_CATALOG_JSON);
  return CreateFromJson(json);
}

DaoBrowserToolCatalog::DaoBrowserToolCatalog(
    std::vector<DaoBrowserToolDefinition> tools)
    : tools_(std::move(tools)) {}

DaoBrowserToolCatalog::~DaoBrowserToolCatalog() = default;

const DaoBrowserToolDefinition* DaoBrowserToolCatalog::Find(
    std::string_view name,
    DaoToolClient client) const {
  auto it = std::ranges::find_if(
      tools_, [name, client](const DaoBrowserToolDefinition& definition) {
        return definition.name == name && HasClient(definition, client);
      });
  return it == tools_.end() ? nullptr : &*it;
}

std::vector<const DaoBrowserToolDefinition*> DaoBrowserToolCatalog::List(
    DaoToolClient client) const {
  std::vector<const DaoBrowserToolDefinition*> result;
  for (const DaoBrowserToolDefinition& definition : tools_) {
    if (HasClient(definition, client)) {
      result.push_back(&definition);
    }
  }
  return result;
}

}  // namespace dao
