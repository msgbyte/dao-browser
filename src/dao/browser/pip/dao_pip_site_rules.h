// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_PIP_DAO_PIP_SITE_RULES_H_
#define DAO_BROWSER_PIP_DAO_PIP_SITE_RULES_H_

#include <optional>
#include <string>
#include <vector>

#include "url/gurl.h"

namespace dao {

struct PipSiteRule {
  // Domain pattern, e.g. "bilibili.com" matches *.bilibili.com.
  std::string domain;

  // CSS selector for the DOM element to PiP instead of the video.
  std::string target_selector;
};

// Returns the PiP site rule matching the given URL, or nullopt if none.
std::optional<PipSiteRule> GetPipSiteRule(const GURL& url);

// Returns all configured PiP site rules.
const std::vector<PipSiteRule>& GetAllPipSiteRules();

}  // namespace dao

#endif  // DAO_BROWSER_PIP_DAO_PIP_SITE_RULES_H_
