// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/icu_test_util.h"
#include "base/test/run_until.h"
#include "base/test/scoped_libc_timezone_override.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_dream_material_collector.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/agent/dao_dream_service_factory.h"
#include "dao/browser/agent/dao_weekly_dream_material_collector.h"
#include "dao/browser/dao_pref_names.h"
#include "dao/browser/ui/webui/dao_agent_ui.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Builds a local base::Time for Y-M-D h:m.
base::Time LocalTime(int year, int month, int day, int hour, int minute) {
  base::Time::Exploded e = {};
  e.year = year;
  e.month = month;
  e.day_of_month = day;
  e.hour = hour;
  e.minute = minute;
  base::Time t;
  CHECK(base::Time::FromLocalExploded(e, &t));
  return t;
}

void SetForegroundDurationForUrl(history::HistoryService* history,
                                 const GURL& url,
                                 base::TimeDelta duration) {
  history::QueryURLAndVisitsResult result;
  base::CancelableTaskTracker tracker;
  base::RunLoop loop;
  history->QueryURLAndVisits(
      url, history::VisitQuery404sPolicy::kInclude404s,
      base::BindLambdaForTesting(
          [&](history::QueryURLAndVisitsResult query_result) {
            result = std::move(query_result);
            loop.Quit();
          }),
      &tracker);
  loop.Run();

  ASSERT_TRUE(result.success);
  ASSERT_FALSE(result.visits.empty());

  history::VisitContextAnnotations annotations;
  annotations.total_foreground_duration = duration;
  history->SetOnCloseContextAnnotationsForVisit(
      result.visits.back().visit_id, annotations);
}

bool SaveConversationSummaryForTest(DaoAgentMemoryService* memory,
                                    ConversationSummary summary) {
  bool saved = false;
  base::RunLoop loop;
  memory->SaveConversationSummary(
      std::move(summary), base::BindLambdaForTesting([&](bool ok) {
        saved = ok;
        loop.Quit();
      }));
  loop.Run();
  return saved;
}

bool SaveConversationMessagesForTest(
    DaoAgentMemoryService* memory,
    const std::string& session_id,
    std::vector<ConversationMessage> messages) {
  bool saved = false;
  base::RunLoop loop;
  memory->SaveConversationMessages(
      session_id, std::move(messages),
      base::BindLambdaForTesting([&](bool ok) {
        saved = ok;
        loop.Quit();
      }));
  loop.Run();
  return saved;
}

bool SaveDreamReportForTest(DaoAgentMemoryService* memory,
                            DreamReport report) {
  bool saved = false;
  base::RunLoop loop;
  memory->SaveDreamReport(
      std::move(report), base::BindLambdaForTesting([&](bool ok) {
        saved = ok;
        loop.Quit();
      }));
  loop.Run();
  return saved;
}

std::optional<DreamReport> LoadDreamReportForTest(
    DaoAgentMemoryService* memory,
    const std::string& dream_date) {
  std::optional<DreamReport> report;
  base::RunLoop loop;
  memory->GetDreamReportByDate(
      dream_date,
      base::BindLambdaForTesting([&](std::optional<DreamReport> value) {
        report = std::move(value);
        loop.Quit();
      }));
  loop.Run();
  return report;
}

bool SaveWeeklyDreamReportForTest(
    DaoAgentMemoryService* memory,
    WeeklyDreamReport report,
    std::vector<WeeklyDreamSource> sources = {}) {
  bool saved = false;
  base::RunLoop loop;
  memory->SaveWeeklyDreamReport(
      std::move(report), std::move(sources),
      base::BindLambdaForTesting([&](bool ok) {
        saved = ok;
        loop.Quit();
      }));
  loop.Run();
  return saved;
}

std::optional<WeeklyDreamReport> LoadWeeklyDreamReportForTest(
    DaoAgentMemoryService* memory,
    const std::string& week_start) {
  std::optional<WeeklyDreamReport> report;
  base::RunLoop loop;
  memory->GetWeeklyDreamReportByWeekStart(
      week_start,
      base::BindLambdaForTesting(
          [&](std::optional<WeeklyDreamReport> value) {
            report = std::move(value);
            loop.Quit();
          }));
  loop.Run();
  return report;
}

std::optional<WeeklyDreamReport> WaitForWeeklyDreamReportForTest(
    DaoAgentMemoryService* memory,
    const std::string& week_start) {
  for (int attempt = 0; attempt < 20; ++attempt) {
    std::optional<WeeklyDreamReport> report =
        LoadWeeklyDreamReportForTest(memory, week_start);
    if (report) {
      return report;
    }
    base::RunLoop().RunUntilIdle();
  }
  return std::nullopt;
}

std::vector<WeeklyDreamSource> LoadWeeklyDreamSourcesForTest(
    DaoAgentMemoryService* memory,
    int64_t report_id) {
  std::vector<WeeklyDreamSource> sources;
  base::RunLoop loop;
  memory->GetWeeklyDreamSources(
      report_id,
      base::BindLambdaForTesting(
          [&](std::vector<WeeklyDreamSource> value) {
            sources = std::move(value);
            loop.Quit();
          }));
  loop.Run();
  return sources;
}

WeeklyDreamMaterial CollectWeeklyMaterialForTest(
    Profile* profile,
    DaoAgentMemoryService* memory,
    base::Time window_start,
    base::Time window_end,
    const std::string& week_start,
    const std::string& week_end) {
  WeeklyDreamMaterial material;
  WeeklyDreamMaterialCollector collector(profile, memory);
  base::RunLoop loop;
  collector.Collect(
      window_start, window_end, week_start, week_end,
      base::BindLambdaForTesting([&](WeeklyDreamMaterial result) {
        material = std::move(result);
        loop.Quit();
      }));
  loop.Run();
  return material;
}

const WeeklyDreamSource* FindWeeklySourceByLocator(
    const std::vector<WeeklyDreamSource>& sources,
    const std::string& local_locator) {
  auto it = std::find_if(
      sources.begin(), sources.end(),
      [&](const WeeklyDreamSource& source) {
        return source.local_locator == local_locator;
      });
  return it == sources.end() ? nullptr : &*it;
}

void ExpectWeeklyWindow(base::Time now,
                        const std::string& expected_start_label,
                        const std::string& expected_end_label,
                        base::Time* start_out = nullptr,
                        base::Time* end_out = nullptr) {
  base::Time start;
  base::Time end;
  std::string week_start;
  std::string week_end;
  DaoDreamService::LatestCompletedWeeklyWindow(
      now, &start, &end, &week_start, &week_end);

  EXPECT_EQ(expected_start_label, week_start);
  EXPECT_EQ(expected_end_label, week_end);
  for (base::Time boundary : {start, end}) {
    base::Time::Exploded exploded;
    boundary.LocalExplode(&exploded);
    EXPECT_EQ(1, exploded.day_of_week);
    EXPECT_EQ(6, exploded.hour);
    EXPECT_EQ(0, exploded.minute);
    EXPECT_EQ(0, exploded.second);
  }
  if (start_out) {
    *start_out = start;
  }
  if (end_out) {
    *end_out = end;
  }
}

}  // namespace

using DaoDreamStaticTest = InProcessBrowserTest;

// --- Pure-logic tests on the static helpers ---

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, WeeklyDreamPrefDefaultsOff) {
  EXPECT_FALSE(browser()->profile()->GetPrefs()->GetBoolean(
      prefs::kDaoDreamWeeklyEnabled));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, DreamDateAttribution) {
  // 23:00 on June 10 → dream day = June 10.
  EXPECT_EQ("2026-06-10",
            DaoDreamService::DreamDateFor(LocalTime(2026, 6, 10, 23, 0)));
  // 01:30 on June 11 → still June 10's dream day.
  EXPECT_EQ("2026-06-10",
            DaoDreamService::DreamDateFor(LocalTime(2026, 6, 11, 1, 30)));
  // 14:00 on June 11 (manual / catch-up daytime) → June 11.
  EXPECT_EQ("2026-06-11",
            DaoDreamService::DreamDateFor(LocalTime(2026, 6, 11, 14, 0)));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, NightWindow) {
  EXPECT_TRUE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 10, 22, 0)));
  EXPECT_TRUE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 11, 2, 0)));
  EXPECT_TRUE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 11, 5, 59)));
  EXPECT_FALSE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 11, 6, 0)));
  EXPECT_FALSE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 10, 21, 59)));
  EXPECT_FALSE(DaoDreamService::IsNightTime(LocalTime(2026, 6, 10, 12, 0)));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, MaterialWindow) {
  base::Time start;
  base::Time end;
  // Querying June 10's window at June 11 01:00: 06-10 06:00 → 01:00 now.
  const base::Time now = LocalTime(2026, 6, 11, 1, 0);
  DaoDreamService::MaterialWindowFor("2026-06-10", now, &start, &end);
  EXPECT_EQ(LocalTime(2026, 6, 10, 6, 0), start);
  EXPECT_EQ(now, end);  // now < hard_end (06-11 06:00)
  // Querying it the next afternoon (catch-up): clamps to 06-11 06:00.
  DaoDreamService::MaterialWindowFor("2026-06-10",
                                     LocalTime(2026, 6, 11, 15, 0), &start,
                                     &end);
  EXPECT_EQ(LocalTime(2026, 6, 11, 6, 0), end);
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest,
                       LatestCompletedWeeklyWindowAroundMondayBoundary) {
  // Sunday and Monday before 06:00 still point at the week that ended on the
  // previous Monday. The new completed week becomes eligible exactly at
  // Monday 06:00 and remains the only automatic catch-up target on Tuesday.
  ExpectWeeklyWindow(LocalTime(2026, 7, 12, 12, 0), "2026-06-29",
                     "2026-07-06");
  ExpectWeeklyWindow(LocalTime(2026, 7, 13, 5, 59), "2026-06-29",
                     "2026-07-06");
  ExpectWeeklyWindow(LocalTime(2026, 7, 13, 6, 0), "2026-07-06",
                     "2026-07-13");
  ExpectWeeklyWindow(LocalTime(2026, 7, 13, 12, 0), "2026-07-06",
                     "2026-07-13");
  ExpectWeeklyWindow(LocalTime(2026, 7, 14, 12, 0), "2026-07-06",
                     "2026-07-13");
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest,
                       LatestCompletedWeeklyWindowAcrossLosAngelesDst) {
  base::test::ScopedRestoreDefaultTimezone scoped_timezone(
      "America/Los_Angeles");
  base::test::ScopedLibcTimezoneOverride scoped_libc_timezone(
      "America/Los_Angeles");

  base::Time spring_start;
  base::Time spring_end;
  ExpectWeeklyWindow(LocalTime(2026, 3, 9, 6, 0), "2026-03-02",
                     "2026-03-09", &spring_start, &spring_end);
  EXPECT_EQ(base::Hours(167), spring_end - spring_start);

  base::Time fall_start;
  base::Time fall_end;
  ExpectWeeklyWindow(LocalTime(2026, 11, 2, 6, 0), "2026-10-26",
                     "2026-11-02", &fall_start, &fall_end);
  EXPECT_EQ(base::Hours(169), fall_end - fall_start);
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, SearchQueryExtraction) {
  EXPECT_EQ("rust async", DreamMaterialCollector::ExtractSearchQuery(
                              "https://www.google.com/search?q=rust+async"));
  EXPECT_EQ("天气", DreamMaterialCollector::ExtractSearchQuery(
                        "https://www.baidu.com/s?wd=%E5%A4%A9%E6%B0%94"));
  // Non-search URL → empty.
  EXPECT_EQ("", DreamMaterialCollector::ExtractSearchQuery(
                    "https://github.com/anthropics/claude-code"));
  // Search domain but no query param → empty.
  EXPECT_EQ("", DreamMaterialCollector::ExtractSearchQuery(
                    "https://www.google.com/maps"));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, DreamDomainExclusionNormalization) {
  EXPECT_EQ("example.com",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                " HTTPS://Example.COM:443/path?q=1 "));
  EXPECT_EQ("app.example.com",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                "app.example.com."));
  EXPECT_EQ("",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(""));
  EXPECT_EQ("",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                "localhost"));
  EXPECT_EQ("",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting("com"));
  EXPECT_EQ("",
            DreamMaterialCollector::NormalizeExcludedDomainForTesting(
                "127.0.0.1"));
}

IN_PROC_BROWSER_TEST_F(DaoDreamStaticTest, DreamDomainExclusionMatching) {
  std::set<std::string> excluded = {"example.com"};
  EXPECT_TRUE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "example.com", excluded));
  EXPECT_TRUE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "app.example.com", excluded));
  EXPECT_FALSE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "notexample.com", excluded));
  EXPECT_FALSE(DreamMaterialCollector::IsDomainExcludedForTesting(
      "example.co", excluded));
}

// --- Fixture with memory + dream enabled ---

class DaoDreamBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    PrefService* pref_service = browser()->profile()->GetPrefs();
    pref_service->SetBoolean(prefs::kDaoAgentMemoryEnabled, true);
    pref_service->SetBoolean(prefs::kDaoDreamEnabled, true);
  }

  DaoDreamService* dream_service() {
    return DaoDreamServiceFactory::GetForProfile(browser()->profile());
  }
};

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, ServiceGatedByPrefs) {
  EXPECT_NE(nullptr, dream_service());
  browser()->profile()->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled,
                                               false);
  EXPECT_EQ(nullptr, dream_service());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorRedactsUrlsAndAggregates) {
  // Seed history inside the window.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  history->AddPage(GURL("https://github.com/foo/bar?token=SECRET123"),
                   now - base::Hours(1), history::SOURCE_BROWSED);
  history->AddPage(GURL("https://github.com/foo/baz"), now - base::Hours(2),
                   history::SOURCE_BROWSED);
  history->AddPage(GURL("https://www.google.com/search?q=hello+world"),
                   now - base::Hours(3), history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  // Privacy invariant: the serialized pack contains no full URL and no
  // query params from visited URLs.
  std::string json;
  base::JSONWriter::Write(pack, &json);
  EXPECT_EQ(std::string::npos, json.find("SECRET123"));
  EXPECT_EQ(std::string::npos, json.find("/foo/bar"));
  EXPECT_EQ(std::string::npos, json.find("https://github.com"));

  // Aggregation: github.com appears once with visit_count 2.
  const base::ListValue* domains = pack.FindList("history");
  ASSERT_TRUE(domains);
  bool found_github = false;
  for (const base::Value& d : *domains) {
    const base::DictValue& dict = d.GetDict();
    const std::string* domain = dict.FindString("domain");
    ASSERT_TRUE(domain);
    if (*domain == "github.com") {
      found_github = true;
      EXPECT_EQ(2, dict.FindInt("visit_count").value_or(0));
    }
  }
  EXPECT_TRUE(found_github);

  const base::DictValue* stats = pack.FindDict("stats");
  ASSERT_TRUE(stats);
  const base::ListValue* source_domains = stats->FindList("source_domains");
  ASSERT_TRUE(source_domains);
  bool source_has_github = false;
  bool source_has_google = false;
  for (const base::Value& domain : *source_domains) {
    if (!domain.is_string()) {
      continue;
    }
    source_has_github |= domain.GetString() == "github.com";
    source_has_google |= domain.GetString() == "www.google.com";
  }
  EXPECT_TRUE(source_has_github);
  EXPECT_TRUE(source_has_google);

  // Search extraction.
  const base::ListValue* queries = pack.FindList("search_queries");
  ASSERT_TRUE(queries);
  ASSERT_EQ(1u, queries->size());
  EXPECT_EQ("hello world", (*queries)[0].GetString());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorExcludesConfiguredDomainsBeforeAggregation) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  base::ListValue excluded;
  excluded.Append("github.com");
  prefs->SetList(prefs::kDaoDreamExcludedDomains, std::move(excluded));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  history->AddPageWithDetails(
      GURL("https://github.com/foo/bar?token=SECRET123"),
      u"Sensitive GitHub title", 1, 0, now - base::Hours(1), false,
      history::SOURCE_BROWSED);
  history->AddPageWithDetails(GURL("https://gist.github.com/private/snippet"),
                              u"Sensitive Gist title", 1, 0,
                              now - base::Hours(2), false,
                              history::SOURCE_BROWSED);
  history->AddPageWithDetails(GURL("https://notgithub.com/public"),
                              u"Public title", 1, 0, now - base::Hours(3),
                              false, history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  std::string json;
  base::JSONWriter::Write(pack, &json);
  const base::ListValue* history_material = pack.FindList("history");
  ASSERT_TRUE(history_material);
  for (const base::Value& value : *history_material) {
    const std::string* domain = value.GetDict().FindString("domain");
    ASSERT_TRUE(domain);
    EXPECT_NE("github.com", *domain);
    EXPECT_NE("gist.github.com", *domain);
  }
  EXPECT_EQ(std::string::npos, json.find("Sensitive GitHub title"));
  EXPECT_EQ(std::string::npos, json.find("Sensitive Gist title"));
  EXPECT_EQ(std::string::npos, json.find("SECRET123"));
  EXPECT_NE(std::string::npos, json.find("notgithub.com"));

  const base::DictValue* stats = pack.FindDict("stats");
  ASSERT_TRUE(stats);
  EXPECT_EQ(2, stats->FindInt("excluded_history_visits").value_or(0));
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorDoesNotExtractQueriesFromExcludedDomains) {
  PrefService* prefs = browser()->profile()->GetPrefs();
  base::ListValue excluded;
  excluded.Append("google.com");
  prefs->SetList(prefs::kDaoDreamExcludedDomains, std::move(excluded));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  history->AddPage(GURL("https://www.google.com/search?q=private+query"),
                   now - base::Hours(1), history::SOURCE_BROWSED);
  history->AddPage(GURL("https://www.bing.com/search?q=public+query"),
                   now - base::Hours(2), history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  std::string json;
  base::JSONWriter::Write(pack, &json);
  EXPECT_EQ(std::string::npos, json.find("private query"));
  EXPECT_NE(std::string::npos, json.find("public query"));
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorAggregatesForegroundDuration) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  const GURL deep_url("https://deep.example/research");
  const GURL quick_url("https://quick.example/glance");
  history->AddPageWithDetails(deep_url, u"Deep research", 1, 0,
                              now - base::Minutes(90), false,
                              history::SOURCE_BROWSED);
  history->AddPageWithDetails(quick_url, u"Quick glance", 1, 0,
                              now - base::Minutes(30), false,
                              history::SOURCE_BROWSED);

  SetForegroundDurationForUrl(history, deep_url, base::Minutes(45));
  SetForegroundDurationForUrl(history, quick_url, base::Seconds(30));

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(3), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  const base::ListValue* domains = pack.FindList("history");
  ASSERT_TRUE(domains);
  ASSERT_GE(domains->size(), 2u);

  const base::DictValue& deep = (*domains)[0].GetDict();
  EXPECT_EQ("deep.example", *deep.FindString("domain"));
  std::optional<int> deep_foreground = deep.FindInt("foreground_seconds");
  ASSERT_TRUE(deep_foreground.has_value());
  EXPECT_EQ(2700, *deep_foreground);
  const std::string* deep_level = deep.FindString("duration_level");
  ASSERT_TRUE(deep_level);
  EXPECT_EQ("deep", *deep_level);

  const base::DictValue& quick = (*domains)[1].GetDict();
  EXPECT_EQ("quick.example", *quick.FindString("domain"));
  std::optional<int> quick_foreground = quick.FindInt("foreground_seconds");
  ASSERT_TRUE(quick_foreground.has_value());
  EXPECT_EQ(30, *quick_foreground);
  const std::string* quick_level = quick.FindString("duration_level");
  ASSERT_TRUE(quick_level);
  EXPECT_EQ("light", *quick_level);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, CollectorCapsLongHistoryText) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time now = base::Time::Now();
  const std::u16string long_title(800, u'T');
  const std::string long_query(800, 'q');
  history->AddPageWithDetails(GURL("https://long-title.example/page"),
                              long_title, 1, 0, now - base::Hours(1),
                              false, history::SOURCE_BROWSED);
  history->AddPage(GURL("https://www.google.com/search?q=" + long_query),
                   now - base::Hours(2), history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  DreamMaterialCollector collector(browser()->profile(), memory);

  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  const base::ListValue* domains = pack.FindList("history");
  ASSERT_TRUE(domains);
  bool found_long_title = false;
  for (const base::Value& d : *domains) {
    const base::DictValue& dict = d.GetDict();
    const std::string* domain = dict.FindString("domain");
    ASSERT_TRUE(domain);
    if (*domain != "long-title.example") {
      continue;
    }
    found_long_title = true;
    const base::ListValue* titles = dict.FindList("titles");
    ASSERT_TRUE(titles);
    ASSERT_FALSE(titles->empty());
    EXPECT_LE((*titles)[0].GetString().size(), 240u);
  }
  EXPECT_TRUE(found_long_title);

  const base::ListValue* queries = pack.FindList("search_queries");
  ASSERT_TRUE(queries);
  ASSERT_FALSE(queries->empty());
  EXPECT_LE((*queries)[0].GetString().size(), 240u);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       CollectorIncludesHighConfidencePreferences) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  {
    bool saved = false;
    base::RunLoop loop;
    memory->MergePreference(
        "interest.documentation",
        "Prefers reading implementation documentation before coding.", 0.95,
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }
  {
    bool saved = false;
    base::RunLoop loop;
    memory->MergePreference(
        "interest.noisy_guess", "A weak one-off browsing guess.", 0.4,
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }
  {
    bool saved = false;
    base::RunLoop loop;
    memory->MergePreference(
        "interest.auto_candidate",
        "A model-generated candidate that was not confirmed.", 0.8,
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }

  DreamMaterialCollector collector(browser()->profile(), memory);
  const base::Time now = base::Time::Now();
  base::DictValue pack;
  base::RunLoop loop;
  collector.Collect(now - base::Hours(6), now,
                    base::BindLambdaForTesting([&](base::DictValue p) {
                      pack = std::move(p);
                      loop.Quit();
                    }));
  loop.Run();

  const base::ListValue* preferences = pack.FindList("preferences");
  ASSERT_TRUE(preferences);
  ASSERT_EQ(1u, preferences->size());

  const base::DictValue& pref = (*preferences)[0].GetDict();
  EXPECT_EQ("interest.documentation", *pref.FindString("key"));
  EXPECT_EQ("Prefers reading implementation documentation before coding.",
            *pref.FindString("value"));
  EXPECT_GE(pref.FindDouble("confidence").value_or(0), 0.9);
  EXPECT_EQ(1, pref.FindInt("evidence_count").value_or(0));

  const base::DictValue* stats = pack.FindDict("stats");
  ASSERT_TRUE(stats);
  EXPECT_EQ(1, stats->FindInt("preferences").value_or(0));

  std::string json;
  base::JSONWriter::Write(pack, &json);
  EXPECT_EQ(std::string::npos, json.find("interest.noisy_guess"));
  EXPECT_EQ(std::string::npos, json.find("interest.auto_candidate"));
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       MemoryContextIncludesDomainSummary) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  ConversationSummary summary;
  summary.session_id = "summary-session";
  summary.summary = "User compared documentation and release notes.";
  summary.message_count = 4;
  summary.first_timestamp = base::Time::FromSecondsSinceUnixEpoch(1800000000);
  summary.last_timestamp = base::Time::FromSecondsSinceUnixEpoch(1800000300);
  summary.primary_domain = "docs.example";

  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveConversationSummary(
        summary, base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }

  MemoryContext context;
  base::RunLoop loop;
  memory->GetMemoryContext(
      "https://docs.example/release-notes", "docs.example", "",
      base::BindLambdaForTesting([&](MemoryContext ctx) {
        context = std::move(ctx);
        loop.Quit();
      }));
  loop.Run();

  ASSERT_TRUE(context.relevant_summary.has_value());
  EXPECT_EQ("User compared documentation and release notes.",
            context.relevant_summary->summary);
  EXPECT_EQ(4, context.relevant_summary->message_count);
  EXPECT_EQ(summary.first_timestamp,
            context.relevant_summary->first_timestamp);
  EXPECT_EQ(summary.last_timestamp, context.relevant_summary->last_timestamp);
  EXPECT_EQ("docs.example", context.relevant_summary->primary_domain);

  const base::DictValue response = SerializeMemoryContextForAgentUi(context);
  const base::DictValue* relevant_summary =
      response.FindDict("relevantSummary");
  ASSERT_TRUE(relevant_summary);
  const std::string* serialized_summary =
      relevant_summary->FindString("summary");
  ASSERT_TRUE(serialized_summary);
  EXPECT_EQ("User compared documentation and release notes.",
            *serialized_summary);
  EXPECT_EQ(4, relevant_summary->FindInt("messageCount").value_or(0));
  EXPECT_EQ(static_cast<double>(
                summary.first_timestamp.ToDeltaSinceWindowsEpoch()
                    .InMicroseconds()),
            relevant_summary->FindDouble("firstTimestamp").value_or(0));
  EXPECT_EQ(static_cast<double>(
                summary.last_timestamp.ToDeltaSinceWindowsEpoch()
                    .InMicroseconds()),
            relevant_summary->FindDouble("lastTimestamp").value_or(0));
  const std::string* primary_domain =
      relevant_summary->FindString("primaryDomain");
  ASSERT_TRUE(primary_domain);
  EXPECT_EQ("docs.example", *primary_domain);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, DreamReportStoreRoundTrip) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  DreamReport report;
  report.dream_date = "2026-06-10";
  report.report_markdown = "# report";
  report.habit_candidates = "[]";
  report.material_stats = "{}";
  report.status = "completed";
  report.attempt_count = 1;
  report.trigger_kind = "manual";
  report.debug_material_json = "{\"history\":[]}";

  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveDreamReport(report,
                            base::BindLambdaForTesting([&](bool ok) {
                              saved = ok;
                              loop.Quit();
                            }));
    loop.Run();
    ASSERT_TRUE(saved);
  }
  {
    std::optional<DreamReport> got;
    base::RunLoop loop;
    memory->GetDreamReportByDate(
        "2026-06-10",
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          got = std::move(r);
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ("completed", got->status);
    EXPECT_EQ("{\"history\":[]}", got->debug_material_json);
    EXPECT_TRUE(got->viewed_at.is_null());
  }
  // Unviewed lookup finds it; marking viewed removes it.
  int64_t id = 0;
  {
    std::optional<DreamReport> got;
    base::RunLoop loop;
    memory->GetLatestUnviewedDreamReport(
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          got = std::move(r);
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(got.has_value());
    id = got->id;
  }
  {
    bool marked = false;
    base::RunLoop loop;
    memory->MarkDreamReportViewed(id,
                                  base::BindLambdaForTesting([&](bool ok) {
                                    marked = ok;
                                    loop.Quit();
                                  }));
    loop.Run();
    EXPECT_TRUE(marked);
  }
  {
    std::optional<DreamReport> got;
    base::RunLoop loop;
    memory->GetLatestUnviewedDreamReport(
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          got = std::move(r);
          loop.Quit();
        }));
    loop.Run();
    EXPECT_FALSE(got.has_value());
  }
  {
    std::optional<DreamReport> got;
    base::RunLoop loop;
    memory->GetLatestDreamReport(
        base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
          got = std::move(r);
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ("2026-06-10", got->dream_date);
    EXPECT_FALSE(got->viewed_at.is_null());
  }
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       DreamReportHistoryListsCompletedReportsNewestFirst) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  auto save_report = [&](const std::string& date, const std::string& status) {
    DreamReport report;
    report.dream_date = date;
    report.report_markdown = "# " + date;
    report.habit_candidates = "[]";
    report.material_stats = "{}";
    report.status = status;
    report.attempt_count = 1;
    report.trigger_kind = "manual";

    bool saved = false;
    base::RunLoop loop;
    memory->SaveDreamReport(report,
                            base::BindLambdaForTesting([&](bool ok) {
                              saved = ok;
                              loop.Quit();
                            }));
    loop.Run();
    ASSERT_TRUE(saved);
  };

  save_report("2026-06-10", "completed");
  save_report("2026-06-11", "failed");
  save_report("2026-06-12", "completed");

  std::vector<DreamReport> reports;
  base::RunLoop loop;
  memory->GetDreamReports(
      10, base::BindLambdaForTesting([&](std::vector<DreamReport> r) {
        reports = std::move(r);
        loop.Quit();
      }));
  loop.Run();

  ASSERT_EQ(2u, reports.size());
  EXPECT_EQ("2026-06-12", reports[0].dream_date);
  EXPECT_EQ("2026-06-10", reports[1].dream_date);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, WeeklyDreamReportStoreRoundTrip) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  WeeklyDreamReport report;
  report.week_start = "2026-07-06";
  report.week_end = "2026-07-13";
  report.content_json =
      R"({"schema_version":1,"headline":"Continue the migration"})";
  report.material_stats = R"({"source_count":2})";
  report.status = "completed";
  report.attempt_count = 1;
  report.trigger_kind = "manual";
  report.debug_material_json = R"({"period":"redacted"})";

  WeeklyDreamSource page_source;
  page_source.ref_id = "page_1";
  page_source.source_kind = "page";
  page_source.title = "Migration guide";
  page_source.domain = "docs.example";
  page_source.local_locator = "https://docs.example/migration";
  page_source.last_seen_at = LocalTime(2026, 7, 10, 9, 30);

  WeeklyDreamSource conversation_source;
  conversation_source.ref_id = "conversation_1";
  conversation_source.source_kind = "conversation";
  conversation_source.title = "Migration planning";
  conversation_source.domain = "docs.example";
  conversation_source.local_locator = "session-weekly";
  conversation_source.last_seen_at = LocalTime(2026, 7, 11, 14, 0);

  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveWeeklyDreamReport(
        report, {page_source, conversation_source},
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }

  int64_t report_id = 0;
  {
    std::optional<WeeklyDreamReport> loaded;
    base::RunLoop loop;
    memory->GetWeeklyDreamReportByWeekStart(
        report.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(loaded.has_value());
    report_id = loaded->id;
    EXPECT_EQ(report.week_start, loaded->week_start);
    EXPECT_EQ(report.week_end, loaded->week_end);
    EXPECT_EQ(report.content_json, loaded->content_json);
    EXPECT_EQ(report.material_stats, loaded->material_stats);
    EXPECT_EQ(report.debug_material_json, loaded->debug_material_json);
    EXPECT_EQ("completed", loaded->status);
    EXPECT_TRUE(loaded->viewed_at.is_null());
    EXPECT_FALSE(loaded->created_at.is_null());
  }

  {
    std::vector<WeeklyDreamReport> reports;
    base::RunLoop loop;
    memory->GetWeeklyDreamReports(
        10,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamReport> values) {
              reports = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(1u, reports.size());
    EXPECT_EQ(report.week_start, reports[0].week_start);
  }

  {
    std::optional<WeeklyDreamReport> unread;
    base::RunLoop loop;
    memory->GetLatestUnviewedWeeklyDreamReport(base::BindLambdaForTesting(
        [&](std::optional<WeeklyDreamReport> value) {
          unread = std::move(value);
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(unread.has_value());
    EXPECT_EQ(report_id, unread->id);
  }

  {
    std::vector<WeeklyDreamSource> sources;
    base::RunLoop loop;
    memory->GetWeeklyDreamSources(
        report_id,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamSource> values) {
              sources = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(2u, sources.size());
    bool found_page = false;
    bool found_conversation = false;
    for (const WeeklyDreamSource& source : sources) {
      EXPECT_EQ(report_id, source.report_id);
      found_page |= source.ref_id == "page_1";
      found_conversation |= source.ref_id == "conversation_1";
    }
    EXPECT_TRUE(found_page);
    EXPECT_TRUE(found_conversation);
  }

  {
    std::optional<WeeklyDreamSource> source;
    base::RunLoop loop;
    memory->GetWeeklyDreamSource(
        report_id, "page_1",
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamSource> value) {
              source = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ("Migration guide", source->title);
    EXPECT_EQ("docs.example", source->domain);
    EXPECT_EQ("https://docs.example/migration", source->local_locator);
    EXPECT_EQ(page_source.last_seen_at, source->last_seen_at);
  }

  {
    bool marked = false;
    base::RunLoop loop;
    memory->MarkWeeklyDreamReportViewed(
        report_id, base::BindLambdaForTesting([&](bool ok) {
          marked = ok;
          loop.Quit();
        }));
    loop.Run();
    EXPECT_TRUE(marked);
  }

  {
    std::optional<WeeklyDreamReport> unread;
    base::RunLoop loop;
    memory->GetLatestUnviewedWeeklyDreamReport(base::BindLambdaForTesting(
        [&](std::optional<WeeklyDreamReport> value) {
          unread = std::move(value);
          loop.Quit();
        }));
    loop.Run();
    EXPECT_FALSE(unread.has_value());
  }

  {
    std::optional<WeeklyDreamReport> loaded;
    base::RunLoop loop;
    memory->GetWeeklyDreamReportByWeekStart(
        report.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->viewed_at.is_null());
  }

  {
    bool deleted = false;
    base::RunLoop loop;
    memory->DeleteWeeklyDreamReport(
        report_id, base::BindLambdaForTesting([&](bool ok) {
          deleted = ok;
          loop.Quit();
        }));
    loop.Run();
    EXPECT_TRUE(deleted);
  }

  {
    std::optional<WeeklyDreamReport> loaded;
    std::vector<WeeklyDreamSource> sources;
    base::RunLoop report_loop;
    memory->GetWeeklyDreamReportByWeekStart(
        report.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              report_loop.Quit();
            }));
    report_loop.Run();
    EXPECT_FALSE(loaded.has_value());

    base::RunLoop sources_loop;
    memory->GetWeeklyDreamSources(
        report_id,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamSource> values) {
              sources = std::move(values);
              sources_loop.Quit();
            }));
    sources_loop.Run();
    EXPECT_TRUE(sources.empty());
  }
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyDreamReportReplacementRollsBackOnBadSources) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  WeeklyDreamReport original;
  original.week_start = "2026-07-06";
  original.week_end = "2026-07-13";
  original.content_json = R"({"headline":"Original"})";
  original.material_stats = R"({"source_count":1})";
  original.status = "completed";
  original.attempt_count = 1;
  original.trigger_kind = "scheduled";

  WeeklyDreamSource original_source;
  original_source.ref_id = "page_1";
  original_source.source_kind = "page";
  original_source.title = "Original source";
  original_source.domain = "original.example";
  original_source.local_locator = "https://original.example/page";
  original_source.last_seen_at = LocalTime(2026, 7, 9, 10, 0);

  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveWeeklyDreamReport(
        original, {original_source},
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }

  int64_t original_id = 0;
  {
    std::optional<WeeklyDreamReport> loaded;
    base::RunLoop loop;
    memory->GetWeeklyDreamReportByWeekStart(
        original.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(loaded.has_value());
    original_id = loaded->id;
  }

  WeeklyDreamReport replacement = original;
  replacement.content_json = R"({"headline":"Replacement"})";
  replacement.attempt_count = 2;
  replacement.trigger_kind = "manual";

  WeeklyDreamSource duplicate_a = original_source;
  duplicate_a.title = "Replacement source A";
  WeeklyDreamSource duplicate_b = duplicate_a;
  duplicate_b.title = "Replacement source B";

  {
    bool saved = true;
    base::RunLoop loop;
    memory->SaveWeeklyDreamReport(
        replacement, {duplicate_a, duplicate_b},
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    EXPECT_FALSE(saved);
  }

  {
    std::optional<WeeklyDreamReport> loaded;
    base::RunLoop loop;
    memory->GetWeeklyDreamReportByWeekStart(
        original.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(original_id, loaded->id);
    EXPECT_EQ(original.content_json, loaded->content_json);
    EXPECT_EQ(original.trigger_kind, loaded->trigger_kind);
  }

  {
    std::vector<WeeklyDreamSource> sources;
    base::RunLoop loop;
    memory->GetWeeklyDreamSources(
        original_id,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamSource> values) {
              sources = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(1u, sources.size());
    EXPECT_EQ("Original source", sources[0].title);
    EXPECT_EQ("https://original.example/page", sources[0].local_locator);
  }
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyDreamReportHistoryListsCompletedNewestFirst) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  auto save_weekly = [&](const std::string& week_start,
                         const std::string& status) {
    WeeklyDreamReport report;
    report.week_start = week_start;
    report.week_end = week_start;
    report.content_json = R"({"schema_version":1})";
    report.material_stats = "{}";
    report.status = status;
    report.attempt_count = 1;
    report.trigger_kind = "manual";

    bool saved = false;
    base::RunLoop loop;
    memory->SaveWeeklyDreamReport(
        report, {}, base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  };

  save_weekly("2026-06-22", "completed");
  save_weekly("2026-06-29", "failed");
  save_weekly("2026-07-06", "skipped");
  save_weekly("2026-07-13", "completed");

  {
    std::vector<WeeklyDreamReport> reports;
    base::RunLoop loop;
    memory->GetWeeklyDreamReports(
        10,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamReport> values) {
              reports = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(2u, reports.size());
    EXPECT_EQ("2026-07-13", reports[0].week_start);
    EXPECT_EQ("2026-06-22", reports[1].week_start);
  }

  {
    std::vector<WeeklyDreamReport> reports;
    base::RunLoop loop;
    memory->GetWeeklyDreamReports(
        1,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamReport> values) {
              reports = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(1u, reports.size());
    EXPECT_EQ("2026-07-13", reports[0].week_start);
  }

  {
    std::optional<WeeklyDreamReport> previous;
    base::RunLoop loop;
    memory->GetLatestWeeklyDreamReportBefore(
        "2026-07-13",
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              previous = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(previous.has_value());
    EXPECT_EQ("2026-06-22", previous->week_start);
  }

  {
    std::optional<WeeklyDreamReport> skipped;
    base::RunLoop loop;
    memory->GetWeeklyDreamReportByWeekStart(
        "2026-07-06",
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              skipped = std::move(value);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_TRUE(skipped.has_value());
    EXPECT_EQ("skipped", skipped->status);
  }

  auto save_daily = [&](const std::string& dream_date) {
    DreamReport report;
    report.dream_date = dream_date;
    report.report_markdown = "# " + dream_date;
    report.habit_candidates = "[]";
    report.material_stats = "{}";
    report.status = "completed";
    report.attempt_count = 1;
    report.trigger_kind = "manual";

    bool saved = false;
    base::RunLoop loop;
    memory->SaveDreamReport(report,
                            base::BindLambdaForTesting([&](bool ok) {
                              saved = ok;
                              loop.Quit();
                            }));
    loop.Run();
    ASSERT_TRUE(saved);
  };

  save_daily("2026-07-06");
  save_daily("2026-07-12");
  save_daily("2026-07-13");

  {
    std::vector<DreamReport> reports;
    base::RunLoop loop;
    memory->GetDreamReportsInDateRange(
        "2026-07-06", "2026-07-13", 10,
        base::BindLambdaForTesting([&](std::vector<DreamReport> values) {
          reports = std::move(values);
          loop.Quit();
        }));
    loop.Run();
    ASSERT_EQ(2u, reports.size());
    EXPECT_EQ("2026-07-12", reports[0].dream_date);
    EXPECT_EQ("2026-07-06", reports[1].dream_date);
  }
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       DeleteConversationInvalidatesWeeklySource) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  const base::Time window_start = LocalTime(2026, 7, 6, 0, 0);
  const base::Time window_end = LocalTime(2026, 7, 13, 0, 0);

  std::vector<ConversationMessage> messages;
  for (base::Time timestamp :
       {window_start, window_end - base::Microseconds(1), window_end}) {
    ConversationMessage message;
    message.session_id = "session-delete";
    message.role = "user";
    message.content = "Question";
    message.timestamp = timestamp;
    messages.push_back(std::move(message));
  }
  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveConversationMessages(
        "session-delete", std::move(messages),
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  }

  auto save_summary = [&](const std::string& session_id,
                          base::Time last_timestamp) {
    ConversationSummary summary;
    summary.session_id = session_id;
    summary.summary = "Summary for " + session_id;
    summary.message_count = 1;
    summary.first_timestamp = last_timestamp - base::Minutes(1);
    summary.last_timestamp = last_timestamp;
    summary.primary_domain = "docs.example";

    bool saved = false;
    base::RunLoop loop;
    memory->SaveConversationSummary(
        std::move(summary), base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);
  };

  save_summary("session-start", window_start);
  save_summary("session-delete", window_end - base::Hours(1));
  save_summary("session-end", window_end);

  {
    std::vector<ConversationMessage> ranged_messages;
    base::RunLoop loop;
    memory->LoadConversationMessagesInRange(
        window_start, window_end, 10,
        base::BindLambdaForTesting(
            [&](std::vector<ConversationMessage> values) {
              ranged_messages = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(2u, ranged_messages.size());
    EXPECT_EQ(window_start, ranged_messages[0].timestamp);
    EXPECT_EQ(window_end - base::Microseconds(1),
              ranged_messages[1].timestamp);
  }

  {
    std::vector<ConversationSummary> ranged_summaries;
    base::RunLoop loop;
    memory->LoadConversationSummariesInRange(
        window_start, window_end, 10,
        base::BindLambdaForTesting(
            [&](std::vector<ConversationSummary> values) {
              ranged_summaries = std::move(values);
              loop.Quit();
            }));
    loop.Run();
    ASSERT_EQ(2u, ranged_summaries.size());
    EXPECT_EQ("session-delete", ranged_summaries[0].session_id);
    EXPECT_EQ("session-start", ranged_summaries[1].session_id);
  }

  WeeklyDreamReport report;
  report.week_start = "2026-07-06";
  report.week_end = "2026-07-13";
  report.content_json = R"({"schema_version":1})";
  report.material_stats = R"({"source_count":2})";
  report.status = "completed";
  report.attempt_count = 1;
  report.trigger_kind = "manual";

  WeeklyDreamSource conversation_source;
  conversation_source.ref_id = "conversation_1";
  conversation_source.source_kind = "conversation";
  conversation_source.title = "Conversation";
  conversation_source.domain = "docs.example";
  conversation_source.local_locator = "session-delete";
  conversation_source.last_seen_at = window_end - base::Hours(1);

  WeeklyDreamSource page_source;
  page_source.ref_id = "page_1";
  page_source.source_kind = "page";
  page_source.title = "Page with matching locator text";
  page_source.domain = "docs.example";
  page_source.local_locator = "session-delete";
  page_source.last_seen_at = window_end - base::Hours(2);

  int64_t report_id = 0;
  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveWeeklyDreamReport(
        report, {conversation_source, page_source},
        base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);

    std::optional<WeeklyDreamReport> loaded;
    base::RunLoop load_loop;
    memory->GetWeeklyDreamReportByWeekStart(
        report.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              load_loop.Quit();
            }));
    load_loop.Run();
    ASSERT_TRUE(loaded.has_value());
    report_id = loaded->id;
  }

  {
    bool deleted = false;
    base::RunLoop loop;
    memory->DeleteConversation(
        "session-delete", base::BindLambdaForTesting([&](bool ok) {
          deleted = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(deleted);
  }

  {
    std::optional<WeeklyDreamSource> conversation;
    std::optional<WeeklyDreamSource> page;
    base::RunLoop conversation_loop;
    memory->GetWeeklyDreamSource(
        report_id, "conversation_1",
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamSource> value) {
              conversation = std::move(value);
              conversation_loop.Quit();
            }));
    conversation_loop.Run();
    EXPECT_FALSE(conversation.has_value());

    base::RunLoop page_loop;
    memory->GetWeeklyDreamSource(
        report_id, "page_1",
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamSource> value) {
              page = std::move(value);
              page_loop.Quit();
            }));
    page_loop.Run();
    ASSERT_TRUE(page.has_value());
    EXPECT_EQ("page", page->source_kind);
  }

  {
    std::vector<ConversationMessage> remaining_messages;
    std::vector<ConversationSummary> remaining_summaries;
    base::RunLoop messages_loop;
    memory->LoadConversationMessagesInRange(
        window_start, window_end, 10,
        base::BindLambdaForTesting(
            [&](std::vector<ConversationMessage> values) {
              remaining_messages = std::move(values);
              messages_loop.Quit();
            }));
    messages_loop.Run();
    EXPECT_TRUE(remaining_messages.empty());

    base::RunLoop summaries_loop;
    memory->LoadConversationSummariesInRange(
        window_start, window_end, 10,
        base::BindLambdaForTesting(
            [&](std::vector<ConversationSummary> values) {
              remaining_summaries = std::move(values);
              summaries_loop.Quit();
            }));
    summaries_loop.Run();
    ASSERT_EQ(1u, remaining_summaries.size());
    EXPECT_EQ("session-start", remaining_summaries[0].session_id);
  }
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       ClearAllDeletesDreamReportsAndSources) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  DreamReport daily;
  daily.dream_date = "2026-07-12";
  daily.report_markdown = "# Daily";
  daily.habit_candidates = "[]";
  daily.material_stats = "{}";
  daily.status = "completed";
  daily.attempt_count = 1;
  daily.trigger_kind = "manual";
  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveDreamReport(daily,
                            base::BindLambdaForTesting([&](bool ok) {
                              saved = ok;
                              loop.Quit();
                            }));
    loop.Run();
    ASSERT_TRUE(saved);
  }

  WeeklyDreamReport weekly;
  weekly.week_start = "2026-07-06";
  weekly.week_end = "2026-07-13";
  weekly.content_json = R"({"schema_version":1})";
  weekly.material_stats = R"({"source_count":1})";
  weekly.status = "completed";
  weekly.attempt_count = 1;
  weekly.trigger_kind = "manual";

  WeeklyDreamSource source;
  source.ref_id = "page_1";
  source.source_kind = "page";
  source.title = "Weekly source";
  source.domain = "docs.example";
  source.local_locator = "https://docs.example/source";
  source.last_seen_at = LocalTime(2026, 7, 11, 10, 0);

  int64_t weekly_id = 0;
  {
    bool saved = false;
    base::RunLoop loop;
    memory->SaveWeeklyDreamReport(
        weekly, {source}, base::BindLambdaForTesting([&](bool ok) {
          saved = ok;
          loop.Quit();
        }));
    loop.Run();
    ASSERT_TRUE(saved);

    std::optional<WeeklyDreamReport> loaded;
    base::RunLoop load_loop;
    memory->GetWeeklyDreamReportByWeekStart(
        weekly.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              loaded = std::move(value);
              load_loop.Quit();
            }));
    load_loop.Run();
    ASSERT_TRUE(loaded.has_value());
    weekly_id = loaded->id;
  }

  {
    bool cleared = false;
    base::RunLoop loop;
    memory->ClearAll(base::BindLambdaForTesting([&](bool ok) {
      cleared = ok;
      loop.Quit();
    }));
    loop.Run();
    ASSERT_TRUE(cleared);
  }

  {
    std::optional<DreamReport> daily_report;
    std::optional<WeeklyDreamReport> weekly_report;
    std::vector<WeeklyDreamSource> sources;

    base::RunLoop daily_loop;
    memory->GetDreamReportByDate(
        daily.dream_date,
        base::BindLambdaForTesting([&](std::optional<DreamReport> value) {
          daily_report = std::move(value);
          daily_loop.Quit();
        }));
    daily_loop.Run();
    EXPECT_FALSE(daily_report.has_value());

    base::RunLoop weekly_loop;
    memory->GetWeeklyDreamReportByWeekStart(
        weekly.week_start,
        base::BindLambdaForTesting(
            [&](std::optional<WeeklyDreamReport> value) {
              weekly_report = std::move(value);
              weekly_loop.Quit();
            }));
    weekly_loop.Run();
    EXPECT_FALSE(weekly_report.has_value());

    base::RunLoop sources_loop;
    memory->GetWeeklyDreamSources(
        weekly_id,
        base::BindLambdaForTesting(
            [&](std::vector<WeeklyDreamSource> values) {
              sources = std::move(values);
              sources_loop.Quit();
            }));
    sources_loop.Run();
    EXPECT_TRUE(sources.empty());
  }
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyCollectorSeparatesLocalLocators) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);

  const base::Time window_end = base::Time::Now();
  const base::Time window_start = window_end - base::Days(7);
  const GURL guide_url(
      "https://docs1.example/migration/guide?private=weekly-secret");
  const GURL notes_url("https://notes.example/decision-log");
  history->AddPageWithDetails(
      guide_url,
      u"Migration guide Reference:https://reference-secret.test/path, "
      u"one,mailto:one-secret@example.com; "
      u"来源：https://source-secret.test/path。 "
      u"x;www.semicolon-secret.test/path. "
      u"dot.www.dot-secret.test/path! "
      u"view-source:https://nested-secret.test/path "
      u"filesystem:https://filesystem-secret.test/temporary/file "
      u"Status: done",
      1, 0, window_end - base::Hours(3), false, history::SOURCE_BROWSED);
  history->AddPageWithDetails(notes_url, u"Decision log", 1, 0,
                              window_end - base::Hours(4), false,
                              history::SOURCE_BROWSED);
  SetForegroundDurationForUrl(history, guide_url, base::Minutes(40));
  SetForegroundDurationForUrl(history, notes_url, base::Minutes(10));

  ConversationSummary summary;
  summary.session_id = "weekly-summary-session-secret";
  summary.summary =
      "Compared two migration approaches. data:text/plain,summary-secret "
      "mailto:summary@example.com Status: done";
  summary.message_count = 6;
  summary.first_timestamp = window_end - base::Hours(5);
  summary.last_timestamp = window_end - base::Hours(2);
  summary.primary_domain = "docs.example";
  ASSERT_TRUE(SaveConversationSummaryForTest(memory, std::move(summary)));

  ConversationMessage fallback;
  fallback.role = "user";
  fallback.content =
      "How should I continue the rollout? //fallback-secret.test/path "
      "www.fallback-secret.test/path";
  fallback.timestamp = window_end - base::Hours(1);
  fallback.page_url = "https://questions.example/private-context";
  fallback.page_title = "Rollout question";

  ConversationMessage trailing_question;
  trailing_question.role = "user";
  trailing_question.content =
      "Can you inspect https://question-secret.test/a?";
  trailing_question.timestamp = window_end - base::Minutes(45);

  ConversationMessage chinese_trailing_question;
  chinese_trailing_question.role = "user";
  chinese_trailing_question.content =
      "可以检查 https://question-cn-secret.test/a？";
  chinese_trailing_question.timestamp = window_end - base::Minutes(44);

  ConversationMessage query_only;
  query_only.role = "user";
  query_only.content =
      "https://query-only-secret.test/a?token=private-query";
  query_only.timestamp = window_end - base::Minutes(40);
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "weekly-fallback-session-secret",
      {fallback, trailing_question, chinese_trailing_question, query_only}));

  ConversationMessage short_session_question;
  short_session_question.role = "user";
  short_session_question.content = "Can I continue?";
  short_session_question.timestamp = window_end - base::Minutes(30);
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "1", {short_session_question}));

  WeeklyDreamMaterial material = CollectWeeklyMaterialForTest(
      profile, memory, window_start, window_end, "2026-07-06",
      "2026-07-13");
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(material.model_material, &json));

  EXPECT_NE(std::string::npos, json.find("Migration guide"));
  EXPECT_NE(std::string::npos, json.find("Decision log"));
  EXPECT_NE(std::string::npos,
            json.find("Compared two migration approaches."));
  EXPECT_NE(std::string::npos,
            json.find("How should I continue the rollout?"));
  EXPECT_NE(std::string::npos, json.find("Can you inspect [link]?"));
  EXPECT_NE(std::string::npos, json.find("可以检查 [link]？"));
  EXPECT_NE(std::string::npos, json.find("Status: done"));
  EXPECT_NE(std::string::npos, json.find("page_1"));
  EXPECT_NE(std::string::npos, json.find("conversation_1"));
  EXPECT_EQ(std::string::npos, json.find("https://"));
  EXPECT_EQ(std::string::npos, json.find("data:text"));
  EXPECT_EQ(std::string::npos, json.find("mailto:"));
  EXPECT_EQ(std::string::npos, json.find("reference-secret"));
  EXPECT_EQ(std::string::npos, json.find("one-secret"));
  EXPECT_EQ(std::string::npos, json.find("source-secret"));
  EXPECT_EQ(std::string::npos, json.find("semicolon-secret"));
  EXPECT_EQ(std::string::npos, json.find("dot-secret"));
  EXPECT_EQ(std::string::npos, json.find("nested-secret"));
  EXPECT_EQ(std::string::npos, json.find("filesystem-secret"));
  EXPECT_EQ(std::string::npos, json.find("question-secret"));
  EXPECT_EQ(std::string::npos, json.find("question-cn-secret"));
  EXPECT_EQ(std::string::npos, json.find("query-only-secret"));
  EXPECT_EQ(std::string::npos, json.find("private-query"));
  EXPECT_EQ(std::string::npos, json.find("//fallback-secret"));
  EXPECT_EQ(std::string::npos, json.find("www.fallback-secret"));
  EXPECT_EQ(std::string::npos, json.find("weekly-secret"));
  EXPECT_EQ(std::string::npos,
            json.find("weekly-summary-session-secret"));
  EXPECT_EQ(std::string::npos,
            json.find("weekly-fallback-session-secret"));

  const WeeklyDreamSource* guide_source =
      FindWeeklySourceByLocator(material.local_sources, guide_url.spec());
  ASSERT_TRUE(guide_source);
  EXPECT_EQ("page", guide_source->source_kind);
  EXPECT_EQ("page_1", guide_source->ref_id);
  EXPECT_EQ("docs1.example", guide_source->domain);
  const WeeklyDreamSource* notes_source =
      FindWeeklySourceByLocator(material.local_sources, notes_url.spec());
  ASSERT_TRUE(notes_source);
  EXPECT_EQ("page", notes_source->source_kind);
  const WeeklyDreamSource* summary_source = FindWeeklySourceByLocator(
      material.local_sources, "weekly-summary-session-secret");
  ASSERT_TRUE(summary_source);
  EXPECT_EQ("conversation", summary_source->source_kind);
  const WeeklyDreamSource* fallback_source = FindWeeklySourceByLocator(
      material.local_sources, "weekly-fallback-session-secret");
  ASSERT_TRUE(fallback_source);
  EXPECT_EQ("conversation", fallback_source->source_kind);
  const WeeklyDreamSource* short_session_source =
      FindWeeklySourceByLocator(material.local_sources, "1");
  ASSERT_TRUE(short_session_source);
  EXPECT_EQ("conversation", short_session_source->source_kind);
  const base::DictValue* period =
      material.model_material.FindDict("period");
  ASSERT_TRUE(period);
  EXPECT_EQ("2026-07-06", *period->FindString("week_start"));
  EXPECT_EQ("2026-07-13", *period->FindString("week_end"));
  const base::ListValue* history_material =
      material.model_material.FindList("history");
  ASSERT_TRUE(history_material);
  ASSERT_FALSE(history_material->empty());
  EXPECT_EQ("docs1.example",
            *(*history_material)[0].GetDict().FindString("domain"));
  const base::ListValue* guide_pages =
      (*history_material)[0].GetDict().FindList("pages");
  ASSERT_TRUE(guide_pages);
  ASSERT_EQ(1u, guide_pages->size());
  EXPECT_EQ(
      "Migration guide [link], one,[link]; 来源：[link]。 "
      "x;[link]. dot.[link]! [link] [link] Status: done",
      *(*guide_pages)[0].GetDict().FindString("title"));
  const base::ListValue* fallback_questions =
      material.model_material.FindList("fallback_questions");
  ASSERT_TRUE(fallback_questions);
  EXPECT_EQ(4u, fallback_questions->size());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyCollectorExcludesConfiguredDomains) {
  Profile* profile = browser()->profile();
  base::ListValue excluded;
  excluded.Append("example.com");
  profile->GetPrefs()->SetList(prefs::kDaoDreamExcludedDomains,
                               std::move(excluded));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time window_end = base::Time::Now();
  const base::Time window_start = window_end - base::Days(7);
  const GURL excluded_url(
      "https://private.example.com/weekly?token=excluded-secret");
  const GURL allowed_url("https://public.example.net/weekly");
  history->AddPageWithDetails(excluded_url, u"Excluded weekly title", 1, 0,
                              window_end - base::Hours(2), false,
                              history::SOURCE_BROWSED);
  history->AddPageWithDetails(allowed_url, u"Allowed weekly title", 1, 0,
                              window_end - base::Hours(1), false,
                              history::SOURCE_BROWSED);

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  WeeklyDreamMaterial material = CollectWeeklyMaterialForTest(
      profile, memory, window_start, window_end, "2026-07-06",
      "2026-07-13");
  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(material.model_material, &json));

  EXPECT_EQ(std::string::npos, json.find("private.example.com"));
  EXPECT_EQ(std::string::npos, json.find("Excluded weekly title"));
  EXPECT_EQ(std::string::npos, json.find("excluded-secret"));
  EXPECT_NE(std::string::npos, json.find("public.example.net"));
  EXPECT_NE(std::string::npos, json.find("Allowed weekly title"));
  EXPECT_FALSE(
      FindWeeklySourceByLocator(material.local_sources, excluded_url.spec()));
  EXPECT_TRUE(
      FindWeeklySourceByLocator(material.local_sources, allowed_url.spec()));
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyCollectorUsesSummariesAndBoundedFallback) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  const base::Time window_end = base::Time::Now();
  const base::Time window_start = window_end - base::Days(7);

  ConversationSummary older_summary;
  older_summary.session_id = "summarized-session-secret";
  older_summary.summary = "An older migration summary.";
  older_summary.message_count = 4;
  older_summary.first_timestamp = window_end - base::Hours(9);
  older_summary.last_timestamp = window_end - base::Hours(6);
  older_summary.primary_domain = "docs.example";
  ASSERT_TRUE(
      SaveConversationSummaryForTest(memory, std::move(older_summary)));

  ConversationSummary newer_summary;
  newer_summary.session_id = "summarized-session-secret";
  newer_summary.summary = "Settled on the staged migration plan.";
  newer_summary.message_count = 8;
  newer_summary.first_timestamp = window_end - base::Hours(8);
  newer_summary.last_timestamp = window_end - base::Hours(4);
  newer_summary.primary_domain = "docs.example";
  ASSERT_TRUE(
      SaveConversationSummaryForTest(memory, std::move(newer_summary)));

  ConversationSummary tied_later_summary;
  tied_later_summary.session_id = "summarized-session-secret";
  tied_later_summary.summary = "Selected by the stable summary tie-break.";
  tied_later_summary.message_count = 9;
  tied_later_summary.first_timestamp = window_end - base::Hours(7);
  tied_later_summary.last_timestamp = window_end - base::Hours(4);
  tied_later_summary.primary_domain = "docs.example";
  ASSERT_TRUE(SaveConversationSummaryForTest(
      memory, std::move(tied_later_summary)));

  ConversationMessage summarized_message;
  summarized_message.role = "user";
  summarized_message.content = "Should this fallback replace the summary?";
  summarized_message.timestamp = window_end - base::Hours(5);
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "summarized-session-secret", {summarized_message}));

  std::vector<ConversationMessage> fallback_a_messages;
  for (int i = 0; i < 20; ++i) {
    ConversationMessage message;
    message.role = "user";
    message.content = base::StringPrintf("Fallback A question %02d?", i);
    message.timestamp = window_end - base::Minutes(60 - i);
    fallback_a_messages.push_back(std::move(message));
  }
  ConversationMessage assistant_message;
  assistant_message.role = "assistant";
  assistant_message.content = "assistant-only-fallback-secret";
  assistant_message.timestamp = window_end - base::Seconds(10);
  fallback_a_messages.push_back(std::move(assistant_message));
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "fallback-a-session-secret", std::move(fallback_a_messages)));

  ConversationSummary empty_summary;
  empty_summary.session_id = "fallback-b-session-secret";
  empty_summary.summary = "   ";
  empty_summary.message_count = 2;
  empty_summary.first_timestamp = window_end - base::Hours(1);
  empty_summary.last_timestamp = window_end - base::Minutes(30);
  empty_summary.primary_domain = "docs.example";
  ASSERT_TRUE(
      SaveConversationSummaryForTest(memory, std::move(empty_summary)));

  std::vector<ConversationMessage> fallback_b_messages;
  ConversationMessage newer_english_question;
  newer_english_question.role = "user";
  newer_english_question.content = "Should the rollout continue?";
  newer_english_question.timestamp = window_end - base::Minutes(2);
  fallback_b_messages.push_back(std::move(newer_english_question));
  ConversationMessage newer_chinese_question;
  newer_chinese_question.role = "user";
  newer_chinese_question.content = "下一步怎么办？";
  newer_chinese_question.timestamp = window_end - base::Minutes(1);
  fallback_b_messages.push_back(std::move(newer_chinese_question));
  ConversationMessage plain_statement;
  plain_statement.role = "user";
  plain_statement.content = "The migration is complete.";
  plain_statement.timestamp = window_end - base::Seconds(40);
  fallback_b_messages.push_back(std::move(plain_statement));
  ConversationMessage completed_statement;
  completed_statement.role = "user";
  completed_statement.content = "已完成";
  completed_statement.timestamp = window_end - base::Seconds(20);
  fallback_b_messages.push_back(std::move(completed_statement));
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "fallback-b-session-secret", std::move(fallback_b_messages)));

  WeeklyDreamMaterial material = CollectWeeklyMaterialForTest(
      profile, memory, window_start, window_end, "2026-07-06",
      "2026-07-13");
  const base::ListValue* conversations =
      material.model_material.FindList("conversations");
  const base::ListValue* fallback_questions =
      material.model_material.FindList("fallback_questions");
  ASSERT_TRUE(conversations);
  ASSERT_TRUE(fallback_questions);
  ASSERT_EQ(1u, conversations->size());
  EXPECT_EQ("Selected by the stable summary tie-break.",
            *(*conversations)[0].GetDict().FindString("summary"));
  EXPECT_EQ(static_cast<size_t>(
                WeeklyDreamMaterialCollector::kMaxFallbackMessages),
            fallback_questions->size());

  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(material.model_material, &json));
  EXPECT_EQ(std::string::npos,
            json.find("Should this fallback replace the summary?"));
  EXPECT_EQ(std::string::npos,
            json.find("assistant-only-fallback-secret"));
  EXPECT_EQ(std::string::npos, json.find("An older migration summary."));
  EXPECT_EQ(std::string::npos,
            json.find("Settled on the staged migration plan."));
  EXPECT_EQ(std::string::npos, json.find("The migration is complete."));
  EXPECT_EQ(std::string::npos, json.find("已完成"));
  EXPECT_EQ(std::string::npos, json.find("Fallback A question 00?"));
  EXPECT_EQ(std::string::npos, json.find("Fallback A question 01?"));
  EXPECT_NE(std::string::npos, json.find("Fallback A question 02?"));
  EXPECT_NE(std::string::npos, json.find("Should the rollout continue?"));
  EXPECT_NE(std::string::npos, json.find("下一步怎么办？"));
  EXPECT_EQ(std::string::npos, json.find("summarized-session-secret"));
  EXPECT_EQ(std::string::npos, json.find("fallback-a-session-secret"));
  EXPECT_EQ(std::string::npos, json.find("fallback-b-session-secret"));
  EXPECT_TRUE(FindWeeklySourceByLocator(material.local_sources,
                                        "summarized-session-secret"));
  EXPECT_TRUE(FindWeeklySourceByLocator(material.local_sources,
                                        "fallback-a-session-secret"));
  EXPECT_TRUE(FindWeeklySourceByLocator(material.local_sources,
                                        "fallback-b-session-secret"));
  EXPECT_EQ(1u, std::count_if(
                    material.local_sources.begin(),
                    material.local_sources.end(),
                    [](const WeeklyDreamSource& source) {
                      return source.local_locator ==
                             "summarized-session-secret";
                    }));
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyCollectorFallsBackFromPlaceholderOnlySummaries) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  const base::Time window_end = base::Time::Now();
  const base::Time window_start = window_end - base::Days(7);

  ConversationSummary url_only_summary;
  url_only_summary.session_id = "url-only-summary-session";
  url_only_summary.summary =
      "(view-source:https://summary-only-secret.test/path),";
  url_only_summary.message_count = 1;
  url_only_summary.first_timestamp = window_end - base::Hours(3);
  url_only_summary.last_timestamp = window_end - base::Hours(2);
  ASSERT_TRUE(SaveConversationSummaryForTest(
      memory, std::move(url_only_summary)));

  ConversationSummary session_only_summary;
  session_only_summary.session_id = "session-only-summary-secret";
  session_only_summary.summary = " session-only-summary-secret... ";
  session_only_summary.message_count = 1;
  session_only_summary.first_timestamp = window_end - base::Hours(3);
  session_only_summary.last_timestamp = window_end - base::Hours(2);
  ASSERT_TRUE(SaveConversationSummaryForTest(
      memory, std::move(session_only_summary)));

  ConversationMessage url_summary_question;
  url_summary_question.role = "user";
  url_summary_question.content =
      "Should the URL-only summary use fallback?";
  url_summary_question.timestamp = window_end - base::Minutes(30);
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "url-only-summary-session", {url_summary_question}));

  ConversationMessage session_summary_question;
  session_summary_question.role = "user";
  session_summary_question.content =
      "Should the session-only summary use fallback？";
  session_summary_question.timestamp = window_end - base::Minutes(20);
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "session-only-summary-secret", {session_summary_question}));

  ConversationMessage placeholder_only_question;
  placeholder_only_question.role = "user";
  placeholder_only_question.content =
      "https://question-only-secret.test/a?";
  placeholder_only_question.timestamp = window_end - base::Minutes(10);
  ConversationMessage query_only_message;
  query_only_message.role = "user";
  query_only_message.content =
      "https://question-query-secret.test/a?token=secret-query";
  query_only_message.timestamp = window_end - base::Minutes(5);
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "placeholder-only-question-session",
      {placeholder_only_question, query_only_message}));

  WeeklyDreamMaterial material = CollectWeeklyMaterialForTest(
      profile, memory, window_start, window_end, "2026-07-06",
      "2026-07-13");
  const base::ListValue* conversations =
      material.model_material.FindList("conversations");
  const base::ListValue* fallback_questions =
      material.model_material.FindList("fallback_questions");
  ASSERT_TRUE(conversations);
  ASSERT_TRUE(fallback_questions);
  EXPECT_TRUE(conversations->empty());
  ASSERT_EQ(2u, fallback_questions->size());

  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(material.model_material, &json));
  EXPECT_NE(std::string::npos,
            json.find("Should the URL-only summary use fallback?"));
  EXPECT_NE(std::string::npos,
            json.find("Should the session-only summary use fallback？"));
  EXPECT_EQ(std::string::npos, json.find("summary-only-secret"));
  EXPECT_EQ(std::string::npos, json.find("question-only-secret"));
  EXPECT_EQ(std::string::npos, json.find("question-query-secret"));
  EXPECT_EQ(std::string::npos, json.find("secret-query"));
  EXPECT_EQ(std::string::npos, json.find("[link]"));
  EXPECT_EQ(std::string::npos, json.find("[session]"));
  EXPECT_TRUE(FindWeeklySourceByLocator(material.local_sources,
                                        "url-only-summary-session"));
  EXPECT_TRUE(FindWeeklySourceByLocator(material.local_sources,
                                        "session-only-summary-secret"));
  EXPECT_FALSE(FindWeeklySourceByLocator(
      material.local_sources, "placeholder-only-question-session"));
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyCollectorIncludesAvailableDailyAndPreviousReports) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);

  for (int day = 6; day <= 12; ++day) {
    DreamReport daily;
    daily.dream_date = base::StringPrintf("2026-07-%02d", day);
    daily.report_markdown =
        "Daily report " + daily.dream_date +
        " about:blank mailto:daily-secret@example.com Status: done";
    daily.habit_candidates = "[]";
    daily.material_stats = "{}";
    daily.status = "completed";
    daily.attempt_count = 1;
    daily.trigger_kind = "nightly";
    ASSERT_TRUE(SaveDreamReportForTest(memory, std::move(daily)));
  }

  WeeklyDreamReport completed;
  completed.week_start = "2026-06-22";
  completed.week_end = "2026-06-29";
  completed.content_json = R"({
    "schema_version": 1,
    "headline": "Continue the renderer migration",
    "primary_thread": {
      "title": "Renderer migration",
      "status_summary": "The adapter is ready. data:text/plain,previous-secret",
      "next_step": "Wire the final call site.",
      "confidence": 0.9,
      "source_refs": ["page_99"]
    },
    "secondary_threads": [{
      "title": "Cleanup",
      "status_summary": "One warning remains.",
      "next_step": "Remove the legacy flag.",
      "confidence": 0.7,
      "source_refs": ["conversation_99"]
    }],
    "retained_outcomes": [{
      "text": "The staged approach was selected. www.previous-secret.test/path",
      "confidence": 0.8,
      "source_refs": ["page_99"]
    }],
    "footprint_summary": {
      "themes": ["Migration"],
      "time_pattern": "Most work happened in the morning. //previous-secret.test/path"
    }
  })";
  completed.material_stats = "{}";
  completed.status = "completed";
  completed.attempt_count = 1;
  completed.trigger_kind = "scheduled";
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, std::move(completed)));

  WeeklyDreamReport failed;
  failed.week_start = "2026-06-29";
  failed.week_end = "2026-07-06";
  failed.content_json = R"({"headline":"Failed report"})";
  failed.material_stats = "{}";
  failed.status = "failed";
  failed.attempt_count = 1;
  failed.trigger_kind = "scheduled";
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, std::move(failed)));

  const base::Time window_end = base::Time::Now();
  WeeklyDreamMaterial material = CollectWeeklyMaterialForTest(
      profile, memory, window_end - base::Days(7), window_end,
      "2026-07-06", "2026-07-13");
  const base::ListValue* daily_reports =
      material.model_material.FindList("daily_reports");
  ASSERT_TRUE(daily_reports);
  ASSERT_EQ(7u, daily_reports->size());
  const base::DictValue* previous_week =
      material.model_material.FindDict("previous_week");
  ASSERT_TRUE(previous_week);
  const base::DictValue* content = previous_week->FindDict("content");
  ASSERT_TRUE(content);
  EXPECT_EQ("Continue the renderer migration",
            *content->FindString("headline"));

  std::string json;
  ASSERT_TRUE(base::JSONWriter::Write(material.model_material, &json));
  EXPECT_EQ(std::string::npos, json.find("source_refs"));
  EXPECT_EQ(std::string::npos, json.find("page_99"));
  EXPECT_EQ(std::string::npos, json.find("conversation_99"));
  EXPECT_EQ(std::string::npos, json.find("Failed report"));
  EXPECT_EQ(std::string::npos, json.find("about:blank"));
  EXPECT_EQ(std::string::npos, json.find("mailto:daily-secret"));
  EXPECT_EQ(std::string::npos, json.find("data:text/plain,previous-secret"));
  EXPECT_EQ(std::string::npos, json.find("www.previous-secret"));
  EXPECT_EQ(std::string::npos, json.find("//previous-secret"));
  EXPECT_NE(std::string::npos, json.find("Status: done"));
  const base::DictValue* stats = material.model_material.FindDict("stats");
  ASSERT_TRUE(stats);
  EXPECT_EQ(7, stats->FindInt("daily_reports").value_or(0));
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyCollectorCapsEverySourceType) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  base::ListValue excluded;
  excluded.Append("blocked.example.com");
  profile->GetPrefs()->SetList(prefs::kDaoDreamExcludedDomains,
                               std::move(excluded));
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  const base::Time window_end = base::Time::Now();
  const base::Time window_start = window_end - base::Days(7);

  const GURL excluded_url(
      "https://private.blocked.example.com/secret-page");
  history->AddPageWithDetails(excluded_url, u"Excluded highest page", 1, 0,
                              window_end - base::Minutes(5), false,
                              history::SOURCE_BROWSED);
  SetForegroundDurationForUrl(history, excluded_url, base::Hours(2));

  for (int i = 0; i < 55; ++i) {
    const std::string domain = base::StringPrintf("d%02d.example.com", i);
    const GURL url("https://" + domain + "/source");
    const std::string title =
        i < 2 ? "Shared duplicate title"
              : base::StringPrintf("Domain title %02d", i);
    history->AddPageWithDetails(
        url, base::UTF8ToUTF16(title), 1, 0,
        window_end - base::Minutes(300 + i), false,
        history::SOURCE_BROWSED);
  }
  for (int i = 0; i < 7; ++i) {
    const GURL url(base::StringPrintf(
        "https://titles.example.com/source-%d", i));
    history->AddPageWithDetails(
        url, base::UTF8ToUTF16(base::StringPrintf("Diverse title %d", i)), 1,
        0, window_end - base::Minutes(100 + i), false,
        history::SOURCE_BROWSED);
  }

  for (int i = 0; i < 12; ++i) {
    ConversationSummary summary;
    summary.session_id = base::StringPrintf("cap-summary-session-%02d", i);
    summary.summary = std::string(300, 'S') + base::StringPrintf("%02d", i);
    summary.message_count = i + 1;
    summary.first_timestamp = window_end - base::Hours(8);
    summary.last_timestamp = window_end - base::Minutes(100 + i);
    summary.primary_domain = "docs.example";
    ASSERT_TRUE(SaveConversationSummaryForTest(memory, std::move(summary)));
  }
  std::vector<ConversationMessage> fallback_messages;
  for (int i = 0; i < 25; ++i) {
    ConversationMessage message;
    message.role = "user";
    message.content = std::string(300, 'Q') + "?";
    message.timestamp = window_end - base::Minutes(50 - i);
    fallback_messages.push_back(std::move(message));
  }
  ASSERT_TRUE(SaveConversationMessagesForTest(
      memory, "cap-fallback-session", std::move(fallback_messages)));

  for (int day = 1; day <= 10; ++day) {
    DreamReport daily;
    daily.dream_date = base::StringPrintf("2026-06-%02d", day);
    daily.report_markdown = std::string(300, 'D');
    daily.habit_candidates = "[]";
    daily.material_stats = "{}";
    daily.status = "completed";
    daily.attempt_count = 1;
    daily.trigger_kind = "nightly";
    ASSERT_TRUE(SaveDreamReportForTest(memory, std::move(daily)));
  }
  for (const auto& [week_start, headline] :
       std::vector<std::pair<std::string, std::string>>{
           {"2026-05-18", "Older previous report"},
           {"2026-05-25", "Latest previous report"}}) {
    WeeklyDreamReport previous;
    previous.week_start = week_start;
    previous.week_end = week_start;
    previous.content_json =
        base::StringPrintf("{\"headline\":\"%s\"}", headline.c_str());
    previous.material_stats = "{}";
    previous.status = "completed";
    previous.attempt_count = 1;
    previous.trigger_kind = "scheduled";
    ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, std::move(previous)));
  }

  WeeklyDreamMaterial material = CollectWeeklyMaterialForTest(
      profile, memory, window_start, window_end, "2026-06-01",
      "2026-07-01");
  WeeklyDreamMaterial repeated_material = CollectWeeklyMaterialForTest(
      profile, memory, window_start, window_end, "2026-06-01",
      "2026-07-01");
  std::string material_json;
  std::string repeated_material_json;
  ASSERT_TRUE(
      base::JSONWriter::Write(material.model_material, &material_json));
  ASSERT_TRUE(base::JSONWriter::Write(repeated_material.model_material,
                                      &repeated_material_json));
  EXPECT_EQ(material_json, repeated_material_json);
  EXPECT_EQ(std::string::npos, material_json.find("blocked.example.com"));
  EXPECT_EQ(std::string::npos, material_json.find("Excluded highest page"));

  const base::ListValue* domains =
      material.model_material.FindList("history");
  const base::ListValue* conversations =
      material.model_material.FindList("conversations");
  const base::ListValue* fallback_questions =
      material.model_material.FindList("fallback_questions");
  const base::ListValue* daily_reports =
      material.model_material.FindList("daily_reports");
  ASSERT_TRUE(domains);
  ASSERT_TRUE(conversations);
  ASSERT_TRUE(fallback_questions);
  ASSERT_TRUE(daily_reports);
  EXPECT_EQ(static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxDomains),
            domains->size());
  EXPECT_EQ(9u, conversations->size());
  EXPECT_EQ(static_cast<size_t>(
                WeeklyDreamMaterialCollector::kMaxFallbackMessages),
            fallback_questions->size());
  EXPECT_EQ(
      static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxDailyReports),
      daily_reports->size());

  size_t page_count = 0;
  size_t titles_domain_page_count = 0;
  int shared_title_count = 0;
  std::set<std::string> model_refs;
  for (const base::Value& domain_value : *domains) {
    const base::DictValue& domain = domain_value.GetDict();
    const base::ListValue* pages = domain.FindList("pages");
    ASSERT_TRUE(pages);
    EXPECT_LE(pages->size(),
              static_cast<size_t>(
                  WeeklyDreamMaterialCollector::kMaxTitlesPerDomain));
    const std::string* domain_name = domain.FindString("domain");
    ASSERT_TRUE(domain_name);
    if (*domain_name == "titles.example.com") {
      titles_domain_page_count = pages->size();
    }
    for (const base::Value& page_value : *pages) {
      const base::DictValue& page = page_value.GetDict();
      const std::string* ref_id = page.FindString("ref_id");
      const std::string* title = page.FindString("title");
      ASSERT_TRUE(ref_id);
      ASSERT_TRUE(title);
      EXPECT_TRUE(model_refs.insert(*ref_id).second);
      shared_title_count += *title == "Shared duplicate title";
    }
    page_count += pages->size();
  }
  EXPECT_EQ(
      static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxPageSources),
      page_count);
  EXPECT_EQ(static_cast<size_t>(
                WeeklyDreamMaterialCollector::kMaxTitlesPerDomain),
            titles_domain_page_count);
  EXPECT_EQ(1, shared_title_count);

  for (const base::Value& conversation : *conversations) {
    const std::string* ref_id =
        conversation.GetDict().FindString("ref_id");
    ASSERT_TRUE(ref_id);
    model_refs.insert(*ref_id);
  }
  for (const base::Value& question : *fallback_questions) {
    const std::string* ref_id = question.GetDict().FindString("ref_id");
    ASSERT_TRUE(ref_id);
    model_refs.insert(*ref_id);
  }

  size_t local_pages = 0;
  size_t local_conversations = 0;
  std::set<std::string> local_refs;
  std::vector<std::string> source_signature;
  for (const WeeklyDreamSource& source : material.local_sources) {
    local_pages += source.source_kind == "page";
    local_conversations += source.source_kind == "conversation";
    EXPECT_TRUE(local_refs.insert(source.ref_id).second);
    source_signature.push_back(source.ref_id + "|" + source.source_kind +
                               "|" + source.local_locator);
  }
  EXPECT_EQ(
      static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxPageSources),
      local_pages);
  EXPECT_EQ(static_cast<size_t>(
                WeeklyDreamMaterialCollector::kMaxConversationSources),
            local_conversations);
  EXPECT_EQ(page_count, local_pages);
  EXPECT_EQ(model_refs, local_refs);

  std::vector<std::string> repeated_source_signature;
  for (const WeeklyDreamSource& source : repeated_material.local_sources) {
    repeated_source_signature.push_back(
        source.ref_id + "|" + source.source_kind + "|" +
        source.local_locator);
  }
  EXPECT_EQ(source_signature, repeated_source_signature);

  auto page_1 = std::find_if(
      material.local_sources.begin(), material.local_sources.end(),
      [](const WeeklyDreamSource& source) {
        return source.ref_id == "page_1";
      });
  ASSERT_NE(material.local_sources.end(), page_1);
  EXPECT_EQ("https://titles.example.com/source-0",
            page_1->local_locator);
  EXPECT_FALSE(
      FindWeeklySourceByLocator(material.local_sources, excluded_url.spec()));

  for (const base::Value& conversation : *conversations) {
    const std::string* summary =
        conversation.GetDict().FindString("summary");
    ASSERT_TRUE(summary);
    EXPECT_EQ(
        static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxTextChars),
        summary->size());
  }
  for (const base::Value& question : *fallback_questions) {
    const std::string* text = question.GetDict().FindString("text");
    ASSERT_TRUE(text);
    EXPECT_EQ(
        static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxTextChars),
        text->size());
  }
  for (const base::Value& daily : *daily_reports) {
    const std::string* report =
        daily.GetDict().FindString("report_markdown");
    ASSERT_TRUE(report);
    EXPECT_EQ(
        static_cast<size_t>(WeeklyDreamMaterialCollector::kMaxTextChars),
        report->size());
  }

  const base::DictValue* previous_week =
      material.model_material.FindDict("previous_week");
  ASSERT_TRUE(previous_week);
  const base::DictValue* previous_content =
      previous_week->FindDict("content");
  ASSERT_TRUE(previous_content);
  EXPECT_EQ("Latest previous report",
            *previous_content->FindString("headline"));
  const base::DictValue* stats = material.model_material.FindDict("stats");
  ASSERT_TRUE(stats);
  EXPECT_EQ(WeeklyDreamMaterialCollector::kMaxDomains,
            stats->FindInt("history_domains").value_or(-1));
  EXPECT_EQ(WeeklyDreamMaterialCollector::kMaxPageSources,
            stats->FindInt("page_sources").value_or(-1));
  EXPECT_EQ(WeeklyDreamMaterialCollector::kMaxConversationSources,
            stats->FindInt("conversation_sources").value_or(-1));
  EXPECT_EQ(WeeklyDreamMaterialCollector::kMaxDailyReports,
            stats->FindInt("daily_reports").value_or(-1));
  EXPECT_EQ(WeeklyDreamMaterialCollector::kMaxPageSources +
                WeeklyDreamMaterialCollector::kMaxConversationSources,
            stats->FindInt("source_count").value_or(-1));
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       DreamReportStoreRoundTripWithoutFtsExpecter) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  DreamReport report;
  report.dream_date = "2026-06-12";
  report.report_markdown = "# report";
  report.habit_candidates = "[]";
  report.material_stats = "{}";
  report.status = "completed";
  report.attempt_count = 1;
  report.trigger_kind = "manual";

  bool saved = false;
  base::RunLoop save_loop;
  memory->SaveDreamReport(report,
                          base::BindLambdaForTesting([&](bool ok) {
                            saved = ok;
                            save_loop.Quit();
                          }));
  save_loop.Run();
  ASSERT_TRUE(saved);

  std::optional<DreamReport> got;
  base::RunLoop load_loop;
  memory->GetDreamReportByDate(
      "2026-06-12",
      base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
        got = std::move(r);
        load_loop.Quit();
      }));
  load_loop.Run();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ("# report", got->report_markdown);
}

namespace {

class FakeRunner : public DaoDreamService::Runner {
 public:
  void RunDream(const DaoDreamService::DreamRunRequest& request,
                const base::DictValue& material) override {
    ran = true;
    requests.push_back(request);
    last_material = material.Clone();
    if (quit_closure) {
      std::move(quit_closure).Run();
    }
  }

  const DaoDreamService::DreamRunRequest& last_request() const {
    CHECK(!requests.empty());
    return requests.back();
  }

  bool ran = false;
  std::vector<DaoDreamService::DreamRunRequest> requests;
  base::DictValue last_material;
  base::OnceClosure quit_closure;
};

DreamReport CompletedDailyReport(const std::string& date) {
  DreamReport report;
  report.dream_date = date;
  report.report_markdown = "# Existing daily report";
  report.habit_candidates = "[]";
  report.material_stats = "{}";
  report.status = "completed";
  report.trigger_kind = "nightly";
  return report;
}

void AddTitledHistoryPage(history::HistoryService* history,
                          const GURL& url,
                          const std::u16string& title,
                          base::Time visit_time) {
  history->AddPageWithDetails(url, title, 1, 0, visit_time, false,
                              history::SOURCE_BROWSED);
}

bool IsHistoryUrlAvailableForTest(history::HistoryService* history,
                                  const GURL& url) {
  bool available = false;
  base::CancelableTaskTracker tracker;
  base::RunLoop loop;
  history->QueryURL(
      url,
      base::BindLambdaForTesting([&](history::QueryURLResult result) {
        available = result.success;
        loop.Quit();
      }),
      &tracker);
  loop.Run();
  return available;
}

base::DictValue CallDreamNativeForTest(content::WebContents* dream_contents,
                                       const std::string& method,
                                       base::DictValue params) {
  std::string method_json;
  std::string params_json;
  CHECK(base::JSONWriter::Write(base::Value(method), &method_json));
  CHECK(base::JSONWriter::Write(params, &params_json));

  const std::string script = base::StringPrintf(
      R"JS(
        (async () => {
          const method = %s;
          let bridgeLoaded = false;
          try {
            const {callNative} = await import('./dream_bridge.js');
            bridgeLoaded = true;
            const payload = await callNative(method, %s, {timeoutMs: 2000});
            return JSON.stringify({
              callbackReceived: true,
              isSuccess: true,
              payload: payload === undefined ? null : payload,
            });
          } catch (error) {
            const message = error instanceof Error ?
                error.message : String(error);
            const timedOut = message === 'Timeout calling ' + method;
            return JSON.stringify({
              callbackReceived: bridgeLoaded && !timedOut,
              isSuccess: false,
              payload: message,
            });
          }
        })()
      )JS",
      method_json.c_str(), params_json.c_str());
  std::string result_json =
      content::EvalJs(dream_contents, script).ExtractString();
  std::optional<base::Value> result =
      base::JSONReader::Read(result_json, base::JSON_PARSE_RFC);
  CHECK(result.has_value());
  CHECK(result->is_dict());
  return std::move(*result).TakeDict();
}

const base::DictValue* FindWeeklySourceForTest(
    const base::ListValue& sources,
    const std::string& ref_id) {
  for (const base::Value& value : sources) {
    const base::DictValue* source = value.GetIfDict();
    if (source && source->FindString("refId") &&
        *source->FindString("refId") == ref_id) {
      return source;
    }
  }
  return nullptr;
}

void DrainDreamScheduling(DaoAgentMemoryService* memory) {
  base::RunLoop loop;
  memory->GetDreamReportByDate(
      "1900-01-01",
      base::BindLambdaForTesting(
          [&](std::optional<DreamReport>) {
            memory->GetWeeklyDreamReports(
                1, base::BindLambdaForTesting(
                       [&](std::vector<WeeklyDreamReport>) { loop.Quit(); }));
          }));
  loop.Run();
  base::RunLoop().RunUntilIdle();
}

}  // namespace

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       DailyExistingCheckRevalidatesRunner) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://daily-race.test/runner"),
                       u"Runner removed during report lookup",
                       LocalTime(2026, 7, 13, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  service->ClearRunner(&runner);
  DrainDreamScheduling(memory);

  std::optional<DreamReport> report;
  for (int attempt = 0; attempt < 10; ++attempt) {
    report = LoadDreamReportForTest(memory, "2026-07-13");
    base::RunLoop().RunUntilIdle();
  }
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_FALSE(report.has_value());
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       DailyExistingCheckRevalidatesMasterPref) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://daily-race.test/pref"),
                       u"Dream disabled during report lookup",
                       LocalTime(2026, 7, 13, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  DrainDreamScheduling(memory);

  std::optional<DreamReport> report;
  for (int attempt = 0; attempt < 10; ++attempt) {
    report = LoadDreamReportForTest(memory, "2026-07-13");
    base::RunLoop().RunUntilIdle();
  }
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_FALSE(report.has_value());
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyRunInvalidatesDeferredAutomaticSchedulerReply) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  PrefService* pref_service = profile->GetPrefs();
  pref_service->SetBoolean(prefs::kDaoDreamEnabled, false);
  pref_service->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-19")));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://stale-scheduler.test/page"),
                       u"Manual weekly material",
                       LocalTime(2026, 7, 15, 12, 0));

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);

  int scheduler_reply_count = 0;
  base::OnceClosure deferred_weekly_reply;
  service->SetSchedulerReplyInterceptorForTesting(base::BindRepeating(
      [](int* scheduler_reply_count,
         base::OnceClosure* deferred_weekly_reply,
         base::OnceClosure reply) {
        ++(*scheduler_reply_count);
        if (*scheduler_reply_count == 2) {
          *deferred_weekly_reply = std::move(reply);
          return;
        }
        std::move(reply).Run();
      },
      base::Unretained(&scheduler_reply_count),
      base::Unretained(&deferred_weekly_reply)));

  pref_service->SetBoolean(prefs::kDaoDreamEnabled, true);
  service->TickForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !deferred_weekly_reply.is_null(); }));
  ASSERT_TRUE(runner.requests.empty());

  base::RunLoop callback_loop;
  std::string callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-13",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            EXPECT_FALSE(success);
            callback_error = error;
            callback_loop.Quit();
          }));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  ASSERT_EQ(DaoDreamService::TriggerKind::kManual,
            runner.last_request().trigger_kind);
  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "temporary failure"});
  callback_loop.Run();
  EXPECT_EQ("weekly_provider", callback_error);
  ASSERT_EQ(DaoDreamService::State::kIdle, service->state());

  service->SetSchedulerReplyInterceptorForTesting(
      base::RepeatingCallback<void(base::OnceClosure)>());
  std::move(deferred_weekly_reply).Run();
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  EXPECT_EQ(1u, runner.requests.size());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerRefreshesWindowAfterDeferredReply) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  PrefService* pref_service = profile->GetPrefs();
  pref_service->SetBoolean(prefs::kDaoDreamEnabled, false);
  pref_service->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-19")));
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-26")));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://latest-window.test/page"),
                       u"New latest week material",
                       LocalTime(2026, 7, 22, 12, 0));

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);

  int scheduler_reply_count = 0;
  base::OnceClosure deferred_weekly_reply;
  service->SetSchedulerReplyInterceptorForTesting(base::BindRepeating(
      [](int* scheduler_reply_count,
         base::OnceClosure* deferred_weekly_reply,
         base::OnceClosure reply) {
        ++(*scheduler_reply_count);
        if (*scheduler_reply_count == 2) {
          *deferred_weekly_reply = std::move(reply);
          return;
        }
        std::move(reply).Run();
      },
      base::Unretained(&scheduler_reply_count),
      base::Unretained(&deferred_weekly_reply)));

  pref_service->SetBoolean(prefs::kDaoDreamEnabled, true);
  service->TickForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return !deferred_weekly_reply.is_null(); }));
  ASSERT_TRUE(runner.requests.empty());

  clock.SetNow(LocalTime(2026, 7, 27, 12, 0));
  service->SetSchedulerReplyInterceptorForTesting(
      base::RepeatingCallback<void(base::OnceClosure)>());
  std::move(deferred_weekly_reply).Run();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  EXPECT_EQ(DaoDreamService::ReportKind::kWeekly,
            runner.last_request().report_kind);
  EXPECT_EQ("2026-07-20", runner.last_request().period_start);
  EXPECT_EQ("2026-07-27", runner.last_request().period_end);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerRequiresIndependentPref) {
  Profile* profile = browser()->profile();
  PrefService* pref_service = profile->GetPrefs();
  EXPECT_FALSE(
      pref_service->GetBoolean(prefs::kDaoDreamWeeklyEnabled));
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-12")));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history,
                       GURL("https://weekly-pref.test/private/path"),
                       u"Weekly preference material",
                       LocalTime(2026, 7, 8, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 13, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  DrainDreamScheduling(memory);
  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_TRUE(runner.requests.empty());

  pref_service->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  service->TickForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  EXPECT_EQ(DaoDreamService::ReportKind::kWeekly,
            runner.last_request().report_kind);
  EXPECT_EQ(DaoDreamService::TriggerKind::kScheduledWeekly,
            runner.last_request().trigger_kind);
  EXPECT_EQ("2026-07-06", runner.last_request().period_start);
  EXPECT_EQ("2026-07-13", runner.last_request().period_end);
  std::string serialized_material;
  ASSERT_TRUE(base::JSONWriter::Write(runner.last_material,
                                      &serialized_material));
  EXPECT_EQ(std::string::npos, serialized_material.find("local_locator"));
  EXPECT_EQ(std::string::npos,
            serialized_material.find("https://weekly-pref.test"));
  EXPECT_EQ(std::string::npos, serialized_material.find("/private/path"));
  EXPECT_NE(std::string::npos, serialized_material.find("page_1"));
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerRunsOnlyLatestCompletedWeek) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-13")));

  WeeklyDreamReport older_failed;
  older_failed.week_start = "2026-06-22";
  older_failed.week_end = "2026-06-29";
  older_failed.content_json = "{}";
  older_failed.material_stats = "{}";
  older_failed.status = "failed";
  older_failed.attempt_count = 1;
  older_failed.trigger_kind = "catchup";
  ASSERT_TRUE(
      SaveWeeklyDreamReportForTest(memory, std::move(older_failed)));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://latest-week.test/current"),
                       u"Latest completed week",
                       LocalTime(2026, 7, 9, 10, 0));
  AddTitledHistoryPage(history, GURL("https://older-week.test/ignored"),
                       u"Older missing week",
                       LocalTime(2026, 6, 25, 10, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  EXPECT_EQ(DaoDreamService::ReportKind::kWeekly,
            runner.last_request().report_kind);
  EXPECT_EQ(DaoDreamService::TriggerKind::kCatchUp,
            runner.last_request().trigger_kind);
  EXPECT_EQ("2026-07-06", runner.last_request().period_start);
  EXPECT_EQ("2026-07-13", runner.last_request().period_end);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerTreatsSkippedAsDone) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-13")));

  WeeklyDreamReport skipped;
  skipped.week_start = "2026-07-06";
  skipped.week_end = "2026-07-13";
  skipped.content_json = "{}";
  skipped.material_stats = "{}";
  skipped.status = "skipped";
  skipped.trigger_kind = "scheduled";
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, std::move(skipped)));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  DrainDreamScheduling(memory);
  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerTreatsCompletedAsDone) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-13")));

  WeeklyDreamReport completed;
  completed.week_start = "2026-07-06";
  completed.week_end = "2026-07-13";
  completed.content_json = R"({"schema_version":1})";
  completed.material_stats = "{}";
  completed.status = "completed";
  completed.trigger_kind = "scheduled";
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, std::move(completed)));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  DrainDreamScheduling(memory);
  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_TRUE(runner.requests.empty());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklySchedulerRetriesFailedBelowCapAndBridgeSkipIsIdempotent) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-13")));

  WeeklyDreamReport older_failed;
  older_failed.week_start = "2026-06-29";
  older_failed.week_end = "2026-07-06";
  older_failed.content_json = "{}";
  older_failed.material_stats = "{}";
  older_failed.status = "failed";
  older_failed.attempt_count = 1;
  older_failed.trigger_kind = "catchup";
  ASSERT_TRUE(
      SaveWeeklyDreamReportForTest(memory, std::move(older_failed)));

  WeeklyDreamReport current_failed;
  current_failed.week_start = "2026-07-06";
  current_failed.week_end = "2026-07-13";
  current_failed.content_json = "{}";
  current_failed.material_stats = "{}";
  current_failed.status = "failed";
  current_failed.attempt_count = 2;
  current_failed.trigger_kind = "catchup";
  ASSERT_TRUE(
      SaveWeeklyDreamReportForTest(memory, std::move(current_failed)));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://retry-weekly.test/page"),
                       u"Retryable weekly material",
                       LocalTime(2026, 7, 9, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  EXPECT_EQ("2026-07-06", runner.last_request().period_start);
  service->OnDreamSkipped(runner.last_request().request_id);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return service->state() == DaoDreamService::State::kIdle; }));

  std::optional<WeeklyDreamReport> current =
      LoadWeeklyDreamReportForTest(memory, "2026-07-06");
  ASSERT_TRUE(current.has_value());
  EXPECT_EQ("skipped", current->status);
  EXPECT_EQ(2, current->attempt_count);

  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_EQ(1u, runner.requests.size());
  std::optional<WeeklyDreamReport> older =
      LoadWeeklyDreamReportForTest(memory, "2026-06-29");
  ASSERT_TRUE(older.has_value());
  EXPECT_EQ("failed", older->status);
  EXPECT_EQ(1, older->attempt_count);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerStopsAtFailedAttemptCap) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-13")));

  WeeklyDreamReport failed;
  failed.week_start = "2026-07-06";
  failed.week_end = "2026-07-13";
  failed.content_json = "{}";
  failed.material_stats = "{}";
  failed.status = "failed";
  failed.attempt_count = 3;
  failed.trigger_kind = "catchup";
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, std::move(failed)));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  DrainDreamScheduling(memory);
  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_TRUE(runner.requests.empty());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, DailyCatchUpWinsBeforeWeekly) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://daily-first.test/page"),
                       u"Final daily material",
                       LocalTime(2026, 7, 12, 12, 0));
  AddTitledHistoryPage(history, GURL("https://weekly-after.test/page"),
                       u"Weekly material",
                       LocalTime(2026, 7, 8, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 13, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  const DaoDreamService::DreamRunRequest daily_request =
      runner.last_request();
  EXPECT_EQ(DaoDreamService::ReportKind::kDaily, daily_request.report_kind);
  EXPECT_EQ("2026-07-12", daily_request.period_start);

  base::DictValue daily_result;
  daily_result.Set("report_markdown", "# Final daily report");
  service->OnDreamResult(daily_request.request_id, std::move(daily_result));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 2u; }));
  EXPECT_EQ(DaoDreamService::ReportKind::kWeekly,
            runner.last_request().report_kind);
  EXPECT_EQ("2026-07-06", runner.last_request().period_start);
  EXPECT_NE(daily_request.request_id, runner.last_request().request_id);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyAndDailyShareOneActiveRun) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://one-active.test/page"),
                       u"One active Dream request",
                       LocalTime(2026, 7, 12, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 13, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  const DaoDreamService::DreamRunRequest daily_request =
      runner.last_request();
  ASSERT_EQ(DaoDreamService::ReportKind::kDaily,
            daily_request.report_kind);

  service->TickForTesting();
  service->TickForTesting();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1u, runner.requests.size());

  base::DictValue daily_result;
  daily_result.Set("report_markdown", "# Daily completed first");
  service->OnDreamResult(daily_request.request_id, std::move(daily_result));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 2u; }));
  ASSERT_EQ(DaoDreamService::ReportKind::kWeekly,
            runner.last_request().report_kind);

  service->TickForTesting();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(2u, runner.requests.size());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklySchedulerPersistsEmptyWeekAsSkipped) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-13")));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 14, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));

  FakeRunner runner;
  service->SetRunner(&runner);
  std::optional<WeeklyDreamReport> report =
      WaitForWeeklyDreamReportForTest(memory, "2026-07-06");
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ("2026-07-13", report->week_end);
  EXPECT_EQ("skipped", report->status);
  EXPECT_EQ("catchup", report->trigger_kind);
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_TRUE(LoadWeeklyDreamSourcesForTest(memory, report->id).empty());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyDefaultsToLatestCompletedWeekAndBypassesWeeklyPref) {
  Profile* profile = browser()->profile();
  EXPECT_FALSE(
      profile->GetPrefs()->GetBoolean(prefs::kDaoDreamWeeklyEnabled));
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-19")));

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://manual-weekly.test/private"),
                       u"Manual weekly material",
                       LocalTime(2026, 7, 15, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  DrainDreamScheduling(memory);

  base::RunLoop callback_loop;
  bool callback_success = true;
  std::string callback_error;
  service->StartManualWeeklyDream(base::BindLambdaForTesting(
      [&](bool success, const std::string& error) {
        callback_success = success;
        callback_error = error;
        callback_loop.Quit();
      }));

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  EXPECT_EQ(DaoDreamService::ReportKind::kWeekly,
            runner.last_request().report_kind);
  EXPECT_EQ(DaoDreamService::TriggerKind::kManual,
            runner.last_request().trigger_kind);
  EXPECT_EQ("2026-07-13", runner.last_request().period_start);
  EXPECT_EQ("2026-07-20", runner.last_request().period_end);

  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "stop test run"});
  callback_loop.Run();
  EXPECT_FALSE(callback_success);
  EXPECT_EQ("weekly_provider", callback_error);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       ManualWeeklyValidationReturnsStableMachineCodes) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);

  auto expect_error = [&](const std::string& week_start,
                          const std::string& expected_error) {
    bool callback_called = false;
    bool callback_success = true;
    std::string callback_error;
    service->StartManualWeeklyDreamForWeekStart(
        week_start,
        base::BindLambdaForTesting(
            [&](bool success, const std::string& error) {
              callback_called = true;
              callback_success = success;
              callback_error = error;
            }));
    EXPECT_TRUE(callback_called);
    EXPECT_FALSE(callback_success);
    EXPECT_EQ(expected_error, callback_error);
    EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  };

  expect_error("2026-7-13", "weekly_invalid_week_start");
  expect_error(" 2026-07-13", "weekly_invalid_week_start");
  expect_error("2026-02-30", "weekly_invalid_week_start");
  expect_error("2026-07-12", "weekly_week_start_not_monday");
  expect_error("2026-07-20", "weekly_week_incomplete");
  expect_error("2026-07-27", "weekly_week_incomplete");
  expect_error("2026-07-13", "dream_runner_unavailable");
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyCompletedResultPersistsContentStatsSourcesAndRedactedDebug) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamDebug, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);

  const GURL private_url(
      "https://weekly-persist.test/private/path?token=weekly-secret");
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, private_url, u"Persisted weekly source",
                       LocalTime(2026, 7, 15, 12, 0));

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  base::RunLoop callback_loop;
  int callback_count = 0;
  bool callback_success = false;
  std::string callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-13",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            ++callback_count;
            callback_success = success;
            callback_error = error;
            callback_loop.Quit();
          }));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  const DaoDreamService::DreamRunRequest request = runner.last_request();

  const base::DictValue* stats = runner.last_material.FindDict("stats");
  ASSERT_TRUE(stats);
  std::string expected_stats;
  ASSERT_TRUE(base::JSONWriter::Write(*stats, &expected_stats));

  base::DictValue stale_result;
  stale_result.Set("headline", "Stale weekly result");
  service->OnDreamResult("stale-weekly-request-id", std::move(stale_result));
  EXPECT_EQ(DaoDreamService::State::kDreaming, service->state());

  base::DictValue result;
  result.Set("schema_version", 1);
  result.Set("headline", "A validated weekly result");
  base::DictValue primary_thread;
  primary_thread.Set("title", "Persistence work");
  base::ListValue source_refs;
  source_refs.Append("page_1");
  primary_thread.Set("source_refs", std::move(source_refs));
  result.Set("primary_thread", std::move(primary_thread));
  base::ListValue habits;
  base::DictValue habit;
  habit.Set("key", "weekly.must_not_merge");
  habit.Set("value", "Weekly output is report-only.");
  habit.Set("confidence", 0.99);
  habits.Append(std::move(habit));
  result.Set("habits", std::move(habits));
  std::string expected_content;
  ASSERT_TRUE(base::JSONWriter::Write(result, &expected_content));

  service->OnDreamResult(request.request_id, std::move(result));
  EXPECT_EQ(DaoDreamService::State::kSaving, service->state());
  base::DictValue duplicate_result;
  duplicate_result.Set("headline", "Duplicate result must be ignored");
  service->OnDreamResult(request.request_id, std::move(duplicate_result));
  callback_loop.Run();

  EXPECT_EQ(1, callback_count);
  EXPECT_TRUE(callback_success) << callback_error;
  std::optional<WeeklyDreamReport> report =
      LoadWeeklyDreamReportForTest(memory, "2026-07-13");
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ("2026-07-20", report->week_end);
  EXPECT_EQ("completed", report->status);
  EXPECT_EQ(1, report->attempt_count);
  EXPECT_EQ("manual", report->trigger_kind);
  EXPECT_EQ(expected_content, report->content_json);
  EXPECT_EQ(expected_stats, report->material_stats);
  EXPECT_NE(std::string::npos,
            report->debug_material_json.find("page_1"));
  EXPECT_EQ(std::string::npos,
            report->debug_material_json.find("local_locator"));
  EXPECT_EQ(std::string::npos,
            report->debug_material_json.find(private_url.spec()));
  EXPECT_EQ(std::string::npos,
            report->debug_material_json.find("/private/path"));
  EXPECT_EQ(std::string::npos,
            report->debug_material_json.find("weekly-secret"));

  std::vector<WeeklyDreamSource> sources =
      LoadWeeklyDreamSourcesForTest(memory, report->id);
  ASSERT_EQ(1u, sources.size());
  EXPECT_EQ("page_1", sources[0].ref_id);
  EXPECT_EQ(private_url.spec(), sources[0].local_locator);

  std::vector<Preference> preferences;
  base::RunLoop preferences_loop;
  memory->GetPreferences(
      100, 0.0,
      base::BindLambdaForTesting([&](std::vector<Preference> values) {
        preferences = std::move(values);
        preferences_loop.Quit();
      }));
  preferences_loop.Run();
  EXPECT_TRUE(std::none_of(
      preferences.begin(), preferences.end(), [](const Preference& pref) {
        return pref.key == "weekly.must_not_merge";
      }));
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyEmptyWeekPersistsSkippedAndReturnsNoMaterial) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  base::RunLoop callback_loop;
  bool callback_success = true;
  std::string callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-13",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            callback_success = success;
            callback_error = error;
            callback_loop.Quit();
          }));
  callback_loop.Run();

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("weekly_no_material", callback_error);
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  std::optional<WeeklyDreamReport> report =
      LoadWeeklyDreamReportForTest(memory, "2026-07-13");
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ("2026-07-20", report->week_end);
  EXPECT_EQ("skipped", report->status);
  EXPECT_EQ("manual", report->trigger_kind);
  EXPECT_TRUE(LoadWeeklyDreamSourcesForTest(memory, report->id).empty());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       WeeklyProviderFailureDefersWithoutConsumingAttempt) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-12")));
  WeeklyDreamReport existing;
  existing.week_start = "2026-07-06";
  existing.week_end = "2026-07-13";
  existing.content_json = R"({"headline":"Existing failed report"})";
  existing.material_stats = R"({"source_count":1})";
  existing.status = "failed";
  existing.attempt_count = 2;
  existing.trigger_kind = "catchup";
  existing.debug_material_json = R"({"debug":"existing"})";
  WeeklyDreamSource existing_source;
  existing_source.ref_id = "old_1";
  existing_source.source_kind = "page";
  existing_source.title = "Existing failed source";
  existing_source.domain = "existing-provider.test";
  existing_source.local_locator =
      "https://existing-provider.test/private/source";
  existing_source.last_seen_at = LocalTime(2026, 7, 8, 12, 0);
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(
      memory, existing, {existing_source}));
  std::optional<WeeklyDreamReport> original =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(original.has_value());
  const int64_t original_id = original->id;
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://weekly-provider.test/page"),
                       u"Provider defer material",
                       LocalTime(2026, 7, 9, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 13, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));
  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));

  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{
          "provider", "sensitive provider failure must not escape"});
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  std::optional<WeeklyDreamReport> after_failure =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(after_failure.has_value());
  EXPECT_EQ(original_id, after_failure->id);
  EXPECT_EQ(existing.content_json, after_failure->content_json);
  EXPECT_EQ(existing.material_stats, after_failure->material_stats);
  EXPECT_EQ(existing.status, after_failure->status);
  EXPECT_EQ(existing.attempt_count, after_failure->attempt_count);
  EXPECT_EQ(existing.trigger_kind, after_failure->trigger_kind);
  EXPECT_EQ(existing.debug_material_json,
            after_failure->debug_material_json);
  std::vector<WeeklyDreamSource> after_failure_sources =
      LoadWeeklyDreamSourcesForTest(memory, after_failure->id);
  ASSERT_EQ(1u, after_failure_sources.size());
  EXPECT_EQ(existing_source.ref_id, after_failure_sources[0].ref_id);
  EXPECT_EQ(existing_source.local_locator,
            after_failure_sources[0].local_locator);

  clock.SetNow(LocalTime(2026, 7, 13, 12, 59));
  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_EQ(1u, runner.requests.size());

  clock.SetNow(LocalTime(2026, 7, 13, 13, 0));
  service->TickForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 2u; }));
  std::optional<WeeklyDreamReport> at_retry =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(at_retry.has_value());
  EXPECT_EQ(original_id, at_retry->id);
  EXPECT_EQ(2, at_retry->attempt_count);
  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "stop retry"});
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyConfigurationAndProviderUseStableCodesAndBypassDefer) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://manual-transient.test/page"),
                       u"Manual transient material",
                       LocalTime(2026, 7, 9, 12, 0));

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  auto run_failure = [&](const std::string& failure_code,
                         const std::string& expected_error,
                         size_t expected_request_count) {
    base::RunLoop callback_loop;
    bool callback_success = true;
    std::string callback_error;
    service->StartManualWeeklyDreamForWeekStart(
        "2026-07-06",
        base::BindLambdaForTesting(
            [&](bool success, const std::string& error) {
              callback_success = success;
              callback_error = error;
              callback_loop.Quit();
            }));
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return runner.requests.size() == expected_request_count;
    }));
    service->OnDreamFailed(
        runner.last_request().request_id,
        DaoDreamService::DreamRunFailure{
            failure_code, "sensitive provider detail"});
    callback_loop.Run();
    EXPECT_FALSE(callback_success);
    EXPECT_EQ(expected_error, callback_error);
  };

  run_failure("configuration", "weekly_configuration", 1u);
  // The first failure established a one-hour automatic defer. An explicit
  // manual run still starts immediately.
  run_failure("provider", "weekly_provider", 2u);
  EXPECT_FALSE(
      LoadWeeklyDreamReportForTest(memory, "2026-07-06").has_value());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyTimeoutDefersWithoutConsumingAttemptAndIgnoresLateResult) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-12")));
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://weekly-timeout.test/page"),
                       u"Timeout defer material",
                       LocalTime(2026, 7, 9, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 13, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));
  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  const DaoDreamService::DreamRunRequest timed_out_request =
      runner.last_request();

  service->FireDreamTimeoutForTesting();
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  base::DictValue late_result;
  late_result.Set("headline", "Late weekly result");
  service->OnDreamResult(timed_out_request.request_id,
                         std::move(late_result));
  EXPECT_FALSE(
      LoadWeeklyDreamReportForTest(memory, "2026-07-06").has_value());

  clock.SetNow(LocalTime(2026, 7, 13, 12, 59));
  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_EQ(1u, runner.requests.size());
  clock.SetNow(LocalTime(2026, 7, 13, 13, 0));
  service->TickForTesting();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 2u; }));
  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "stop retry"});
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyInvalidOutputConsumesAttemptAndStopsAtThree) {
  Profile* profile = browser()->profile();
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamWeeklyEnabled, true);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamDebug, true);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  ASSERT_TRUE(
      SaveDreamReportForTest(memory, CompletedDailyReport("2026-07-12")));

  WeeklyDreamReport existing;
  existing.week_start = "2026-07-06";
  existing.week_end = "2026-07-13";
  existing.content_json = "{}";
  existing.material_stats = R"({"source_count":1})";
  existing.status = "failed";
  existing.attempt_count = 2;
  existing.trigger_kind = "catchup";
  WeeklyDreamSource old_source;
  old_source.ref_id = "old_1";
  old_source.source_kind = "page";
  old_source.title = "Old failed source";
  old_source.domain = "old-invalid.test";
  old_source.local_locator = "https://old-invalid.test/private";
  old_source.last_seen_at = LocalTime(2026, 7, 8, 12, 0);
  ASSERT_TRUE(
      SaveWeeklyDreamReportForTest(memory, existing, {old_source}));
  std::optional<WeeklyDreamReport> old_report =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(old_report.has_value());
  const int64_t old_report_id = old_report->id;

  const GURL private_url(
      "https://weekly-invalid.test/private/path?secret=invalid-output");
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, private_url, u"Invalid output material",
                       LocalTime(2026, 7, 9, 12, 0));

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 13, 12, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 600; }));
  FakeRunner runner;
  service->SetRunner(&runner);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  const base::DictValue* stats = runner.last_material.FindDict("stats");
  ASSERT_TRUE(stats);
  std::string expected_stats;
  ASSERT_TRUE(base::JSONWriter::Write(*stats, &expected_stats));

  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{
          "invalid_output", "invalid output detail must not be persisted"});
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return service->state() == DaoDreamService::State::kIdle; }));

  std::optional<WeeklyDreamReport> failed =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(failed.has_value());
  EXPECT_NE(old_report_id, failed->id);
  EXPECT_EQ("2026-07-13", failed->week_end);
  EXPECT_EQ("{}", failed->content_json);
  EXPECT_EQ("failed", failed->status);
  EXPECT_EQ(3, failed->attempt_count);
  EXPECT_EQ("scheduled", failed->trigger_kind);
  EXPECT_EQ(expected_stats, failed->material_stats);
  EXPECT_NE(std::string::npos,
            failed->debug_material_json.find("page_1"));
  EXPECT_EQ(std::string::npos,
            failed->debug_material_json.find("local_locator"));
  EXPECT_EQ(std::string::npos,
            failed->debug_material_json.find(private_url.spec()));
  EXPECT_EQ(std::string::npos,
            failed->debug_material_json.find("/private/path"));
  EXPECT_EQ(std::string::npos,
            failed->debug_material_json.find("invalid-output"));
  EXPECT_TRUE(
      LoadWeeklyDreamSourcesForTest(memory, failed->id).empty());
  EXPECT_TRUE(
      LoadWeeklyDreamSourcesForTest(memory, old_report_id).empty());

  service->TickForTesting();
  DrainDreamScheduling(memory);
  EXPECT_EQ(1u, runner.requests.size());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyFailuresPreserveCompletedReportAndSources) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);

  WeeklyDreamReport existing;
  existing.week_start = "2026-07-06";
  existing.week_end = "2026-07-13";
  existing.content_json = R"({"headline":"Keep completed report"})";
  existing.material_stats = R"({"source_count":1,"coverage_days":4})";
  existing.status = "completed";
  existing.attempt_count = 1;
  existing.trigger_kind = "scheduled";
  existing.debug_material_json = R"({"safe":"existing debug"})";
  WeeklyDreamSource existing_source;
  existing_source.ref_id = "existing_1";
  existing_source.source_kind = "page";
  existing_source.title = "Completed source";
  existing_source.domain = "completed-failure.test";
  existing_source.local_locator =
      "https://completed-failure.test/private/original";
  existing_source.last_seen_at = LocalTime(2026, 7, 8, 12, 0);
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(
      memory, existing, {existing_source}));
  std::optional<WeeklyDreamReport> original =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(original.has_value());
  const int64_t original_id = original->id;

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history,
                       GURL("https://manual-failure.test/new/material"),
                       u"Manual failure material",
                       LocalTime(2026, 7, 9, 12, 0));
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  auto expect_original_preserved = [&]() {
    std::optional<WeeklyDreamReport> report =
        LoadWeeklyDreamReportForTest(memory, existing.week_start);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(original_id, report->id);
    EXPECT_EQ(existing.content_json, report->content_json);
    EXPECT_EQ(existing.material_stats, report->material_stats);
    EXPECT_EQ(existing.status, report->status);
    EXPECT_EQ(existing.attempt_count, report->attempt_count);
    EXPECT_EQ(existing.trigger_kind, report->trigger_kind);
    EXPECT_EQ(existing.debug_material_json, report->debug_material_json);
    EXPECT_EQ(original->created_at, report->created_at);
    EXPECT_EQ(original->viewed_at, report->viewed_at);
    std::vector<WeeklyDreamSource> sources =
        LoadWeeklyDreamSourcesForTest(memory, report->id);
    ASSERT_EQ(1u, sources.size());
    EXPECT_EQ(existing_source.ref_id, sources[0].ref_id);
    EXPECT_EQ(existing_source.local_locator, sources[0].local_locator);
  };

  auto run_bridge_failure = [&](const std::string& failure_code,
                                const std::string& expected_error,
                                size_t expected_request_count) {
    base::RunLoop callback_loop;
    bool callback_success = true;
    std::string callback_error;
    service->StartManualWeeklyDreamForWeekStart(
        existing.week_start,
        base::BindLambdaForTesting(
            [&](bool success, const std::string& error) {
              callback_success = success;
              callback_error = error;
              callback_loop.Quit();
            }));
    ASSERT_TRUE(base::test::RunUntil([&]() {
      return runner.requests.size() == expected_request_count;
    }));
    service->OnDreamFailed(
        runner.last_request().request_id,
        DaoDreamService::DreamRunFailure{
            failure_code, "sensitive provider detail must not escape"});
    callback_loop.Run();
    EXPECT_FALSE(callback_success);
    EXPECT_EQ(expected_error, callback_error);
    expect_original_preserved();
  };

  run_bridge_failure("configuration", "weekly_configuration", 1u);
  run_bridge_failure("provider", "weekly_provider", 2u);

  base::RunLoop timeout_callback_loop;
  int timeout_callback_count = 0;
  bool timeout_callback_success = true;
  std::string timeout_callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      existing.week_start,
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            ++timeout_callback_count;
            timeout_callback_success = success;
            timeout_callback_error = error;
            timeout_callback_loop.Quit();
          }));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 3u; }));
  const DaoDreamService::DreamRunRequest timed_out_request =
      runner.last_request();
  service->FireDreamTimeoutForTesting();
  timeout_callback_loop.Run();
  EXPECT_EQ(1, timeout_callback_count);
  EXPECT_FALSE(timeout_callback_success);
  EXPECT_EQ("weekly_timeout", timeout_callback_error);
  base::DictValue late_result;
  late_result.Set("headline", "Late result must not replace completed data");
  service->OnDreamResult(timed_out_request.request_id,
                         std::move(late_result));
  service->OnDreamFailed(
      timed_out_request.request_id,
      DaoDreamService::DreamRunFailure{"invalid_output", "late failure"});
  EXPECT_EQ(1, timeout_callback_count);
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  expect_original_preserved();

  run_bridge_failure("invalid_output", "weekly_invalid_output", 4u);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyRunnerDisappearsDuringCollectionIsTransient) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://runner-loss.test/page"),
                       u"Runner loss material",
                       LocalTime(2026, 7, 9, 12, 0));

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetWeeklyCollectionStartedCallbackForTesting(
      base::BindOnce(&DaoDreamService::ClearRunner,
                     base::Unretained(service), base::Unretained(&runner)));
  service->SetRunner(&runner);

  base::RunLoop callback_loop;
  bool callback_success = true;
  std::string callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-06",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            callback_success = success;
            callback_error = error;
            callback_loop.Quit();
          }));

  callback_loop.Run();

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("dream_runner_unavailable", callback_error);
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  EXPECT_FALSE(
      LoadWeeklyDreamReportForTest(memory, "2026-07-06").has_value());
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyRerunPreservesCompletedReportAndSources) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);

  WeeklyDreamReport existing;
  existing.week_start = "2026-07-06";
  existing.week_end = "2026-07-13";
  existing.content_json = R"({"headline":"Original weekly report"})";
  existing.material_stats = R"({"source_count":1})";
  existing.status = "completed";
  existing.attempt_count = 1;
  existing.trigger_kind = "scheduled";
  existing.debug_material_json = R"({"safe":"old debug"})";

  WeeklyDreamSource existing_source;
  existing_source.ref_id = "legacy_1";
  existing_source.source_kind = "page";
  existing_source.title = "Original source";
  existing_source.domain = "original-weekly.test";
  existing_source.local_locator =
      "https://original-weekly.test/private/original";
  existing_source.last_seen_at = LocalTime(2026, 7, 9, 10, 0);
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(
      memory, existing, {existing_source}));
  std::optional<WeeklyDreamReport> original =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(original.has_value());
  const int64_t original_id = original->id;

  auto expect_original_preserved = [&]() {
    std::optional<WeeklyDreamReport> report =
        LoadWeeklyDreamReportForTest(memory, existing.week_start);
    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(original_id, report->id);
    EXPECT_EQ(existing.content_json, report->content_json);
    EXPECT_EQ(existing.material_stats, report->material_stats);
    EXPECT_EQ(existing.status, report->status);
    EXPECT_EQ(existing.attempt_count, report->attempt_count);
    EXPECT_EQ(existing.trigger_kind, report->trigger_kind);
    EXPECT_EQ(existing.debug_material_json, report->debug_material_json);
    std::vector<WeeklyDreamSource> sources =
        LoadWeeklyDreamSourcesForTest(memory, report->id);
    ASSERT_EQ(1u, sources.size());
    EXPECT_EQ(existing_source.ref_id, sources[0].ref_id);
    EXPECT_EQ(existing_source.local_locator, sources[0].local_locator);
  };

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  // An empty rerun must finish without replacing the completed report.
  {
    base::RunLoop callback_loop;
    bool callback_success = true;
    std::string callback_error;
    service->StartManualWeeklyDreamForWeekStart(
        existing.week_start,
        base::BindLambdaForTesting(
            [&](bool success, const std::string& error) {
              callback_success = success;
              callback_error = error;
              callback_loop.Quit();
            }));
    callback_loop.Run();
    EXPECT_FALSE(callback_success);
    EXPECT_EQ("weekly_no_material", callback_error);
    EXPECT_TRUE(runner.requests.empty());
    expect_original_preserved();
  }

  const GURL replacement_url(
      "https://replacement-weekly.test/private/replacement");
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, replacement_url, u"Replacement source",
                       LocalTime(2026, 7, 9, 12, 0));

  // A bridge sparse/skip result follows the same preservation path.
  {
    base::RunLoop callback_loop;
    bool callback_success = true;
    std::string callback_error;
    service->StartManualWeeklyDreamForWeekStart(
        existing.week_start,
        base::BindLambdaForTesting(
            [&](bool success, const std::string& error) {
              callback_success = success;
              callback_error = error;
              callback_loop.Quit();
            }));
    ASSERT_TRUE(base::test::RunUntil(
        [&]() { return runner.requests.size() == 1u; }));
    service->OnDreamSkipped(runner.last_request().request_id);
    callback_loop.Run();
    EXPECT_FALSE(callback_success);
    EXPECT_EQ("weekly_no_material", callback_error);
    expect_original_preserved();
  }

  // A validated result atomically replaces both the report and source map.
  base::RunLoop callback_loop;
  bool callback_success = false;
  std::string callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      existing.week_start,
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            callback_success = success;
            callback_error = error;
            callback_loop.Quit();
          }));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 2u; }));
  base::DictValue replacement_result;
  replacement_result.Set("schema_version", 1);
  replacement_result.Set("headline", "Replacement weekly report");
  base::DictValue primary_thread;
  primary_thread.Set("title", "Replacement thread");
  base::ListValue source_refs;
  source_refs.Append("page_1");
  primary_thread.Set("source_refs", std::move(source_refs));
  replacement_result.Set("primary_thread", std::move(primary_thread));
  std::string expected_replacement_content;
  ASSERT_TRUE(base::JSONWriter::Write(replacement_result,
                                      &expected_replacement_content));
  service->OnDreamResult(runner.last_request().request_id,
                         std::move(replacement_result));
  callback_loop.Run();
  EXPECT_TRUE(callback_success) << callback_error;

  std::optional<WeeklyDreamReport> replacement =
      LoadWeeklyDreamReportForTest(memory, existing.week_start);
  ASSERT_TRUE(replacement.has_value());
  EXPECT_NE(original_id, replacement->id);
  EXPECT_EQ("completed", replacement->status);
  EXPECT_EQ("manual", replacement->trigger_kind);
  EXPECT_EQ(2, replacement->attempt_count);
  EXPECT_EQ(expected_replacement_content, replacement->content_json);
  std::vector<WeeklyDreamSource> replacement_sources =
      LoadWeeklyDreamSourcesForTest(memory, replacement->id);
  ASSERT_EQ(1u, replacement_sources.size());
  EXPECT_EQ("page_1", replacement_sources[0].ref_id);
  EXPECT_EQ(replacement_url.spec(), replacement_sources[0].local_locator);
  EXPECT_NE(existing_source.local_locator,
            replacement_sources[0].local_locator);
  EXPECT_TRUE(
      LoadWeeklyDreamSourcesForTest(memory, original_id).empty());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    ManualWeeklyLookupOccupiesStateAndRejectsConcurrentRun) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  AddTitledHistoryPage(history, GURL("https://manual-busy.test/page"),
                       u"Manual busy material",
                       LocalTime(2026, 7, 15, 12, 0));

  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  base::RunLoop first_callback_loop;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-13",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            EXPECT_FALSE(success);
            EXPECT_EQ("weekly_provider", error);
            first_callback_loop.Quit();
          }));
  EXPECT_EQ(DaoDreamService::State::kCollecting, service->state());

  bool second_callback_called = false;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-13",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            second_callback_called = true;
            EXPECT_FALSE(success);
            EXPECT_EQ("dream_busy", error);
          }));
  EXPECT_TRUE(second_callback_called);
  EXPECT_EQ(DaoDreamService::State::kCollecting, service->state());

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return runner.requests.size() == 1u; }));
  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "stop test run"});
  first_callback_loop.Run();
  EXPECT_EQ(1u, runner.requests.size());
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       ManualWeeklyLookupRevalidatesRunner) {
  Profile* profile = browser()->profile();
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  profile->GetPrefs()->SetBoolean(prefs::kDaoDreamEnabled, false);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 7, 20, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop callback_loop;
  bool callback_success = true;
  std::string callback_error;
  service->StartManualWeeklyDreamForWeekStart(
      "2026-07-13",
      base::BindLambdaForTesting(
          [&](bool success, const std::string& error) {
            callback_success = success;
            callback_error = error;
            callback_loop.Quit();
          }));
  service->ClearRunner(&runner);
  callback_loop.Run();

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("dream_runner_unavailable", callback_error);
  EXPECT_TRUE(runner.requests.empty());
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, SchedulerSkipsWhenNotIdle) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  FakeRunner runner;
  service->SetRunner(&runner);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 10, 23, 30));  // night
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 60; }));  // only 1 min idle
  service->TickForTesting();
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
  EXPECT_FALSE(runner.ran);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, SchedulerSkipsWithoutRunner) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 10, 23, 30));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 7200; }));
  // No runner registered → tick must NOT start (WebUI unavailable).
  service->TickForTesting();
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       RunnerRegistrationStartsCatchUpWithoutIdle) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/yesterday"),
                   LocalTime(2026, 6, 10, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 11, 9, 0));
  service->SetClockForTesting(&clock);
  service->SetIdleTimeCallbackForTesting(base::BindRepeating([] {
    return 0;
  }));

  FakeRunner runner;
  service->SetRunner(&runner);

  EXPECT_TRUE(base::test::RunUntil([&]() { return runner.ran; }));
  EXPECT_EQ(DaoDreamService::ReportKind::kDaily,
            runner.last_request().report_kind);
  EXPECT_FALSE(runner.last_request().request_id.empty());
  EXPECT_EQ("2026-06-10", runner.last_request().period_start);
  EXPECT_EQ("2026-06-11", runner.last_request().period_end);
  EXPECT_EQ(DaoDreamService::TriggerKind::kCatchUp,
            runner.last_request().trigger_kind);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       SchedulerStartsNightlyWhenIdleAtNight) {
  // Seed one history row so material is non-empty and the run reaches
  // the runner instead of being skipped.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/page"), base::Time::Now(),
                   history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  FakeRunner runner;
  service->SetRunner(&runner);

  // Use the real wall clock so the material window (dream date 06:00 →
  // now) covers the freshly added history row regardless of test time.
  service->SetIdleTimeCallbackForTesting(
      base::BindRepeating([] { return 7200; }));

  // Drive via manual start (same StartDream path as nightly, minus the
  // night/idle gate already covered above).
  base::RunLoop loop;
  runner.quit_closure = loop.QuitClosure();
  bool callback_fired = false;
  service->StartManualDream(base::BindLambdaForTesting(
      [&](bool ok, const std::string& error) { callback_fired = ok; }));
  loop.Run();

  EXPECT_TRUE(runner.ran);
  EXPECT_EQ(DaoDreamService::State::kDreaming, service->state());

  // Complete the dream via the result path and verify persistence.
  base::DictValue result;
  result.Set("report_markdown", "# nightly report");
  service->OnDreamResult(runner.last_request().request_id, std::move(result));

  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);
  // Poll until the save lands (kSaving → kIdle).
  while (service->state() != DaoDreamService::State::kIdle) {
    base::RunLoop().RunUntilIdle();
  }
  EXPECT_TRUE(callback_fired);

  base::RunLoop verify_loop;
  memory->GetDreamReportByDate(
      runner.last_request().period_start,
      base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ("completed", r->status);
        EXPECT_EQ("manual", r->trigger_kind);
        EXPECT_EQ("# nightly report", r->report_markdown);
        verify_loop.Quit();
      }));
  verify_loop.Run();

  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       ManualDreamFailureReportsRunnerError) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/page"), base::Time::Now(),
                   history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  FakeRunner runner;
  service->SetRunner(&runner);

  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();

  base::RunLoop callback_loop;
  bool callback_success = true;
  std::string callback_error;
  service->StartManualDream(base::BindLambdaForTesting(
      [&](bool ok, const std::string& error) {
        callback_success = ok;
        callback_error = error;
        callback_loop.Quit();
      }));
  runner_loop.Run();

  ASSERT_TRUE(runner.ran);
  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider",
                                        "API Error: invalid key"});
  callback_loop.Run();

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("API Error: invalid key", callback_error);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       TimeoutRejectsLateCompletionWhileSavingFailure) {
  Profile* profile = browser()->profile();
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://timeout-race.test/page"),
                   LocalTime(2026, 6, 12, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 23, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();
  base::RunLoop callback_loop;
  int callback_count = 0;
  bool callback_success = true;
  std::string callback_error;
  service->StartManualDreamForDate(
      "2026-06-12",
      base::BindLambdaForTesting([&](bool success, const std::string& error) {
        ++callback_count;
        callback_success = success;
        callback_error = error;
        callback_loop.Quit();
      }));
  runner_loop.Run();
  const DaoDreamService::DreamRunRequest request = runner.last_request();

  service->FireDreamTimeoutForTesting();
  EXPECT_EQ(DaoDreamService::State::kSaving, service->state());
  base::DictValue late_result;
  late_result.Set("report_markdown", "# Late completion must be ignored");
  service->OnDreamResult(request.request_id, std::move(late_result));
  callback_loop.Run();

  EXPECT_EQ(1, callback_count);
  EXPECT_FALSE(callback_success);
  EXPECT_EQ("timeout", callback_error);
  std::optional<DreamReport> report =
      LoadDreamReportForTest(
          DaoAgentMemoryServiceFactory::GetForProfile(profile),
          request.period_start);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ("failed", report->status);
  EXPECT_EQ(1, report->attempt_count);
  EXPECT_NE("# Late completion must be ignored", report->report_markdown);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       DuplicateFailureStartsOnlyOnePersistenceChain) {
  Profile* profile = browser()->profile();
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://duplicate-failure.test/page"),
                   LocalTime(2026, 6, 12, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 23, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();
  base::RunLoop callback_loop;
  int callback_count = 0;
  std::string callback_error;
  service->StartManualDreamForDate(
      "2026-06-12",
      base::BindLambdaForTesting([&](bool success, const std::string& error) {
        EXPECT_FALSE(success);
        ++callback_count;
        callback_error = error;
        callback_loop.Quit();
      }));
  runner_loop.Run();
  const DaoDreamService::DreamRunRequest request = runner.last_request();

  service->OnDreamFailed(
      request.request_id,
      DaoDreamService::DreamRunFailure{"provider", "first failure"});
  EXPECT_EQ(DaoDreamService::State::kSaving, service->state());
  service->OnDreamFailed(
      request.request_id,
      DaoDreamService::DreamRunFailure{"provider", "duplicate failure"});
  base::DictValue late_result;
  late_result.Set("report_markdown", "# Late result");
  service->OnDreamResult(request.request_id, std::move(late_result));
  callback_loop.Run();

  EXPECT_EQ(1, callback_count);
  EXPECT_EQ("first failure", callback_error);
  std::optional<DreamReport> report =
      LoadDreamReportForTest(
          DaoAgentMemoryServiceFactory::GetForProfile(profile),
          request.period_start);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ("failed", report->status);
  EXPECT_EQ(1, report->attempt_count);
  EXPECT_NE("# Late result", report->report_markdown);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       ConsecutiveSamePeriodRunsRejectOldRequestId) {
  Profile* profile = browser()->profile();
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://same-period.test/page"),
                   LocalTime(2026, 6, 12, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 23, 0));
  service->SetClockForTesting(&clock);
  FakeRunner runner;
  service->SetRunner(&runner);

  base::RunLoop first_runner_loop;
  runner.quit_closure = first_runner_loop.QuitClosure();
  base::RunLoop first_callback_loop;
  service->StartManualDreamForDate(
      "2026-06-12",
      base::BindLambdaForTesting([&](bool success, const std::string& error) {
        EXPECT_TRUE(success) << error;
        first_callback_loop.Quit();
      }));
  first_runner_loop.Run();
  const DaoDreamService::DreamRunRequest first_request =
      runner.last_request();
  base::DictValue first_result;
  first_result.Set("report_markdown", "# First result");
  service->OnDreamResult(first_request.request_id, std::move(first_result));
  first_callback_loop.Run();
  ASSERT_EQ(DaoDreamService::State::kIdle, service->state());

  base::RunLoop second_runner_loop;
  runner.quit_closure = second_runner_loop.QuitClosure();
  base::RunLoop second_callback_loop;
  int second_callback_count = 0;
  service->StartManualDreamForDate(
      "2026-06-12",
      base::BindLambdaForTesting([&](bool success, const std::string& error) {
        EXPECT_TRUE(success) << error;
        ++second_callback_count;
        second_callback_loop.Quit();
      }));
  second_runner_loop.Run();
  const DaoDreamService::DreamRunRequest second_request =
      runner.last_request();
  ASSERT_EQ(first_request.period_start, second_request.period_start);
  EXPECT_NE(first_request.request_id, second_request.request_id);

  base::DictValue stale_result;
  stale_result.Set("report_markdown", "# Stale result");
  service->OnDreamResult(first_request.request_id, std::move(stale_result));
  service->OnDreamFailed(
      first_request.request_id,
      DaoDreamService::DreamRunFailure{"provider", "stale failure"});
  EXPECT_EQ(DaoDreamService::State::kDreaming, service->state());
  EXPECT_EQ(0, second_callback_count);

  base::DictValue second_result;
  second_result.Set("report_markdown", "# Second result");
  service->OnDreamResult(second_request.request_id, std::move(second_result));
  second_callback_loop.Run();
  EXPECT_EQ(1, second_callback_count);

  std::optional<DreamReport> report =
      LoadDreamReportForTest(
          DaoAgentMemoryServiceFactory::GetForProfile(profile),
          second_request.period_start);
  ASSERT_TRUE(report.has_value());
  EXPECT_EQ("completed", report->status);
  EXPECT_EQ("# Second result", report->report_markdown);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, ManualDreamRunsSpecifiedDate) {
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/june-10"),
                   LocalTime(2026, 6, 10, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();
  base::RunLoop callback_loop;
  bool callback_success = true;
  std::string callback_error;

  service->StartManualDreamForDate(
      "2026-06-10",
      base::BindLambdaForTesting([&](bool ok, const std::string& error) {
        callback_success = ok;
        callback_error = error;
        callback_loop.Quit();
      }));
  runner_loop.Run();

  EXPECT_TRUE(runner.ran);
  EXPECT_EQ("2026-06-10", runner.last_request().period_start);

  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "stop test run"});
  callback_loop.Run();
  EXPECT_FALSE(callback_success);
  EXPECT_EQ("stop test run", callback_error);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest, ManualDreamRejectsFutureDate) {
  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 12, 0));
  service->SetClockForTesting(&clock);

  bool callback_success = true;
  std::string callback_error;
  service->StartManualDreamForDate(
      "2026-06-13",
      base::BindLambdaForTesting([&](bool ok, const std::string& error) {
        callback_success = ok;
        callback_error = error;
      }));

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("dream date cannot be in the future", callback_error);
  EXPECT_EQ(DaoDreamService::State::kIdle, service->state());
}

IN_PROC_BROWSER_TEST_F(DaoDreamBrowserTest,
                       FailedManualRerunPreservesCompletedReport) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  DreamReport existing;
  existing.dream_date = "2026-06-10";
  existing.report_markdown = "# old report";
  existing.habit_candidates = "[]";
  existing.material_stats = "{}";
  existing.status = "completed";
  existing.trigger_kind = "nightly";
  {
    base::RunLoop loop;
    memory->SaveDreamReport(
        existing, base::BindLambdaForTesting([&](bool ok) {
          ASSERT_TRUE(ok);
          loop.Quit();
        }));
    loop.Run();
  }

  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      browser()->profile(), ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);
  history->AddPage(GURL("https://example.com/june-10"),
                   LocalTime(2026, 6, 10, 12, 0), history::SOURCE_BROWSED);

  DaoDreamService* service = dream_service();
  ASSERT_TRUE(service);
  base::SimpleTestClock clock;
  clock.SetNow(LocalTime(2026, 6, 12, 12, 0));
  service->SetClockForTesting(&clock);

  FakeRunner runner;
  service->SetRunner(&runner);
  base::RunLoop runner_loop;
  runner.quit_closure = runner_loop.QuitClosure();
  base::RunLoop callback_loop;

  service->StartManualDreamForDate(
      "2026-06-10",
      base::BindLambdaForTesting([&](bool ok, const std::string& error) {
        EXPECT_FALSE(ok);
        EXPECT_EQ("rerun failed", error);
        callback_loop.Quit();
      }));
  runner_loop.Run();
  service->OnDreamFailed(
      runner.last_request().request_id,
      DaoDreamService::DreamRunFailure{"provider", "rerun failed"});
  callback_loop.Run();

  std::optional<DreamReport> got;
  base::RunLoop verify_loop;
  memory->GetDreamReportByDate(
      "2026-06-10",
      base::BindLambdaForTesting([&](std::optional<DreamReport> r) {
        got = std::move(r);
        verify_loop.Quit();
      }));
  verify_loop.Run();
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ("completed", got->status);
  EXPECT_EQ("# old report", got->report_markdown);
  service->ClearRunner(&runner);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyDreamWebUiSerializesLockedShapeAndOmitsInvalidContent) {
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(memory);

  WeeklyDreamReport valid_report;
  valid_report.week_start = "2026-07-06";
  valid_report.week_end = "2026-07-13";
  valid_report.content_json =
      R"({"schema_version":1,"headline":"Keep shipping","primary_thread":{"title":"Native bridge"}})";
  valid_report.material_stats =
      R"({"source_count":2,"page_count":1,"conversation_count":1})";
  valid_report.status = "completed";
  valid_report.attempt_count = 1;
  valid_report.trigger_kind = "scheduled";
  valid_report.debug_material_json =
      R"({"url":"https://serializer-secret.test/private?token=SECRET"})";

  WeeklyDreamSource page_source;
  page_source.ref_id = "page_serialized";
  page_source.source_kind = "page";
  page_source.title = "Private page";
  page_source.domain = "serializer-secret.test";
  page_source.local_locator =
      "https://serializer-secret.test/private?token=SECRET";
  page_source.last_seen_at = LocalTime(2026, 7, 10, 10, 0);

  WeeklyDreamSource conversation_source;
  conversation_source.ref_id = "conversation_serialized";
  conversation_source.source_kind = "conversation";
  conversation_source.title = "Private conversation";
  conversation_source.domain = "agent";
  conversation_source.local_locator = "session-serialized-secret";
  conversation_source.last_seen_at = LocalTime(2026, 7, 11, 11, 0);

  ASSERT_TRUE(SaveWeeklyDreamReportForTest(
      memory, valid_report, {page_source, conversation_source}));
  std::optional<WeeklyDreamReport> stored_valid =
      LoadWeeklyDreamReportForTest(memory, valid_report.week_start);
  ASSERT_TRUE(stored_valid.has_value());

  WeeklyDreamReport invalid_report;
  invalid_report.week_start = "2026-07-13";
  invalid_report.week_end = "2026-07-20";
  invalid_report.content_json = "{not valid json";
  invalid_report.material_stats = R"({"source_count":0})";
  invalid_report.status = "completed";
  invalid_report.attempt_count = 1;
  invalid_report.trigger_kind = "manual";
  ASSERT_TRUE(SaveWeeklyDreamReportForTest(memory, invalid_report));

  content::WebContents* dream_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(dream_contents);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("dao://dream/")));

  const double fractional_report_id =
      static_cast<double>(stored_valid->id) + 0.5;
  for (const std::string& method : {
           "getWeeklyDreamSources",
           "openWeeklyDreamSource",
           "markWeeklyDreamReportViewed",
       }) {
    base::DictValue invalid_id_params;
    invalid_id_params.Set("reportId", fractional_report_id);
    if (method == "openWeeklyDreamSource") {
      invalid_id_params.Set("refId", page_source.ref_id);
    }
    base::DictValue invalid_id_response = CallDreamNativeForTest(
        dream_contents, method, std::move(invalid_id_params));
    ASSERT_TRUE(invalid_id_response.FindBool("callbackReceived")
                    .value_or(false));
    EXPECT_FALSE(invalid_id_response.FindBool("isSuccess").value_or(true))
        << method;
  }

  base::DictValue valid_params;
  valid_params.Set("weekStart", valid_report.week_start);
  base::DictValue valid_response = CallDreamNativeForTest(
      dream_contents, "getWeeklyDreamReport", std::move(valid_params));
  ASSERT_TRUE(
      valid_response.FindBool("callbackReceived").value_or(false));
  ASSERT_TRUE(valid_response.FindBool("isSuccess").value_or(false));
  const base::DictValue* payload = valid_response.FindDict("payload");
  ASSERT_TRUE(payload);
  EXPECT_EQ(9u, payload->size());
  const std::string* report_kind = payload->FindString("reportKind");
  ASSERT_TRUE(report_kind);
  EXPECT_EQ("weekly", *report_kind);
  ASSERT_TRUE(payload->Find("id"));
  EXPECT_EQ(static_cast<double>(stored_valid->id),
            payload->Find("id")->GetDouble());
  const std::string* week_start = payload->FindString("weekStart");
  const std::string* week_end = payload->FindString("weekEnd");
  const std::string* material_stats = payload->FindString("materialStats");
  const std::string* trigger_kind = payload->FindString("triggerKind");
  ASSERT_TRUE(week_start);
  ASSERT_TRUE(week_end);
  ASSERT_TRUE(material_stats);
  ASSERT_TRUE(trigger_kind);
  EXPECT_EQ(valid_report.week_start, *week_start);
  EXPECT_EQ(valid_report.week_end, *week_end);
  EXPECT_EQ(valid_report.material_stats, *material_stats);
  EXPECT_EQ(valid_report.trigger_kind, *trigger_kind);
  EXPECT_EQ(2, payload->FindInt("sourceCount").value_or(-1));
  ASSERT_TRUE(payload->Find("createdAt"));
  EXPECT_TRUE(payload->Find("createdAt")->is_int() ||
              payload->Find("createdAt")->is_double());
  const base::DictValue* content = payload->FindDict("content");
  ASSERT_TRUE(content);
  EXPECT_EQ(1, content->FindInt("schema_version").value_or(0));
  const std::string* headline = content->FindString("headline");
  const base::DictValue* primary_thread =
      content->FindDict("primary_thread");
  ASSERT_TRUE(headline);
  ASSERT_TRUE(primary_thread);
  const std::string* primary_title = primary_thread->FindString("title");
  ASSERT_TRUE(primary_title);
  EXPECT_EQ("Keep shipping", *headline);
  EXPECT_EQ("Native bridge", *primary_title);

  std::string serialized_payload;
  ASSERT_TRUE(base::JSONWriter::Write(valid_response, &serialized_payload));
  EXPECT_EQ(std::string::npos, serialized_payload.find("https://"));
  EXPECT_EQ(std::string::npos,
            serialized_payload.find("serializer-secret.test/private"));
  EXPECT_EQ(std::string::npos,
            serialized_payload.find("session-serialized-secret"));
  EXPECT_EQ(std::string::npos,
            serialized_payload.find("debugMaterial"));
  EXPECT_EQ(std::string::npos,
            serialized_payload.find("localLocator"));
  EXPECT_EQ(std::string::npos,
            serialized_payload.find("local_locator"));

  base::DictValue invalid_params;
  invalid_params.Set("weekStart", invalid_report.week_start);
  base::DictValue invalid_response = CallDreamNativeForTest(
      dream_contents, "getWeeklyDreamReport", std::move(invalid_params));
  ASSERT_TRUE(
      invalid_response.FindBool("callbackReceived").value_or(false));
  ASSERT_TRUE(invalid_response.FindBool("isSuccess").value_or(false));
  const base::Value* invalid_payload = invalid_response.Find("payload");
  ASSERT_TRUE(invalid_payload);
  EXPECT_TRUE(invalid_payload->is_none());

  base::DictValue list_params;
  list_params.Set("limit", 10);
  base::DictValue list_response = CallDreamNativeForTest(
      dream_contents, "getWeeklyDreamReports", std::move(list_params));
  ASSERT_TRUE(
      list_response.FindBool("callbackReceived").value_or(false));
  ASSERT_TRUE(list_response.FindBool("isSuccess").value_or(false));
  const base::ListValue* reports = list_response.FindList("payload");
  ASSERT_TRUE(reports);
  ASSERT_EQ(1u, reports->size());
  ASSERT_TRUE((*reports)[0].is_dict());
  const base::DictValue& listed_report = (*reports)[0].GetDict();
  ASSERT_TRUE(listed_report.Find("id"));
  EXPECT_EQ(static_cast<double>(stored_valid->id),
            listed_report.Find("id")->GetDouble());
  const std::string* listed_week_start =
      listed_report.FindString("weekStart");
  ASSERT_TRUE(listed_week_start);
  EXPECT_EQ(valid_report.week_start, *listed_week_start);
}

IN_PROC_BROWSER_TEST_F(
    DaoDreamBrowserTest,
    WeeklyDreamWebUiKeepsSourcesPrivateAndRejectsStaleExactPage) {
  Profile* profile = browser()->profile();
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  ASSERT_TRUE(memory);
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(history);

  const GURL exact_url(
      "https://same-title.test/private/exact?token=EXACT_SECRET");
  const GURL decoy_url(
      "https://same-title.test/private/decoy?token=DECOY_SECRET");
  AddTitledHistoryPage(history, exact_url, u"Shared weekly source",
                       LocalTime(2026, 7, 10, 10, 0));
  AddTitledHistoryPage(history, decoy_url, u"Shared weekly source",
                       LocalTime(2026, 7, 10, 11, 0));
  {
    base::RunLoop loop;
    history->FlushForTest(loop.QuitClosure());
    loop.Run();
  }
  ASSERT_TRUE(IsHistoryUrlAvailableForTest(history, exact_url));
  ASSERT_TRUE(IsHistoryUrlAvailableForTest(history, decoy_url));

  WeeklyDreamReport report;
  report.week_start = "2026-07-06";
  report.week_end = "2026-07-13";
  report.content_json =
      R"({"schema_version":1,"headline":"Inspect exact sources"})";
  report.material_stats = R"({"source_count":2})";
  report.status = "completed";
  report.attempt_count = 1;
  report.trigger_kind = "scheduled";

  WeeklyDreamSource page_source;
  page_source.ref_id = "page_exact";
  page_source.source_kind = "page";
  page_source.title = "Shared weekly source";
  page_source.domain = "same-title.test";
  page_source.local_locator = exact_url.spec();
  page_source.last_seen_at = LocalTime(2026, 7, 10, 10, 0);

  WeeklyDreamSource conversation_source;
  conversation_source.ref_id = "conversation_available";
  conversation_source.source_kind = "conversation";
  conversation_source.title = "Planning thread";
  conversation_source.domain = "agent";
  conversation_source.local_locator = "session-private-locator";
  conversation_source.last_seen_at = LocalTime(2026, 7, 11, 12, 0);

  ASSERT_TRUE(SaveWeeklyDreamReportForTest(
      memory, report, {page_source, conversation_source}));
  std::optional<WeeklyDreamReport> stored_report =
      LoadWeeklyDreamReportForTest(memory, report.week_start);
  ASSERT_TRUE(stored_report.has_value());

  content::WebContents* dream_contents =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(dream_contents);
  ASSERT_TRUE(
      ui_test_utils::NavigateToURL(browser(), GURL("dao://dream/")));

  base::DictValue available_sources_params;
  available_sources_params.Set("reportId",
                               static_cast<double>(stored_report->id));
  base::DictValue available_sources_response = CallDreamNativeForTest(
      dream_contents, "getWeeklyDreamSources",
      std::move(available_sources_params));
  ASSERT_TRUE(available_sources_response.FindBool("callbackReceived")
                  .value_or(false));
  ASSERT_TRUE(
      available_sources_response.FindBool("isSuccess").value_or(false));
  const base::ListValue* available_sources =
      available_sources_response.FindList("payload");
  ASSERT_TRUE(available_sources);
  ASSERT_EQ(2u, available_sources->size());
  const base::DictValue* available_page =
      FindWeeklySourceForTest(*available_sources, page_source.ref_id);
  const base::DictValue* available_conversation = FindWeeklySourceForTest(
      *available_sources, conversation_source.ref_id);
  ASSERT_TRUE(available_page);
  ASSERT_TRUE(available_conversation);
  EXPECT_TRUE(available_page->FindBool("available").value_or(false));
  EXPECT_TRUE(
      available_conversation->FindBool("available").value_or(false));

  history->DeleteURLs({exact_url});
  {
    base::RunLoop loop;
    history->FlushForTest(loop.QuitClosure());
    loop.Run();
  }
  ASSERT_FALSE(IsHistoryUrlAvailableForTest(history, exact_url));
  ASSERT_TRUE(IsHistoryUrlAvailableForTest(history, decoy_url));

  base::DictValue sources_params;
  sources_params.Set("reportId", static_cast<double>(stored_report->id));
  base::DictValue sources_response = CallDreamNativeForTest(
      dream_contents, "getWeeklyDreamSources", std::move(sources_params));
  ASSERT_TRUE(
      sources_response.FindBool("callbackReceived").value_or(false));
  ASSERT_TRUE(sources_response.FindBool("isSuccess").value_or(false));
  const base::ListValue* sources = sources_response.FindList("payload");
  ASSERT_TRUE(sources);
  ASSERT_EQ(2u, sources->size());
  for (const base::Value& value : *sources) {
    ASSERT_TRUE(value.is_dict());
    EXPECT_EQ(5u, value.GetDict().size());
  }

  const base::DictValue* serialized_page =
      FindWeeklySourceForTest(*sources, page_source.ref_id);
  ASSERT_TRUE(serialized_page);
  const std::string* page_kind = serialized_page->FindString("sourceKind");
  const std::string* page_title = serialized_page->FindString("title");
  const std::string* page_domain = serialized_page->FindString("domain");
  ASSERT_TRUE(page_kind);
  ASSERT_TRUE(page_title);
  ASSERT_TRUE(page_domain);
  EXPECT_EQ("page", *page_kind);
  EXPECT_EQ(page_source.title, *page_title);
  EXPECT_EQ(page_source.domain, *page_domain);
  EXPECT_FALSE(serialized_page->FindBool("available").value_or(true));

  const base::DictValue* serialized_conversation =
      FindWeeklySourceForTest(*sources, conversation_source.ref_id);
  ASSERT_TRUE(serialized_conversation);
  const std::string* conversation_kind =
      serialized_conversation->FindString("sourceKind");
  const std::string* conversation_title =
      serialized_conversation->FindString("title");
  const std::string* conversation_domain =
      serialized_conversation->FindString("domain");
  ASSERT_TRUE(conversation_kind);
  ASSERT_TRUE(conversation_title);
  ASSERT_TRUE(conversation_domain);
  EXPECT_EQ("conversation", *conversation_kind);
  EXPECT_EQ(conversation_source.title, *conversation_title);
  EXPECT_EQ(conversation_source.domain, *conversation_domain);
  EXPECT_TRUE(
      serialized_conversation->FindBool("available").value_or(false));

  std::string serialized_sources;
  ASSERT_TRUE(
      base::JSONWriter::Write(sources_response, &serialized_sources));
  EXPECT_EQ(std::string::npos, serialized_sources.find("https://"));
  EXPECT_EQ(std::string::npos, serialized_sources.find("EXACT_SECRET"));
  EXPECT_EQ(std::string::npos, serialized_sources.find("DECOY_SECRET"));
  EXPECT_EQ(std::string::npos,
            serialized_sources.find("session-private-locator"));
  EXPECT_EQ(std::string::npos,
            serialized_sources.find("localLocator"));
  EXPECT_EQ(std::string::npos,
            serialized_sources.find("local_locator"));

  const int tab_count_before_open = browser()->tab_strip_model()->count();
  base::DictValue open_params;
  open_params.Set("reportId", static_cast<double>(stored_report->id));
  open_params.Set("refId", page_source.ref_id);
  base::DictValue open_response = CallDreamNativeForTest(
      dream_contents, "openWeeklyDreamSource", std::move(open_params));
  ASSERT_TRUE(
      open_response.FindBool("callbackReceived").value_or(false));
  EXPECT_FALSE(open_response.FindBool("isSuccess").value_or(true));
  EXPECT_EQ(tab_count_before_open, browser()->tab_strip_model()->count());
  for (int index = 0; index < browser()->tab_strip_model()->count(); ++index) {
    const GURL& url = browser()
                          ->tab_strip_model()
                          ->GetWebContentsAt(index)
                          ->GetLastCommittedURL();
    EXPECT_NE(exact_url, url);
    EXPECT_NE(decoy_url, url);
  }
  const base::Value* open_error = open_response.Find("payload");
  ASSERT_TRUE(open_error);
  ASSERT_TRUE(open_error->is_string());
  EXPECT_EQ("weekly_source_unavailable", open_error->GetString());
}

}  // namespace dao
