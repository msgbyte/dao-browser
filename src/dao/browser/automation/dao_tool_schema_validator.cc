// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/automation/dao_tool_schema_validator.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "dao/browser/automation/dao_browser_tool_catalog.h"

namespace dao {
namespace {

enum class SchemaType {
  kObject,
  kString,
  kNumber,
  kInteger,
  kBoolean,
  kArray,
};

constexpr double kMaxSafeJsonInteger = 9007199254740991.0;

std::optional<SchemaType> ParseSchemaType(const base::DictValue& schema) {
  const std::string* type = schema.FindString("type");
  if (!type) {
    return std::nullopt;
  }
  if (*type == "object") {
    return SchemaType::kObject;
  }
  if (*type == "string") {
    return SchemaType::kString;
  }
  if (*type == "number") {
    return SchemaType::kNumber;
  }
  if (*type == "integer") {
    return SchemaType::kInteger;
  }
  if (*type == "boolean") {
    return SchemaType::kBoolean;
  }
  if (*type == "array") {
    return SchemaType::kArray;
  }
  return std::nullopt;
}

bool IsFiniteNumber(const base::Value& value) {
  return (value.is_int() || value.is_double()) &&
         std::isfinite(value.GetDouble());
}

bool IsSafeJsonInteger(const base::Value& value) {
  if (value.is_int()) {
    return true;
  }
  if (!value.is_double()) {
    return false;
  }
  const double number = value.GetDouble();
  return std::isfinite(number) && std::trunc(number) == number &&
         std::abs(number) <= kMaxSafeJsonInteger;
}

bool ValueMatchesType(const base::Value& value, SchemaType type) {
  switch (type) {
    case SchemaType::kObject:
      return value.is_dict();
    case SchemaType::kString:
      return value.is_string();
    case SchemaType::kNumber:
      return IsFiniteNumber(value);
    case SchemaType::kInteger:
      return IsSafeJsonInteger(value);
    case SchemaType::kBoolean:
      return value.is_bool();
    case SchemaType::kArray:
      return value.is_list();
  }
  return false;
}

bool EnumValuesEqual(const base::Value& expected, const base::Value& actual) {
  if (IsFiniteNumber(expected) && IsFiniteNumber(actual)) {
    return expected.GetDouble() == actual.GetDouble();
  }
  return expected == actual;
}

bool IsAllowedKeyword(std::string_view keyword, SchemaType type) {
  if (keyword == "type" || keyword == "description" || keyword == "enum") {
    return true;
  }
  switch (type) {
    case SchemaType::kObject:
      return keyword == "properties" || keyword == "required" ||
             keyword == "additionalProperties";
    case SchemaType::kNumber:
    case SchemaType::kInteger:
      return keyword == "minimum" || keyword == "maximum";
    case SchemaType::kArray:
      return keyword == "items";
    case SchemaType::kString:
    case SchemaType::kBoolean:
      return false;
  }
  return false;
}

bool ValidateSchema(const base::DictValue& schema) {
  std::optional<SchemaType> type = ParseSchemaType(schema);
  if (!type) {
    return false;
  }
  for (const auto [keyword, _] : schema) {
    if (!IsAllowedKeyword(keyword, *type)) {
      return false;
    }
  }

  if (const base::Value* description = schema.Find("description");
      description && !description->is_string()) {
    return false;
  }
  if (const base::ListValue* enum_values = schema.FindList("enum")) {
    if (enum_values->empty()) {
      return false;
    }
    for (const base::Value& enum_value : *enum_values) {
      if (!ValueMatchesType(enum_value, *type)) {
        return false;
      }
    }
  } else if (schema.contains("enum")) {
    return false;
  }

  if (*type == SchemaType::kObject) {
    const base::DictValue* properties = schema.FindDict("properties");
    const base::ListValue* required = schema.FindList("required");
    if (!properties || !required) {
      return false;
    }
    for (const auto [name, property] : *properties) {
      if (!property.is_dict() || !ValidateSchema(property.GetDict())) {
        return false;
      }
    }
    std::set<std::string> required_names;
    for (const base::Value& required_name : *required) {
      const std::string* name = required_name.GetIfString();
      if (!name || !properties->contains(*name) ||
          !required_names.insert(*name).second) {
        return false;
      }
    }
    if (schema.contains("additionalProperties") &&
        schema.FindBool("additionalProperties") != false) {
      return false;
    }
    return true;
  }

  if (*type == SchemaType::kArray) {
    const base::DictValue* items = schema.FindDict("items");
    return items && ValidateSchema(*items);
  }

  if (*type == SchemaType::kNumber || *type == SchemaType::kInteger) {
    const std::optional<double> minimum = schema.FindDouble("minimum");
    const std::optional<double> maximum = schema.FindDouble("maximum");
    if ((schema.contains("minimum") && !minimum) ||
        (schema.contains("maximum") && !maximum)) {
      return false;
    }
    return !minimum || !maximum || *minimum <= *maximum;
  }
  return true;
}

bool ValidateValue(const base::DictValue& schema, const base::Value& value);

bool ValidateObject(const base::DictValue& schema,
                    const base::DictValue& value) {
  const base::DictValue* properties = schema.FindDict("properties");
  const base::ListValue* required = schema.FindList("required");
  if (!properties || !required) {
    return false;
  }

  for (const base::Value& required_name : *required) {
    const std::string* name = required_name.GetIfString();
    if (!name || !value.contains(*name)) {
      return false;
    }
  }
  if (schema.FindBool("additionalProperties") == false) {
    for (const auto [name, _] : value) {
      if (!properties->contains(name)) {
        return false;
      }
    }
  }
  for (const auto [name, property_schema] : *properties) {
    const base::Value* argument = value.Find(name);
    if (argument && !ValidateValue(property_schema.GetDict(), *argument)) {
      return false;
    }
  }
  return true;
}

bool ValidateValue(const base::DictValue& schema, const base::Value& value) {
  std::optional<SchemaType> type = ParseSchemaType(schema);
  if (!type || !ValueMatchesType(value, *type)) {
    return false;
  }

  if (const base::ListValue* enum_values = schema.FindList("enum");
      enum_values && std::ranges::find_if(
                         *enum_values, [&value](const base::Value& enum_value) {
                           return EnumValuesEqual(enum_value, value);
                         }) == enum_values->end()) {
    return false;
  }

  if (*type == SchemaType::kNumber || *type == SchemaType::kInteger) {
    const double number = value.GetDouble();
    if (const std::optional<double> minimum = schema.FindDouble("minimum");
        minimum && number < *minimum) {
      return false;
    }
    if (const std::optional<double> maximum = schema.FindDouble("maximum");
        maximum && number > *maximum) {
      return false;
    }
  }

  if (*type == SchemaType::kObject) {
    return ValidateObject(schema, value.GetDict());
  }
  if (*type == SchemaType::kArray) {
    const base::DictValue* items = schema.FindDict("items");
    if (!items) {
      return false;
    }
    for (const base::Value& item : value.GetList()) {
      if (!ValidateValue(*items, item)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

bool IsSupportedToolSchema(const base::DictValue& schema) {
  return ValidateSchema(schema) &&
         ParseSchemaType(schema) == SchemaType::kObject;
}

base::expected<void, DaoToolError> ValidateToolArguments(
    const DaoBrowserToolDefinition& definition,
    const base::DictValue& arguments) {
  if (!IsSupportedToolSchema(definition.input_schema) ||
      !ValidateObject(definition.input_schema, arguments)) {
    return base::unexpected(MakeDaoToolError(
        DaoToolErrorCode::kInvalidArgument,
        "Invalid arguments for browser tool: " + definition.name));
  }
  return base::ok();
}

}  // namespace dao
