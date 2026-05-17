// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/workspace/text_only_filter.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "base/files/file_path.h"
#include "base/strings/string_util.h"

namespace dao {

namespace {

constexpr std::array<std::string_view, 10> kAllowedExtensions = {
    ".txt", ".md", ".json", ".csv", ".yaml",
    ".yml", ".html", ".xml", ".log", ".tsv",
};

constexpr size_t kNulProbeBytes = 8 * 1024;

}  // namespace

bool IsTextExtensionAllowed(const base::FilePath& path) {
  std::string ext = path.Extension();
  if (ext.empty()) {
    return false;
  }
  std::string lower = base::ToLowerASCII(ext);
  return std::find(kAllowedExtensions.begin(), kAllowedExtensions.end(),
                   lower) != kAllowedExtensions.end();
}

bool ContainsNulByte(std::string_view content) {
  std::string_view probe =
      content.substr(0, std::min(content.size(), kNulProbeBytes));
  return probe.find('\0') != std::string_view::npos;
}

}  // namespace dao
