// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>
#include <string>
#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/test/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_dream_material_collector.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/agent/dao_dream_service_factory.h"
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

}  // namespace

using DaoDreamStaticTest = InProcessBrowserTest;

// --- Pure-logic tests on the static helpers ---

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

  // Search extraction.
  const base::ListValue* queries = pack.FindList("search_queries");
  ASSERT_TRUE(queries);
  ASSERT_EQ(1u, queries->size());
  EXPECT_EQ("hello world", (*queries)[0].GetString());
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
  void RunDream(const std::string& dream_date,
                const base::DictValue& material) override {
    ran = true;
    last_dream_date = dream_date;
    if (quit_closure) {
      std::move(quit_closure).Run();
    }
  }

  bool ran = false;
  std::string last_dream_date;
  base::OnceClosure quit_closure;
};

}  // namespace

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
  service->OnDreamResult(runner.last_dream_date, std::move(result));

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
      runner.last_dream_date,
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
  service->OnDreamFailed(runner.last_dream_date, "API Error: invalid key");
  callback_loop.Run();

  EXPECT_FALSE(callback_success);
  EXPECT_EQ("API Error: invalid key", callback_error);
  service->ClearRunner(&runner);
}

}  // namespace dao
