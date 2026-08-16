// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_history_material.h"

#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "crypto/sha2.h"
#include "url/origin.h"

namespace dao {
namespace {

constexpr size_t kMaxLaunchTargets = 12;
constexpr size_t kMaxSourceCandidates = 3;
constexpr size_t kUnknownTargetHashLength = 24;
constexpr size_t kMaxRouteFamiliesPerOrigin = 8;
constexpr base::TimeDelta kHistoryWindow = base::Days(30);

constexpr char kPageFeedSchema[] = R"json({
  "type": "array",
  "items": {
    "type": "object",
    "properties": {
      "title": {"type": "string"},
      "url": {"type": "string"},
      "image": {"type": "string"}
    },
    "required": ["title", "url"],
    "additionalProperties": false
  }
})json";

struct RankedDestination {
  int visit_count = 0;
  int content_visit_count = 0;
  int sensitive_visit_count = 0;
  bool has_repeated_content_family = false;
  std::map<std::string, std::string> content_route_families;
  std::string site_host;
  GURL root_url;
};

constexpr std::array<std::string_view, 14> kSensitiveRouteTokens = {
    "account",  "accounts", "admin",  "auth",   "billing",
    "checkout", "inbox",    "login",  "mail",   "messages",
    "payment",  "settings", "signin", "wallet",
};

constexpr std::array<std::string_view, 6> kNonContentRouteTokens = {
    "discover", "explore", "popular", "search", "trending", "welcome",
};

bool ContainsToken(std::string_view value,
                   std::span<const std::string_view> tokens) {
  size_t token_start = 0;
  for (size_t index = 0; index <= value.size(); ++index) {
    const bool is_token_character =
        index < value.size() && base::IsAsciiAlphaNumeric(value[index]);
    if (is_token_character) {
      continue;
    }
    if (index > token_start &&
        std::ranges::contains(tokens,
                              value.substr(token_start, index - token_start))) {
      return true;
    }
    token_start = index + 1;
  }
  return false;
}

bool IsSensitiveRoute(const GURL& url) {
  return ContainsToken(base::ToLowerASCII(url.host()), kSensitiveRouteTokens) ||
         ContainsToken(base::ToLowerASCII(url.path()), kSensitiveRouteTokens);
}

std::string ContentRouteFamily(const GURL& url) {
  const std::string path = base::ToLowerASCII(url.path());
  if (path == "/" || path.empty() ||
      ContainsToken(path, kSensitiveRouteTokens) ||
      ContainsToken(path, kNonContentRouteTokens)) {
    return {};
  }
  const size_t start = path.find_first_not_of('/');
  if (start == std::string::npos) {
    return {};
  }
  const size_t end = path.find('/', start);
  return path.substr(start, end - start);
}

std::string TargetId(std::string_view site_host) {
  std::string digest =
      base::ToLowerASCII(base::HexEncode(crypto::SHA256HashString(site_host)));
  return "site_" + digest.substr(0, kUnknownTargetHashLength);
}

const char* HomeSourceEligibilityToString(HomeSourceEligibility eligibility) {
  switch (eligibility) {
    case HomeSourceEligibility::kLaunchAndFeed:
      return "launch_and_feed";
    case HomeSourceEligibility::kLaunchOnly:
      return "launch_only";
    case HomeSourceEligibility::kSensitiveLaunchOnly:
      return "sensitive_launch_only";
    case HomeSourceEligibility::kUnsupported:
      return "unsupported";
  }
}

}  // namespace

HomeBootstrapBrief BuildHomeBootstrapBrief(const history::QueryResults& results,
                                           base::Time now,
                                           std::string locale) {
  const base::Time cutoff = now - kHistoryWindow;
  std::map<std::string, RankedDestination> ranked_destinations;
  for (const history::URLResult& result : results) {
    const GURL& url = result.url();
    const base::Time visit_time = result.visit_time();
    if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS() || !url.has_host() ||
        visit_time < cutoff || visit_time > now) {
      continue;
    }
    const url::Origin origin = url::Origin::Create(url);
    const std::string origin_key = origin.Serialize();
    const std::string site_host = base::ToLowerASCII(url.host());
    if (origin.opaque() || site_host.empty()) {
      continue;
    }

    RankedDestination& destination = ranked_destinations[origin_key];
    ++destination.visit_count;
    if (IsSensitiveRoute(url)) {
      ++destination.sensitive_visit_count;
    } else if (std::string family = ContentRouteFamily(url); !family.empty()) {
      ++destination.content_visit_count;
      const auto existing = destination.content_route_families.find(family);
      if (existing != destination.content_route_families.end() &&
          existing->second != url.path()) {
        destination.has_repeated_content_family = true;
      } else if (existing == destination.content_route_families.end() &&
                 destination.content_route_families.size() <
                     kMaxRouteFamiliesPerOrigin) {
        destination.content_route_families.emplace(std::move(family),
                                                   url.path());
      }
    }
    destination.site_host = site_host;
    destination.root_url = origin.GetURL();
  }

  std::vector<std::pair<std::string, RankedDestination>> sorted;
  sorted.reserve(ranked_destinations.size());
  for (auto& [origin, destination] : ranked_destinations) {
    sorted.emplace_back(std::move(origin), std::move(destination));
  }
  std::ranges::sort(sorted, [](const auto& left, const auto& right) {
    if (left.second.visit_count != right.second.visit_count) {
      return left.second.visit_count > right.second.visit_count;
    }
    return TargetId(left.first) < TargetId(right.first);
  });

  std::set<std::string> source_origins;
  for (const auto& [origin, destination] : sorted) {
    if (source_origins.size() == kMaxSourceCandidates) {
      break;
    }
    if (destination.has_repeated_content_family &&
        destination.content_visit_count > destination.sensitive_visit_count) {
      source_origins.insert(origin);
    }
  }

  std::set<std::string> launch_origins = source_origins;
  for (const auto& [origin, destination] : sorted) {
    if (launch_origins.size() == kMaxLaunchTargets) {
      break;
    }
    launch_origins.insert(origin);
  }

  HomeBootstrapBrief brief;
  brief.locale = std::move(locale);
  for (const auto& [origin, destination] : sorted) {
    if (!launch_origins.contains(origin)) {
      continue;
    }
    HomeLaunchTarget target;
    target.id = TargetId(origin);
    target.label_hint = destination.site_host;
    target.url = destination.root_url;
    if (destination.sensitive_visit_count > 0 &&
        destination.sensitive_visit_count >= destination.content_visit_count) {
      target.category_hint = "sensitive";
      target.source_eligibility = HomeSourceEligibility::kSensitiveLaunchOnly;
    } else if (source_origins.contains(origin)) {
      target.category_hint = "content";
      target.source_eligibility = HomeSourceEligibility::kLaunchAndFeed;
    } else {
      target.category_hint = "site";
      target.source_eligibility = HomeSourceEligibility::kLaunchOnly;
    }
    brief.launch_targets.push_back(std::move(target));
  }

  for (const HomeLaunchTarget& target : brief.launch_targets) {
    if (target.source_eligibility != HomeSourceEligibility::kLaunchAndFeed ||
        brief.source_candidates.size() == kMaxSourceCandidates) {
      continue;
    }
    HomeSourceCandidate candidate;
    candidate.launch_target_id = target.id;
    candidate.connector_kind_hint = "page_feed";
    candidate.collection_url = target.url;
    candidate.content_intent = "site_feed";
    candidate.content_kinds.emplace_back("content");
    candidate.schema_source = kPageFeedSchema;
    brief.source_candidates.push_back(std::move(candidate));
  }

  return brief;
}

base::DictValue HomeBootstrapBriefToValue(const HomeBootstrapBrief& brief) {
  base::ListValue launch_targets;
  for (const HomeLaunchTarget& target : brief.launch_targets) {
    launch_targets.Append(
        base::DictValue()
            .Set("id", target.id)
            .Set("label_hint", target.label_hint)
            .Set("url", target.url.spec())
            .Set("category_hint", target.category_hint)
            .Set("source_eligibility",
                 HomeSourceEligibilityToString(target.source_eligibility)));
  }

  base::ListValue source_candidates;
  for (const HomeSourceCandidate& candidate : brief.source_candidates) {
    base::ListValue content_kinds;
    for (const std::string& content_kind : candidate.content_kinds) {
      content_kinds.Append(content_kind);
    }
    source_candidates.Append(
        base::DictValue()
            .Set("launch_target_id", candidate.launch_target_id)
            .Set("connector_kind_hint", candidate.connector_kind_hint)
            .Set("collection_url", candidate.collection_url.spec())
            .Set("content_intent", candidate.content_intent)
            .Set("content_kinds", std::move(content_kinds))
            .Set("schema_source", candidate.schema_source));
  }

  return base::DictValue()
      .Set("launch_targets", std::move(launch_targets))
      .Set("source_candidates", std::move(source_candidates))
      .Set("locale", brief.locale);
}

}  // namespace dao
