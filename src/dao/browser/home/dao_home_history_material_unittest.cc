// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_history_material.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/json/json_writer.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/browser/url_row.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace dao {
namespace {

history::URLResult Result(const char* url,
                          const char16_t* title,
                          base::Time visit_time) {
  history::URLResult result(GURL(url), visit_time);
  result.set_title(title);
  return result;
}

TEST(DaoHomeHistoryMaterialTest, BuildsActionsWithoutReportMaterial) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://github.com/private/repo?token=secret", u"Secret PR", now),
      Result("https://github.com/private/issue?token=secret", u"Secret issue",
             now),
      Result("https://www.bilibili.com/video/BV-secret", u"Private title", now),
      Result("https://linear.app/acme/issue/DAO-1", u"Roadmap", now),
      Result("https://translate.google.com/?q=private", u"Translate", now),
      Result("https://mail.google.com/mail/u/1/#inbox", u"Inbox", now),
      Result("https://www.feishu.cn/secret", u"Feishu secret", now),
      Result("https://unknown.example.test/private?token=secret",
             u"Unknown secret", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(7u, brief.launch_targets.size());
  EXPECT_TRUE(brief.launch_targets[0].id.starts_with("site_"));
  EXPECT_EQ("github.com", brief.launch_targets[0].label_hint);
  EXPECT_EQ(GURL("https://github.com/"), brief.launch_targets[0].url);
  EXPECT_EQ(HomeSourceEligibility::kLaunchAndFeed,
            brief.launch_targets[0].source_eligibility);
  const auto gmail = std::ranges::find_if(
      brief.launch_targets, [](const HomeLaunchTarget& target) {
        return target.label_hint == "mail.google.com";
      });
  ASSERT_NE(brief.launch_targets.end(), gmail);
  EXPECT_EQ(HomeSourceEligibility::kSensitiveLaunchOnly,
            gmail->source_eligibility);
  EXPECT_EQ("en-US", brief.locale);
  ASSERT_EQ(1u, brief.source_candidates.size());
  EXPECT_FALSE(brief.source_candidates[0].schema_source.empty());
  EXPECT_EQ(brief.launch_targets[0].id,
            brief.source_candidates[0].launch_target_id);
  EXPECT_EQ(GURL("https://github.com/"),
            brief.source_candidates[0].collection_url);
  EXPECT_EQ("site_feed", brief.source_candidates[0].content_intent);
  EXPECT_EQ((std::vector<std::string>{"content"}),
            brief.source_candidates[0].content_kinds);

  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(HomeBootstrapBriefToValue(brief), &json));
  EXPECT_EQ(std::string::npos, json.find("visit_count"));
  EXPECT_EQ(std::string::npos, json.find("time_buckets"));
  EXPECT_EQ(std::string::npos, json.find("Secret PR"));
  EXPECT_EQ(std::string::npos, json.find("token"));
  EXPECT_EQ(std::string::npos, json.find("private"));
  EXPECT_EQ(std::string::npos, json.find("module_source"));
  EXPECT_NE(std::string::npos, json.find("schema_source"));
  EXPECT_NE(std::string::npos, json.find("collection_url"));
  EXPECT_NE(std::string::npos, json.find("content_intent"));
  EXPECT_NE(std::string::npos, json.find("content_kinds"));
}

TEST(DaoHomeHistoryMaterialTest, BoundsLaunchTargetsAndEligibleCandidates) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  std::vector<history::URLResult> rows;
  for (int i = 0; i < 20; ++i) {
    rows.push_back(
        Result(("https://site" + std::to_string(i) + ".test/path").c_str(),
               u"Title", now));
  }
  rows.push_back(Result("https://github.com/repo/a", u"GitHub", now));
  rows.push_back(Result("https://github.com/repo/b", u"GitHub", now));
  rows.push_back(Result("https://github.com/repo/c", u"GitHub", now));
  rows.push_back(Result("https://www.bilibili.com/video/a", u"Bilibili", now));
  rows.push_back(Result("https://www.bilibili.com/video/b", u"Bilibili", now));
  rows.push_back(Result("https://mail.google.com/a", u"Gmail", now));
  rows.push_back(Result("https://www.feishu.cn/a", u"Feishu", now));
  history::QueryResults results;
  results.SetURLResults(std::move(rows));

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(12u, brief.launch_targets.size());
  EXPECT_LE(brief.source_candidates.size(), 3u);
  ASSERT_EQ(2u, brief.source_candidates.size());
  const auto source_labels = [&]() {
    std::vector<std::string> labels;
    for (const HomeSourceCandidate& candidate : brief.source_candidates) {
      const auto target = std::ranges::find_if(
          brief.launch_targets, [&](const HomeLaunchTarget& launch_target) {
            return launch_target.id == candidate.launch_target_id;
          });
      EXPECT_NE(brief.launch_targets.end(), target);
      if (target != brief.launch_targets.end()) {
        labels.push_back(target->label_hint);
      }
    }
    return labels;
  }();
  EXPECT_EQ((std::vector<std::string>{"github.com", "www.bilibili.com"}),
            source_labels);
  for (const HomeSourceCandidate& candidate : brief.source_candidates) {
    EXPECT_EQ("site_feed", candidate.content_intent);
    EXPECT_EQ((std::vector<std::string>{"content"}), candidate.content_kinds);
  }
}

TEST(DaoHomeHistoryMaterialTest, OrdersEqualFrequencyLaunchTargetsByKey) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://github.com/private", u"GitHub", now),
      Result("https://www.bilibili.com/private", u"Bilibili", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(2u, brief.launch_targets.size());
  EXPECT_LT(brief.launch_targets[0].id, brief.launch_targets[1].id);
  EXPECT_NE(brief.launch_targets[0].label_hint,
            brief.launch_targets[1].label_hint);
}

TEST(DaoHomeHistoryMaterialTest, TreatsEveryDomainWithGenericRules) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://evilgithub.com/private", u"Evil", now),
      Result("https://github.com.evil.test/private", u"Evil", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(2u, brief.launch_targets.size());
  EXPECT_TRUE(brief.launch_targets[0].id.starts_with("site_"));
  EXPECT_TRUE(brief.launch_targets[1].id.starts_with("site_"));
  EXPECT_EQ(HomeSourceEligibility::kLaunchOnly,
            brief.launch_targets[0].source_eligibility);
  EXPECT_EQ(HomeSourceEligibility::kLaunchOnly,
            brief.launch_targets[1].source_eligibility);
  EXPECT_TRUE(brief.source_candidates.empty());
}

TEST(DaoHomeHistoryMaterialTest, UsesStableExperienceCompatibleIdsForDomains) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults first_results;
  first_results.SetURLResults(
      {Result("https://news.example.com/private/one?token=secret",
              u"First secret", now)});
  history::QueryResults second_results;
  second_results.SetURLResults(
      {Result("https://news.example.com/other/two?query=private",
              u"Other title", now)});

  HomeBootstrapBrief first =
      BuildHomeBootstrapBrief(first_results, now, "en-US");
  HomeBootstrapBrief second =
      BuildHomeBootstrapBrief(second_results, now, "en-US");
  ASSERT_EQ(1u, first.launch_targets.size());
  ASSERT_EQ(1u, second.launch_targets.size());
  EXPECT_EQ(first.launch_targets[0].id, second.launch_targets[0].id);
  EXPECT_TRUE(first.launch_targets[0].id.starts_with("site_"));
  EXPECT_LE(first.launch_targets[0].id.size(), 64u);
  for (char character : first.launch_targets[0].id) {
    EXPECT_TRUE((character >= 'a' && character <= 'z') ||
                (character >= '0' && character <= '9') || character == '_' ||
                character == '-');
  }
  EXPECT_EQ("news.example.com", first.launch_targets[0].label_hint);
  EXPECT_EQ(GURL("https://news.example.com/"), first.launch_targets[0].url);
  EXPECT_EQ(std::string::npos, first.launch_targets[0].id.find("example"));
  EXPECT_EQ(std::string::npos, first.launch_targets[0].id.find("secret"));
}

TEST(DaoHomeHistoryMaterialTest, GivesDifferentUnknownDomainsDifferentIds) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://example.com/path", u"Same title", now),
      Result("https://example.org/path", u"Same title", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(2u, brief.launch_targets.size());
  EXPECT_NE(brief.launch_targets[0].id, brief.launch_targets[1].id);
}

TEST(DaoHomeHistoryMaterialTest,
     DiscoversGenericFeedCandidatesFromDistinctContentRoutes) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://news.example.test/articles/one", u"One", now),
      Result("https://news.example.test/articles/two", u"Two", now),
      Result("https://another.test/posts/one", u"One", now),
      Result("https://another.test/posts/two", u"Two", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(2u, brief.source_candidates.size());
  for (const HomeSourceCandidate& candidate : brief.source_candidates) {
    EXPECT_EQ("page_feed", candidate.connector_kind_hint);
    EXPECT_EQ("site_feed", candidate.content_intent);
    EXPECT_EQ((std::vector<std::string>{"content"}), candidate.content_kinds);
  }
}

TEST(DaoHomeHistoryMaterialTest, KeepsSensitiveAndUtilityRoutesLaunchOnly) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://portal.example.test/account/profile", u"Profile", now),
      Result("https://portal.example.test/settings/security", u"Settings", now),
      Result("https://lookup.test/search/one", u"Search", now),
      Result("https://lookup.test/search/two", u"Search", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(2u, brief.launch_targets.size());
  EXPECT_TRUE(brief.source_candidates.empty());
  const auto sensitive = std::ranges::find_if(
      brief.launch_targets, [](const HomeLaunchTarget& target) {
        return target.label_hint == "portal.example.test";
      });
  ASSERT_NE(brief.launch_targets.end(), sensitive);
  EXPECT_EQ(HomeSourceEligibility::kSensitiveLaunchOnly,
            sensitive->source_eligibility);
}

TEST(DaoHomeHistoryMaterialTest, ReservesLaunchSlotsForFeedCandidates) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  std::vector<history::URLResult> rows;
  for (int site = 0; site < 12; ++site) {
    for (int visit = 0; visit < 3; ++visit) {
      rows.push_back(
          Result(("https://tool" + std::to_string(site) + ".test/").c_str(),
                 u"Tool", now));
    }
  }
  rows.push_back(Result("https://content.test/articles/one", u"One", now));
  rows.push_back(Result("https://content.test/articles/two", u"Two", now));
  history::QueryResults results;
  results.SetURLResults(std::move(rows));

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(12u, brief.launch_targets.size());
  ASSERT_EQ(1u, brief.source_candidates.size());
  const std::string& source_id = brief.source_candidates[0].launch_target_id;
  EXPECT_NE(brief.launch_targets.end(),
            std::ranges::find_if(brief.launch_targets,
                                 [&](const HomeLaunchTarget& target) {
                                   return target.id == source_id;
                                 }));
}

TEST(DaoHomeHistoryMaterialTest, BoundsGenericFeedCandidates) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  std::vector<history::URLResult> rows;
  for (int site = 0; site < 6; ++site) {
    rows.push_back(Result(
        ("https://content" + std::to_string(site) + ".test/items/one").c_str(),
        u"One", now));
    rows.push_back(Result(
        ("https://content" + std::to_string(site) + ".test/items/two").c_str(),
        u"Two", now));
  }
  history::QueryResults results;
  results.SetURLResults(std::move(rows));

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  EXPECT_EQ(3u, brief.source_candidates.size());
}

TEST(DaoHomeHistoryMaterialTest, KeepsOriginsWithDifferentPortsSeparate) {
  const base::Time now = base::Time::UnixEpoch() + base::Days(100);
  history::QueryResults results;
  results.SetURLResults({
      Result("https://example.test:8443/items/one", u"One", now),
      Result("https://example.test:8443/items/two", u"Two", now),
      Result("https://example.test:9443/items/one", u"One", now),
      Result("https://example.test:9443/items/two", u"Two", now),
  });

  HomeBootstrapBrief brief = BuildHomeBootstrapBrief(results, now, "en-US");
  ASSERT_EQ(2u, brief.launch_targets.size());
  EXPECT_NE(brief.launch_targets[0].id, brief.launch_targets[1].id);
  EXPECT_NE(brief.launch_targets[0].url, brief.launch_targets[1].url);
  EXPECT_EQ(2u, brief.source_candidates.size());
}

}  // namespace
}  // namespace dao
