// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_site_rules.h"

#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"

namespace dao {

namespace {

// Embedded from pip_site_rules.json at compile time.
constexpr char kPipSiteRulesJson[] = R"json([
  {
    "domain": "bilibili.com",
    "target_selector": "#bilibili-player .bpx-player-video-area"
  }
])json";

std::vector<PipSiteRule> ParseRules() {
  std::vector<PipSiteRule> rules;
  auto parsed = base::JSONReader::Read(kPipSiteRulesJson);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    const std::string* domain = item.GetDict().FindString("domain");
    const std::string* selector = item.GetDict().FindString("target_selector");
    if (domain && selector) {
      rules.push_back({*domain, *selector});
    }
  }
  return rules;
}

}  // namespace

const std::vector<PipSiteRule>& GetAllPipSiteRules() {
  static base::NoDestructor<std::vector<PipSiteRule>> rules(ParseRules());
  return *rules;
}

std::optional<PipSiteRule> GetPipSiteRule(const GURL& url) {
  if (!url.is_valid()) {
    return std::nullopt;
  }
  const std::string& host = url.host();
  for (const auto& rule : GetAllPipSiteRules()) {
    if (host == rule.domain || host == "www." + rule.domain ||
        base::EndsWith(host, "." + rule.domain)) {
      return rule;
    }
  }
  return std::nullopt;
}

}  // namespace dao
