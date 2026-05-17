// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_WORKSPACE_TEXT_ONLY_FILTER_H_
#define DAO_BROWSER_AGENT_WORKSPACE_TEXT_ONLY_FILTER_H_

#include <string_view>

#include "base/files/file_path.h"

namespace dao {

// Returns true if `path` has an extension on the workspace text allowlist
// (.txt .md .json .csv .yaml .yml .html .xml .log .tsv). Case-insensitive.
bool IsTextExtensionAllowed(const base::FilePath& path);

// First-8KB NUL byte probe — defense in depth for content that slipped
// through the extension check.
bool ContainsNulByte(std::string_view content);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_WORKSPACE_TEXT_ONLY_FILTER_H_
