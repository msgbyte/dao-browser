// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_experience.h"

#include <optional>
#include <set>
#include <string>

#include "base/json/json_reader.h"
#include "base/values.h"

namespace dao {
namespace {

constexpr size_t kMaxPrimaryActions = 12;
constexpr size_t kMaxSourceSlots = 3;
constexpr size_t kMaxIdentifierLength = 64;

bool IsIdentifierCharacter(char value) {
  return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
         value == '_' || value == '-';
}

bool IsValidIdentifier(std::string_view value) {
  if (value.empty() || value.size() > kMaxIdentifierLength ||
      !((value.front() >= 'a' && value.front() <= 'z') ||
        (value.front() >= '0' && value.front() <= '9'))) {
    return false;
  }
  for (char character : value) {
    if (!IsIdentifierCharacter(character)) {
      return false;
    }
  }
  return true;
}

bool ReadIdentifiers(const base::DictValue& value,
                     std::string_view key,
                     size_t maximum_size,
                     std::vector<std::string>* output) {
  const base::ListValue* identifiers = value.FindList(key);
  if (!identifiers || identifiers->size() > maximum_size) {
    return false;
  }
  std::set<std::string> seen;
  for (const base::Value& identifier_value : *identifiers) {
    const std::string* identifier = identifier_value.GetIfString();
    if (!identifier || !IsValidIdentifier(*identifier) ||
        !seen.insert(*identifier).second) {
      return false;
    }
    output->push_back(*identifier);
  }
  return true;
}

}  // namespace

base::expected<HomeExperience, HomeError> ParseHomeExperience(
    std::string_view json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  const base::DictValue& value = parsed->GetDict();
  const std::string* kind = value.FindString("kind");
  if (value.size() != 3u || !kind || *kind != "start_surface") {
    return base::unexpected(HomeError::kInvalidManifest);
  }

  HomeExperience experience;
  if (!ReadIdentifiers(value, "primary_actions", kMaxPrimaryActions,
                       &experience.primary_actions) ||
      !ReadIdentifiers(value, "source_slots", kMaxSourceSlots,
                       &experience.source_slots)) {
    return base::unexpected(HomeError::kInvalidManifest);
  }
  return experience;
}

}  // namespace dao
