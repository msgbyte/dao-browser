// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_domain_utils.h"

#include <utility>

#include "base/strings/string_util.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/dao_pref_names.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/url_util.h"
#include "url/gurl.h"
#include "url/url_util.h"

namespace dao {

std::string NormalizeDreamExcludedDomain(const std::string& input) {
  std::string trimmed =
      base::ToLowerASCII(base::TrimWhitespaceASCII(input, base::TRIM_ALL));
  if (trimmed.empty()) {
    return std::string();
  }

  GURL parsed(trimmed);
  if (!parsed.is_valid() || !parsed.has_host()) {
    parsed = GURL("https://" + trimmed);
  }
  if (!parsed.is_valid() || !parsed.has_host()) {
    return std::string();
  }

  std::string host = base::ToLowerASCII(parsed.host());
  while (!host.empty() && host.back() == '.') {
    host.pop_back();
  }
  if (host.empty() || host.find('.') == std::string::npos ||
      net::HostStringIsLocalhost(host) || url::HostIsIPAddress(host)) {
    return std::string();
  }

  const size_t registry_length =
      net::registry_controlled_domains::GetRegistryLength(
          GURL("https://" + host),
          net::registry_controlled_domains::EXCLUDE_UNKNOWN_REGISTRIES,
          net::registry_controlled_domains::EXCLUDE_PRIVATE_REGISTRIES);
  if (registry_length == 0 || registry_length == std::string::npos ||
      registry_length == host.size()) {
    return std::string();
  }
  return host;
}

bool IsDreamDomainExcluded(const std::string& host,
                           const std::set<std::string>& excluded_domains) {
  const std::string normalized = NormalizeDreamExcludedDomain(host);
  if (normalized.empty()) {
    return false;
  }
  for (const std::string& excluded : excluded_domains) {
    if (normalized == excluded) {
      return true;
    }
    if (normalized.size() > excluded.size() &&
        normalized.compare(normalized.size() - excluded.size(),
                           excluded.size(), excluded) == 0 &&
        normalized[normalized.size() - excluded.size() - 1] == '.') {
      return true;
    }
  }
  return false;
}

std::set<std::string> LoadDreamExcludedDomains(Profile* profile) {
  std::set<std::string> domains;
  if (!profile) {
    return domains;
  }
  for (const base::Value& value :
       profile->GetPrefs()->GetList(prefs::kDaoDreamExcludedDomains)) {
    if (!value.is_string()) {
      continue;
    }
    std::string normalized = NormalizeDreamExcludedDomain(value.GetString());
    if (!normalized.empty()) {
      domains.insert(std::move(normalized));
    }
  }
  return domains;
}

}  // namespace dao
