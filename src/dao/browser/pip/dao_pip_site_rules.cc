// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/pip/dao_pip_site_rules.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"

namespace dao {

namespace {

// Embedded from pip_site_rules.json at compile time.
constexpr char kPipSiteRulesJson[] = R"json([
  {
    "domain": "bilibili.com",
    "target_selector": "#bilibili-player .bpx-player-video-area",
    "custom_styles": [
      ".bpx-player-sending-bar{display:none!important}"
    ]
  }
])json";

std::vector<PipSiteRule> ParseRules() {
  std::vector<PipSiteRule> rules;
  auto parsed =
      base::JSONReader::Read(kPipSiteRulesJson, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return rules;
  }
  for (const auto& item : parsed->GetList()) {
    if (!item.is_dict()) {
      continue;
    }
    const std::string* domain = item.GetDict().FindString("domain");
    const std::string* selector = item.GetDict().FindString("target_selector");
    std::vector<std::string> custom_styles;
    if (const auto* styles = item.GetDict().FindList("custom_styles")) {
      for (const auto& style : *styles) {
        if (style.is_string()) {
          custom_styles.push_back(style.GetString());
        }
      }
    }
    if (domain && selector) {
      rules.emplace_back(*domain, *selector, std::move(custom_styles));
    }
  }
  return rules;
}

}  // namespace

PipSiteRule::PipSiteRule() = default;

PipSiteRule::PipSiteRule(std::string domain_value,
                         std::string target_selector_value,
                         std::vector<std::string> custom_styles_value)
    : domain(std::move(domain_value)),
      target_selector(std::move(target_selector_value)),
      custom_styles(std::move(custom_styles_value)) {}

PipSiteRule::PipSiteRule(const PipSiteRule& other) = default;

PipSiteRule& PipSiteRule::operator=(const PipSiteRule& other) = default;

PipSiteRule::~PipSiteRule() = default;

const std::vector<PipSiteRule>& GetAllPipSiteRules() {
  static base::NoDestructor<std::vector<PipSiteRule>> rules(ParseRules());
  return *rules;
}

std::optional<PipSiteRule> GetPipSiteRule(const GURL& url) {
  if (!url.is_valid()) {
    return std::nullopt;
  }
  std::string_view host = url.host();
  for (const auto& rule : GetAllPipSiteRules()) {
    if (host == rule.domain || host == "www." + rule.domain ||
        base::EndsWith(host, "." + rule.domain)) {
      return rule;
    }
  }
  return std::nullopt;
}

}  // namespace dao
