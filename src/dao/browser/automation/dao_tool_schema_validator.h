// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AUTOMATION_DAO_TOOL_SCHEMA_VALIDATOR_H_
#define DAO_BROWSER_AUTOMATION_DAO_TOOL_SCHEMA_VALIDATOR_H_

#include "base/types/expected.h"
#include "base/values.h"
#include "dao/browser/automation/dao_browser_tool_types.h"

namespace dao {

struct DaoBrowserToolDefinition;

bool IsSupportedToolSchema(const base::DictValue& schema);

base::expected<void, DaoToolError> ValidateToolArguments(
    const DaoBrowserToolDefinition& definition,
    const base::DictValue& arguments);

}  // namespace dao

#endif  // DAO_BROWSER_AUTOMATION_DAO_TOOL_SCHEMA_VALIDATOR_H_
