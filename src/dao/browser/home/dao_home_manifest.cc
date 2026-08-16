// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_manifest.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "crypto/sha2.h"
#include "url/gurl.h"

namespace dao {
namespace {

constexpr int64_t kMaxResultBytes = 5 * 1024 * 1024;
constexpr int kMaxItems = 500;

bool ReadStringList(const base::DictValue& dict,
                    std::string_view key,
                    std::vector<std::string>* output) {
  const base::ListValue* values = dict.FindList(key);
  if (!values || values->empty()) {
    return false;
  }
  std::set<std::string> seen;
  for (const base::Value& value : *values) {
    const std::string* text = value.GetIfString();
    if (!text || text->empty() || !seen.insert(*text).second) {
      return false;
    }
    output->push_back(*text);
  }
  return true;
}

base::expected<HomeConnector, HomeError> ParseConnector(
    const base::DictValue& value) {
  const std::string* id = value.FindString("id");
  const std::string* module = value.FindString("module");
  const std::string* schema = value.FindString("schema");
  const base::DictValue* permissions = value.FindDict("permissions");
  if (!id || id->empty() || id->size() > 80 || !module || !schema ||
      !permissions || !IsValidHomeRelativePath(*module) ||
      !IsValidHomeRelativePath(*schema)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }

  HomeConnector connector;
  connector.id = *id;
  connector.module = *module;
  connector.schema = *schema;

  std::vector<std::string> origins;
  if (!ReadStringList(*permissions, "origins", &origins)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  for (const std::string& origin_value : origins) {
    GURL url(origin_value);
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || url.path() != "/" ||
        url.has_query() || url.has_ref() || url.username().size() ||
        url.password().size()) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
    connector.permissions.origins.push_back(url::Origin::Create(url));
  }

  if (!ReadStringList(*permissions, "paths", &connector.permissions.paths)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  for (const std::string& path : connector.permissions.paths) {
    if (!IsHomeRoute(path)) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
  }

  std::vector<std::string> capabilities;
  if (!ReadStringList(*permissions, "capabilities", &capabilities)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  for (const std::string& capability_value : capabilities) {
    std::optional<HomePageCapability> capability =
        HomePageCapabilityFromString(capability_value);
    if (!capability) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
    connector.permissions.capabilities.insert(*capability);
  }

  const std::string* mode = permissions->FindString("mode");
  if (!mode || *mode != "read") {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  connector.permissions.mode = HomeAccessMode::kRead;
  return connector;
}

}  // namespace

bool IsValidHomeRelativePath(std::string_view path) {
  if (path.empty() || path.size() > 240 || path.front() == '/' ||
      path.back() == '/' || path.find('\\') != std::string_view::npos ||
      path.find('\0') != std::string_view::npos) {
    return false;
  }
  for (std::string_view part : base::SplitStringPiece(
           path, "/", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    if (part.empty() || part == "." || part == ".." ||
        part.find(':') != std::string_view::npos) {
      return false;
    }
  }
  return true;
}

bool IsHomeRoute(std::string_view path) {
  return !path.empty() && path.front() == '/' &&
         path.find("..") == std::string_view::npos &&
         path.find('\\') == std::string_view::npos &&
         path.find('?') == std::string_view::npos &&
         path.find('#') == std::string_view::npos;
}

base::expected<HomeManifest, HomeError> ParseHomeManifest(
    std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  const base::DictValue& value = parsed->GetDict();
  const std::optional<int> format_version = value.FindInt("format_version");
  if (!format_version) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  if (*format_version != 1) {
    return base::unexpected(HomeError::kUnsupportedFormat);
  }

  HomeManifest manifest;
  manifest.format_version = *format_version;
  const std::string* entry = value.FindString("entry");
  if (!entry || !IsValidHomeRelativePath(*entry)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  manifest.entry = *entry;
  if (!ReadStringList(value, "routes", &manifest.routes)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  for (const std::string& route : manifest.routes) {
    if (!IsHomeRoute(route)) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
  }

  std::set<std::string> connector_ids;
  if (const base::ListValue* connectors = value.FindList("connectors")) {
    for (const base::Value& item : *connectors) {
      if (!item.is_dict()) {
        return base::unexpected(HomeError::kInvalidManifest);
      }
      auto connector = ParseConnector(item.GetDict());
      if (!connector.has_value()) {
        return base::unexpected(connector.error());
      }
      if (!connector_ids.insert(connector->id).second) {
        return base::unexpected(HomeError::kInvalidManifest);
      }
      manifest.connectors.push_back(std::move(connector.value()));
    }
  }

  if (const base::DictValue* limits = value.FindDict("limits")) {
    std::optional<int> bytes = limits->FindInt("max_result_bytes");
    std::optional<int> items = limits->FindInt("max_items_per_connector");
    if (!bytes || *bytes <= 0 || *bytes > kMaxResultBytes || !items ||
        *items <= 0 || *items > kMaxItems) {
      return base::unexpected(HomeError::kInvalidManifest);
    }
    manifest.limits.max_result_bytes = *bytes;
    manifest.limits.max_items_per_connector = *items;
  }
  return manifest;
}

base::DictValue HomeManifestToValue(const HomeManifest& manifest) {
  base::DictValue result;
  result.Set("format_version", manifest.format_version);
  result.Set("entry", manifest.entry);
  base::ListValue routes;
  for (const std::string& route : manifest.routes) {
    routes.Append(route);
  }
  result.Set("routes", std::move(routes));
  base::ListValue connectors;
  for (const HomeConnector& connector : manifest.connectors) {
    base::DictValue permission;
    base::ListValue origins;
    for (const url::Origin& origin : connector.permissions.origins) {
      origins.Append(origin.Serialize());
    }
    permission.Set("origins", std::move(origins));
    base::ListValue paths;
    for (const std::string& path : connector.permissions.paths) {
      paths.Append(path);
    }
    permission.Set("paths", std::move(paths));
    base::ListValue capabilities;
    for (HomePageCapability capability : connector.permissions.capabilities) {
      capabilities.Append(HomePageCapabilityToString(capability));
    }
    permission.Set("capabilities", std::move(capabilities));
    permission.Set("mode", "read");
    connectors.Append(base::DictValue()
                          .Set("id", connector.id)
                          .Set("module", connector.module)
                          .Set("schema", connector.schema)
                          .Set("permissions", std::move(permission)));
  }
  result.Set("connectors", std::move(connectors));
  result.Set("limits",
             base::DictValue()
                 .Set("max_result_bytes",
                      static_cast<int>(manifest.limits.max_result_bytes))
                 .Set("max_items_per_connector",
                      manifest.limits.max_items_per_connector));
  return result;
}

std::string HomeConnectorPermissionFingerprint(
    const HomeConnectorPermission& permission) {
  base::DictValue value;
  base::ListValue origins;
  std::vector<std::string> origin_values;
  for (const url::Origin& origin : permission.origins) {
    origin_values.push_back(origin.Serialize());
  }
  std::ranges::sort(origin_values);
  for (const std::string& origin : origin_values) {
    origins.Append(origin);
  }
  value.Set("origins", std::move(origins));
  base::ListValue paths;
  std::vector<std::string> path_values = permission.paths;
  std::ranges::sort(path_values);
  for (const std::string& path : path_values) {
    paths.Append(path);
  }
  value.Set("paths", std::move(paths));
  base::ListValue capabilities;
  for (HomePageCapability capability : permission.capabilities) {
    capabilities.Append(HomePageCapabilityToString(capability));
  }
  value.Set("capabilities", std::move(capabilities));
  value.Set("mode", "read");
  std::string serialized;
  base::JSONWriter::Write(value, &serialized);
  return serialized;
}

std::string HomeConnectorGrantFingerprint(
    const HomeConnectorPermission& permission,
    const HomeLimits& limits) {
  base::DictValue value;
  value.Set("permission", HomeConnectorPermissionFingerprint(permission));
  value.Set("max_result_bytes", static_cast<double>(limits.max_result_bytes));
  value.Set("max_items_per_connector", limits.max_items_per_connector);
  std::string serialized;
  base::JSONWriter::Write(value, &serialized);
  return serialized;
}

std::string HomeConnectorFingerprint(const HomeConnector& connector,
                                     const HomeLimits& limits,
                                     std::string_view module_source,
                                     std::string_view schema_source) {
  base::DictValue value;
  value.Set("permission",
            HomeConnectorPermissionFingerprint(connector.permissions));
  value.Set("max_result_bytes", base::NumberToString(limits.max_result_bytes));
  value.Set("max_items_per_connector", limits.max_items_per_connector);
  value.Set("module_sha256",
            base::HexEncode(crypto::SHA256HashString(module_source)));
  value.Set("schema_sha256",
            base::HexEncode(crypto::SHA256HashString(schema_source)));
  std::string serialized;
  base::JSONWriter::Write(value, &serialized);
  return base::HexEncode(crypto::SHA256HashString(serialized));
}

}  // namespace dao
