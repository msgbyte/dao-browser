// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_DOMAIN_UTILS_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_DOMAIN_UTILS_H_

#include <set>
#include <string>

class Profile;

namespace dao {

// Returns a normalized host suitable for Dream Analysis exclusion matching, or
// an empty string when the input is not a registrable domain.
std::string NormalizeDreamExcludedDomain(const std::string& input);

// Returns true when `host` exactly matches an excluded domain or is one of its
// subdomains. Inputs are normalized before matching.
bool IsDreamDomainExcluded(const std::string& host,
                           const std::set<std::string>& excluded_domains);

// Loads normalized Dream excluded domains from the profile pref.
std::set<std::string> LoadDreamExcludedDomains(Profile* profile);

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_DOMAIN_UTILS_H_
