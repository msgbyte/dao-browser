// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_sample_shape.h"

#include <string>

#include "base/containers/flat_set.h"

namespace dao {
namespace {

std::string ValueKind(const base::Value& value) {
  if (value.is_none()) {
    return "null";
  }
  if (value.is_bool()) {
    return "boolean";
  }
  if (value.is_int() || value.is_double()) {
    return "number";
  }
  if (value.is_string()) {
    return "string";
  }
  if (value.is_list()) {
    return "array";
  }
  if (value.is_dict()) {
    return "object";
  }
  return "unsupported";
}

base::ListValue DistinctKinds(const base::ListValue& values) {
  base::flat_set<std::string> kinds;
  for (const base::Value& value : values) {
    kinds.insert(ValueKind(value));
  }
  base::ListValue result;
  for (const std::string& kind : kinds) {
    result.Append(kind);
  }
  return result;
}

base::ListValue DistinctKinds(const base::DictValue& values) {
  base::flat_set<std::string> kinds;
  for (auto [key, value] : values) {
    static_cast<void>(key);
    kinds.insert(ValueKind(value));
  }
  base::ListValue result;
  for (const std::string& kind : kinds) {
    result.Append(kind);
  }
  return result;
}

}  // namespace

base::DictValue BuildHomeConnectorSampleShape(const base::Value& sample) {
  base::DictValue shape;
  shape.Set("kind", ValueKind(sample));
  if (sample.is_string()) {
    shape.Set("state", sample.GetString().empty() ? "empty" : "non_empty");
  } else if (sample.is_list()) {
    const base::ListValue& values = sample.GetList();
    shape.Set("state", values.empty() ? "empty" : "non_empty");
    shape.Set("item_kinds", DistinctKinds(values));
  } else if (sample.is_dict()) {
    const base::DictValue& values = sample.GetDict();
    shape.Set("state", values.empty() ? "empty" : "non_empty");
    shape.Set("value_kinds", DistinctKinds(values));
  }
  return shape;
}

}  // namespace dao
