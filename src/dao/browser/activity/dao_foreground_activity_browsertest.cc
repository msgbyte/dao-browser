// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/power_monitor/power_monitor_source.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_libc_timezone_override.h"
#include "base/test/simple_test_clock.h"
#include "base/test/simple_test_tick_clock.h"
#include "base/time/clock.h"
#include "base/time/tick_clock.h"
#include "base/time/time.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/keyed_service/core/keyed_service.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "dao/browser/activity/dao_foreground_activity_service.h"
#include "dao/browser/activity/dao_foreground_activity_service_factory.h"
#include "dao/browser/activity/dao_foreground_activity_store.h"
#include "dao/browser/agent/dao_dream_foreground_policy.h"
#include "dao/browser/ui/views/split/dao_split_pane_view.h"
#include "dao/browser/ui/views/split/dao_split_view.h"
#include "net/base/filename_util.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace dao {
namespace {

base::Time LocalTime(int year, int month, int day, int hour, int minute) {
  base::Time::Exploded exploded = {};
  exploded.year = year;
  exploded.month = month;
  exploded.day_of_month = day;
  exploded.hour = hour;
  exploded.minute = minute;
  base::Time time;
  CHECK(base::Time::FromLocalExploded(exploded, &time));
  return time;
}

std::string LocalDate(base::Time time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return base::StringPrintf("%04d-%02d-%02d", exploded.year, exploded.month,
                            exploded.day_of_month);
}

int64_t Milliseconds(base::TimeDelta duration) {
  return duration.InMilliseconds();
}

class TestPowerMonitorSource : public base::PowerMonitorSource {
 public:
  static void Suspend() { ProcessPowerEvent(SUSPEND_EVENT); }
  static void Resume() { ProcessPowerEvent(RESUME_EVENT); }

  base::PowerStateObserver::BatteryPowerStatus GetBatteryPowerStatus()
      const override {
    return base::PowerStateObserver::BatteryPowerStatus::kUnknown;
  }
};

std::unique_ptr<KeyedService> BuildForegroundActivityService(
    bool* was_created_eagerly,
    base::Clock* clock,
    const base::TickClock* tick_clock,
    content::BrowserContext* context) {
  *was_created_eagerly = true;
  return std::make_unique<DaoForegroundActivityService>(
      Profile::FromBrowserContext(context), clock, tick_clock);
}

DaoSplitPaneView* FindPaneForContents(views::View* view,
                                      content::WebContents* contents) {
  if (auto* pane = views::AsViewClass<DaoSplitPaneView>(view);
      pane && pane->web_contents() == contents) {
    return pane;
  }
  for (views::View* child : view->children()) {
    if (DaoSplitPaneView* pane = FindPaneForContents(child, contents)) {
      return pane;
    }
  }
  return nullptr;
}

std::map<std::string, int64_t> ForegroundMsByHost(
    const DaoForegroundActivitySnapshot& snapshot) {
  std::map<std::string, int64_t> totals;
  for (const DaoForegroundActivityRow& row : snapshot.rows) {
    totals[row.host] += row.foreground_ms;
  }
  return totals;
}

TEST(DaoForegroundActivityStoreTest, UpsertsQueriesAndPrunes371Dates) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  base::ScopedTempDir profile_dir;
  ASSERT_TRUE(profile_dir.CreateUniqueTempDir());

  const base::Time tracking_started_at = LocalTime(2026, 8, 1, 9, 0);
  const base::Time later_initialization = LocalTime(2026, 8, 2, 9, 0);
  const base::Time today_time = LocalTime(2026, 8, 31, 12, 0);
  const std::string today = LocalDate(today_time);
  const std::string retained_from = LocalDate(today_time - base::Days(370));
  const std::string pruned_date = LocalDate(today_time - base::Days(371));
  const std::string started_retained_from =
      LocalDate(tracking_started_at - base::Days(370));
  const std::string reopened_retained_from =
      LocalDate(later_initialization - base::Days(370));

  DaoForegroundActivityStore store(profile_dir.GetPath());
  ASSERT_TRUE(store.Initialize().has_value());
  const auto started = store.StartTracking(tracking_started_at);
  ASSERT_TRUE(started.has_value());
  EXPECT_TRUE(started->available);
  EXPECT_EQ(tracking_started_at, started->tracking_started_at);
  EXPECT_EQ(started_retained_from, started->retained_from_date);

  std::vector<DaoForegroundActivityDelta> deltas;
  for (int day_offset = 0; day_offset <= 371; ++day_offset) {
    deltas.push_back({LocalDate(today_time - base::Days(day_offset)),
                      DaoForegroundActivityBucket::kMorning, "retained.test",
                      1});
  }
  deltas.push_back(
      {today, DaoForegroundActivityBucket::kMorning, "retained.test", 4});
  deltas.push_back(
      {today, DaoForegroundActivityBucket::kEvening, "alpha.test", 7});

  auto initial = store.ApplyDeltasAndQuery(pruned_date, today, today, deltas);
  ASSERT_TRUE(initial.has_value());
  EXPECT_EQ(tracking_started_at, initial->tracking_started_at);
  EXPECT_EQ(retained_from, initial->retained_from_date);
  ASSERT_EQ(372u, initial->rows.size());
  EXPECT_EQ(retained_from, initial->rows.front().local_date);
  EXPECT_EQ(today, initial->rows.back().local_date);
  EXPECT_EQ(today, initial->rows[initial->rows.size() - 2].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kMorning,
            initial->rows[initial->rows.size() - 2].bucket);
  EXPECT_EQ("retained.test", initial->rows[initial->rows.size() - 2].host);
  EXPECT_EQ(5, initial->rows[initial->rows.size() - 2].foreground_ms);
  EXPECT_EQ("alpha.test", initial->rows.back().host);
  EXPECT_EQ(7, initial->rows.back().foreground_ms);

  DaoForegroundActivityStore reopened_store(profile_dir.GetPath());
  ASSERT_TRUE(reopened_store.Initialize().has_value());
  const auto reopened_metadata =
      reopened_store.StartTracking(later_initialization);
  ASSERT_TRUE(reopened_metadata.has_value());
  EXPECT_TRUE(reopened_metadata->available);
  EXPECT_EQ(tracking_started_at, reopened_metadata->tracking_started_at);
  EXPECT_EQ(reopened_retained_from, reopened_metadata->retained_from_date);
  auto reopened = reopened_store.ApplyDeltasAndQuery(pruned_date, today, today,
                                                     /*deltas=*/{});
  ASSERT_TRUE(reopened.has_value());
  EXPECT_EQ(tracking_started_at, reopened->tracking_started_at);
  EXPECT_EQ(retained_from, reopened->retained_from_date);
  EXPECT_EQ(372u, reopened->rows.size());

  auto invalid_date = reopened_store.ApplyDeltasAndQuery(
      today, today, today,
      {{"not-a-local-date", DaoForegroundActivityBucket::kMorning,
        "retained.test", 1}});
  EXPECT_FALSE(invalid_date.has_value());
  auto partially_invalid_batch = reopened_store.ApplyDeltasAndQuery(
      today, today, today,
      {{today, DaoForegroundActivityBucket::kAfternoon, "atomic.test", 8},
       {"not-a-local-date", DaoForegroundActivityBucket::kAfternoon,
        "atomic.test", 1}});
  EXPECT_FALSE(partially_invalid_batch.has_value());
  auto after_rejected_batch =
      reopened_store.ApplyDeltasAndQuery(today, today, today, /*deltas=*/{});
  ASSERT_TRUE(after_rejected_batch.has_value());
  for (const DaoForegroundActivityRow& row : after_rejected_batch->rows) {
    EXPECT_NE("atomic.test", row.host);
  }
  auto invalid_duration = reopened_store.ApplyDeltasAndQuery(
      today, today, today,
      {{today, DaoForegroundActivityBucket::kMorning, "retained.test", -1}});
  EXPECT_FALSE(invalid_duration.has_value());
  auto invalid_bucket = reopened_store.ApplyDeltasAndQuery(
      today, today, today,
      {{today, static_cast<DaoForegroundActivityBucket>(5), "retained.test",
        1}});
  EXPECT_FALSE(invalid_bucket.has_value());

  auto maximum_duration = reopened_store.ApplyDeltasAndQuery(
      today, today, today,
      {{today, DaoForegroundActivityBucket::kAfternoon, "overflow.test",
        std::numeric_limits<int64_t>::max()}});
  ASSERT_TRUE(maximum_duration.has_value());
  auto overflow = reopened_store.ApplyDeltasAndQuery(
      today, today, today,
      {{today, DaoForegroundActivityBucket::kAfternoon, "overflow.test", 1}});
  EXPECT_FALSE(overflow.has_value());
}

TEST(DaoForegroundActivityIntervalTest,
     SplitsAtLocalBucketAndMidnightBoundaries) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  const base::Time wall_start = LocalTime(2026, 1, 2, 5, 45);
  const base::TimeTicks tick_start = base::TimeTicks() + base::Hours(10);
  const base::TimeTicks tick_end =
      tick_start + base::Hours(24) + base::Minutes(30);

  const std::vector<DaoForegroundActivityDelta> slices =
      internal::SplitForegroundActivityInterval(wall_start, tick_start,
                                                tick_end, "split.test");

  const int64_t fifteen_minutes = Milliseconds(base::Minutes(15));
  const int64_t two_hours = Milliseconds(base::Hours(2));
  const int64_t six_hours = Milliseconds(base::Hours(6));
  ASSERT_EQ(7u, slices.size());
  EXPECT_EQ("2026-01-02", slices[0].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kNightBeforeMorning, slices[0].bucket);
  EXPECT_EQ(fifteen_minutes, slices[0].foreground_ms);
  EXPECT_EQ("2026-01-02", slices[1].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kMorning, slices[1].bucket);
  EXPECT_EQ(six_hours, slices[1].foreground_ms);
  EXPECT_EQ("2026-01-02", slices[2].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kAfternoon, slices[2].bucket);
  EXPECT_EQ(six_hours, slices[2].foreground_ms);
  EXPECT_EQ("2026-01-02", slices[3].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kEvening, slices[3].bucket);
  EXPECT_EQ(two_hours * 2, slices[3].foreground_ms);
  EXPECT_EQ("2026-01-02", slices[4].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kNightAfterEvening, slices[4].bucket);
  EXPECT_EQ(two_hours, slices[4].foreground_ms);
  EXPECT_EQ("2026-01-03", slices[5].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kNightBeforeMorning, slices[5].bucket);
  EXPECT_EQ(six_hours, slices[5].foreground_ms);
  EXPECT_EQ("2026-01-03", slices[6].local_date);
  EXPECT_EQ(DaoForegroundActivityBucket::kMorning, slices[6].bucket);
  EXPECT_EQ(fifteen_minutes, slices[6].foreground_ms);

  int64_t split_total = 0;
  for (const DaoForegroundActivityDelta& slice : slices) {
    EXPECT_EQ("split.test", slice.host);
    split_total += slice.foreground_ms;
  }
  EXPECT_EQ(Milliseconds(tick_end - tick_start), split_total);
}

TEST(DaoForegroundActivityIntervalTest,
     ConservesElapsedAcrossSpringDstTransition) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  const base::Time wall_start = LocalTime(2026, 3, 8, 0, 30);
  const base::TimeTicks tick_start = base::TimeTicks() + base::Hours(10);
  const base::TimeTicks tick_end = tick_start + base::Hours(6);

  const std::vector<DaoForegroundActivityDelta> slices =
      internal::SplitForegroundActivityInterval(wall_start, tick_start,
                                                tick_end, "spring.test");

  ASSERT_EQ(2u, slices.size());
  EXPECT_EQ(DaoForegroundActivityBucket::kNightBeforeMorning, slices[0].bucket);
  EXPECT_EQ(Milliseconds(base::Hours(4) + base::Minutes(30)),
            slices[0].foreground_ms);
  EXPECT_EQ(DaoForegroundActivityBucket::kMorning, slices[1].bucket);
  EXPECT_EQ(Milliseconds(base::Hours(1) + base::Minutes(30)),
            slices[1].foreground_ms);
  EXPECT_EQ(Milliseconds(tick_end - tick_start),
            slices[0].foreground_ms + slices[1].foreground_ms);
}

TEST(DaoForegroundActivityIntervalTest,
     ConservesElapsedAcrossFallDstTransition) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  const base::Time wall_start = LocalTime(2026, 11, 1, 0, 30);
  const base::TimeTicks tick_start = base::TimeTicks() + base::Hours(10);
  const base::TimeTicks tick_end = tick_start + base::Hours(7);

  const std::vector<DaoForegroundActivityDelta> slices =
      internal::SplitForegroundActivityInterval(wall_start, tick_start,
                                                tick_end, "fall.test");

  ASSERT_EQ(2u, slices.size());
  EXPECT_EQ(DaoForegroundActivityBucket::kNightBeforeMorning, slices[0].bucket);
  EXPECT_EQ(Milliseconds(base::Hours(6) + base::Minutes(30)),
            slices[0].foreground_ms);
  EXPECT_EQ(DaoForegroundActivityBucket::kMorning, slices[1].bucket);
  EXPECT_EQ(Milliseconds(base::Minutes(30)), slices[1].foreground_ms);
  EXPECT_EQ(Milliseconds(tick_end - tick_start),
            slices[0].foreground_ms + slices[1].foreground_ms);
}

TEST(DaoForegroundActivityUrlTest, RejectsEmptyAndInvalidUrls) {
  EXPECT_FALSE(internal::IsEligibleForegroundActivityUrl(GURL()));
  EXPECT_FALSE(internal::IsEligibleForegroundActivityUrl(GURL("http://")));
}

TEST(DaoForegroundActivityDreamPolicyTest, SelectsExactlyOneSourcePerDate) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  DaoForegroundActivitySnapshot snapshot;
  snapshot.available = true;
  snapshot.tracking_started_at = LocalTime(2026, 8, 26, 10, 0);
  snapshot.retained_from_date = "2025-08-26";
  const base::Time now = LocalTime(2026, 8, 31, 15, 0);

  const DreamForegroundDatePolicy legacy =
      ResolveDreamForegroundDatePolicy("2026-08-25", snapshot, now);
  EXPECT_EQ(DreamForegroundSource::kChromiumHistoryLegacy, legacy.source);
  EXPECT_EQ(DreamForegroundCoverage::kLegacy, legacy.coverage);
  EXPECT_FALSE(legacy.use_native);
  EXPECT_TRUE(legacy.use_legacy);

  const DreamForegroundDatePolicy start =
      ResolveDreamForegroundDatePolicy("2026-08-26", snapshot, now);
  EXPECT_EQ(DreamForegroundSource::kDaoActiveTabV1, start.source);
  EXPECT_EQ(DreamForegroundCoverage::kPartial, start.coverage);
  EXPECT_TRUE(start.use_native);
  EXPECT_FALSE(start.use_legacy);

  const DreamForegroundDatePolicy completed =
      ResolveDreamForegroundDatePolicy("2026-08-30", snapshot, now);
  EXPECT_EQ(DreamForegroundSource::kDaoActiveTabV1, completed.source);
  EXPECT_EQ(DreamForegroundCoverage::kFull, completed.coverage);
  EXPECT_TRUE(completed.use_native);
  EXPECT_FALSE(completed.use_legacy);

  const DreamForegroundDatePolicy current =
      ResolveDreamForegroundDatePolicy("2026-08-31", snapshot, now);
  EXPECT_EQ(DreamForegroundSource::kDaoActiveTabV1, current.source);
  EXPECT_EQ(DreamForegroundCoverage::kPartial, current.coverage);
  EXPECT_TRUE(current.use_native);
  EXPECT_FALSE(current.use_legacy);

  DaoForegroundActivitySnapshot unavailable = snapshot;
  unavailable.available = false;
  const DreamForegroundDatePolicy unavailable_policy =
      ResolveDreamForegroundDatePolicy("2026-08-27", unavailable, now);
  EXPECT_EQ(DreamForegroundSource::kDaoActiveTabV1, unavailable_policy.source);
  EXPECT_EQ(DreamForegroundCoverage::kUnavailable, unavailable_policy.coverage);
  EXPECT_FALSE(unavailable_policy.use_native);
  EXPECT_FALSE(unavailable_policy.use_legacy);

  const DreamForegroundRangePolicy mixed = ResolveDreamForegroundRangePolicy(
      "2026-08-23", "2026-08-29", snapshot, now);
  EXPECT_EQ(DreamForegroundSource::kMixed, mixed.source);
  EXPECT_EQ(DreamForegroundCoverage::kMixed, mixed.coverage);

  const DreamForegroundRangePolicy mixed_unavailable =
      ResolveDreamForegroundRangePolicy("2026-08-25", "2026-08-27", unavailable,
                                        now);
  EXPECT_EQ(DreamForegroundSource::kMixed, mixed_unavailable.source);
  EXPECT_EQ(DreamForegroundCoverage::kUnavailable, mixed_unavailable.coverage);
  EXPECT_EQ(0, mixed_unavailable.coverage_seconds);
}

TEST(DaoForegroundActivityDreamPolicyTest,
     FiltersHostsBeforeRecomputingTotals) {
  std::vector<DaoForegroundActivityRow> rows = {
      {"2026-08-30", DaoForegroundActivityBucket::kMorning, "allowed.test",
       Milliseconds(base::Seconds(3))},
      {"2026-08-30", DaoForegroundActivityBucket::kMorning, "sub.example.com",
       Milliseconds(base::Seconds(100))},
      {"2026-08-30", DaoForegroundActivityBucket::kMorning, "allowed.test",
       Milliseconds(base::Seconds(2))},
      {"2026-08-30", DaoForegroundActivityBucket::kEvening, "notexample.com",
       Milliseconds(base::Seconds(6))},
  };
  const std::set<std::string> excluded_domains = {"example.com"};

  const std::optional<DreamForegroundActivitySummary> summary =
      SummarizeDreamForegroundActivity(rows, excluded_domains);

  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(Milliseconds(base::Seconds(11)), summary->total_foreground_ms);
  ASSERT_EQ(2u, summary->foreground_ms_by_host.size());
  EXPECT_EQ(Milliseconds(base::Seconds(5)),
            summary->foreground_ms_by_host.at("allowed.test"));
  EXPECT_EQ(Milliseconds(base::Seconds(6)),
            summary->foreground_ms_by_host.at("notexample.com"));
  EXPECT_EQ(summary->foreground_ms_by_host.end(),
            summary->foreground_ms_by_host.find("sub.example.com"));
  EXPECT_EQ(Milliseconds(base::Seconds(5)),
            summary->foreground_ms_by_bucket[static_cast<size_t>(
                DaoForegroundActivityBucket::kMorning)]);
  EXPECT_EQ(Milliseconds(base::Seconds(6)),
            summary->foreground_ms_by_bucket[static_cast<size_t>(
                DaoForegroundActivityBucket::kEvening)]);
}

TEST(DaoForegroundActivityDreamPolicyTest,
     ExactSixAmWindowSelectsOnlyIntersectingNightHalves) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  const base::Time start = LocalTime(2026, 8, 30, 6, 0);
  const base::Time end = LocalTime(2026, 8, 31, 6, 0);
  EXPECT_FALSE(IsDreamForegroundRowInWindow(
      {"2026-08-30", DaoForegroundActivityBucket::kNightBeforeMorning,
       "early.test", 1},
      start, end, end));
  EXPECT_TRUE(IsDreamForegroundRowInWindow(
      {"2026-08-30", DaoForegroundActivityBucket::kNightAfterEvening,
       "late.test", 1},
      start, end, end));
  EXPECT_TRUE(IsDreamForegroundRowInWindow(
      {"2026-08-31", DaoForegroundActivityBucket::kNightBeforeMorning,
       "early.test", 1},
      start, end, end));
  EXPECT_FALSE(IsDreamForegroundRowInWindow(
      {"2026-08-31", DaoForegroundActivityBucket::kNightAfterEvening,
       "late.test", 1},
      start, end, end));

  DaoForegroundActivitySnapshot snapshot;
  snapshot.available = true;
  snapshot.tracking_started_at = LocalTime(2026, 8, 29, 12, 0);
  snapshot.retained_from_date = "2025-08-30";
  const DreamForegroundRangePolicy window = ResolveDreamForegroundWindowPolicy(
      start, end, snapshot, LocalTime(2026, 8, 31, 12, 0));
  EXPECT_EQ(DreamForegroundCoverage::kFull, window.coverage);
  EXPECT_EQ(base::Hours(24).InSeconds(), window.coverage_seconds);

  snapshot.tracking_started_at = LocalTime(2026, 8, 30, 12, 0);
  const DreamForegroundRangePolicy tracking_clipped =
      ResolveDreamForegroundWindowPolicy(start, end, snapshot,
                                         LocalTime(2026, 8, 31, 12, 0));
  EXPECT_EQ(DreamForegroundCoverage::kPartial, tracking_clipped.coverage);
  EXPECT_EQ(base::Hours(18).InSeconds(), tracking_clipped.coverage_seconds);

  snapshot.tracking_started_at = LocalTime(2026, 8, 29, 12, 0);
  const DreamForegroundRangePolicy query_clipped =
      ResolveDreamForegroundWindowPolicy(start, end, snapshot,
                                         LocalTime(2026, 8, 30, 18, 0));
  EXPECT_EQ(DreamForegroundCoverage::kPartial, query_clipped.coverage);
  EXPECT_EQ(base::Hours(12).InSeconds(), query_clipped.coverage_seconds);
}

TEST(DaoForegroundActivityDreamPolicyTest,
     ExactWindowCoverageUsesDstAdjustedDuration) {
  base::test::ScopedLibcTimezoneOverride timezone("America/Los_Angeles");
  DaoForegroundActivitySnapshot snapshot;
  snapshot.available = true;
  snapshot.retained_from_date = "2025-01-01";

  const base::Time spring_start = LocalTime(2026, 3, 7, 6, 0);
  const base::Time spring_end = LocalTime(2026, 3, 8, 6, 0);
  snapshot.tracking_started_at = LocalTime(2026, 3, 6, 6, 0);
  const DreamForegroundRangePolicy spring = ResolveDreamForegroundWindowPolicy(
      spring_start, spring_end, snapshot, spring_end);
  EXPECT_EQ(DreamForegroundCoverage::kFull, spring.coverage);
  EXPECT_EQ(base::Hours(23).InSeconds(), spring.coverage_seconds);

  const base::Time fall_start = LocalTime(2026, 10, 31, 6, 0);
  const base::Time fall_end = LocalTime(2026, 11, 1, 6, 0);
  snapshot.tracking_started_at = LocalTime(2026, 10, 30, 6, 0);
  const DreamForegroundRangePolicy fall = ResolveDreamForegroundWindowPolicy(
      fall_start, fall_end, snapshot, fall_end);
  EXPECT_EQ(DreamForegroundCoverage::kFull, fall.coverage);
  EXPECT_EQ(base::Hours(25).InSeconds(), fall.coverage_seconds);
}

TEST(DaoForegroundActivityDreamPolicyTest,
     CombinesBothPersistedNightHalvesAndRejectsInvalidRows) {
  const std::vector<DaoForegroundActivityRow> rows = {
      {"2026-08-30", DaoForegroundActivityBucket::kNightBeforeMorning,
       "night.test", std::numeric_limits<int64_t>::max()},
      {"2026-08-30", DaoForegroundActivityBucket::kNightAfterEvening,
       "night.test", 1},
  };
  EXPECT_FALSE(SummarizeDreamForegroundActivity(rows, {}).has_value());

  const auto summary = SummarizeDreamForegroundActivity(
      {{"2026-08-30", DaoForegroundActivityBucket::kNightBeforeMorning,
        "night.test", 500},
       {"2026-08-30", DaoForegroundActivityBucket::kNightAfterEvening,
        "night.test", 700}},
      {});
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(1200, summary->foreground_ms_by_bucket[3]);
  EXPECT_EQ(1200,
            summary->foreground_ms_by_host_and_bucket.at("night.test")[3]);
}

class DaoForegroundActivityBrowserTest : public InProcessBrowserTest {
 public:
  DaoForegroundActivityBrowserTest()
      : https_server_(net::EmbeddedTestServer::TYPE_HTTPS) {
    clock_.SetNow(LocalTime(2026, 8, 31, 9, 0));
    tick_clock_.SetNowTicks(base::TimeTicks() + base::Hours(1));
  }
  ~DaoForegroundActivityBrowserTest() override = default;

 protected:
  void SetUpBrowserContextKeyedServices(
      content::BrowserContext* context) override {
    DaoForegroundActivityServiceFactory::GetInstance()->SetTestingFactory(
        context,
        base::BindRepeating(&BuildForegroundActivityService,
                            &service_created_eagerly_, &clock_, &tick_clock_));
    InProcessBrowserTest::SetUpBrowserContextKeyedServices(context);
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
    ASSERT_TRUE(embedded_test_server()->Start());
    https_server_.ServeFilesFromSourceDirectory("chrome/test/data");
    https_server_.SetCertHostnames({"second.test"});
    ASSERT_TRUE(https_server_.Start());
    ASSERT_TRUE(service_created_eagerly_);
    service_ = DaoForegroundActivityServiceFactory::GetForProfile(
        browser()->profile());
    ASSERT_TRUE(service_);
  }

  void TearDownOnMainThread() override {
    service_ = nullptr;
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void Advance(base::TimeDelta duration) {
    clock_.Advance(duration);
    tick_clock_.Advance(duration);
  }

  DaoForegroundActivitySnapshot Snapshot() {
    std::optional<DaoForegroundActivitySnapshot> snapshot;
    base::RunLoop loop;
    const std::string local_date = LocalDate(clock_.Now());
    service_->GetSnapshot(
        local_date, local_date,
        base::BindOnce(
            [](std::optional<DaoForegroundActivitySnapshot>* result,
               base::RunLoop* run_loop, DaoForegroundActivitySnapshot value) {
              *result = std::move(value);
              run_loop->Quit();
            },
            &snapshot, &loop));
    loop.Run();
    CHECK(snapshot.has_value());
    return std::move(*snapshot);
  }

  GURL HttpTestUrl(const std::string& host, const std::string& path) {
    return embedded_test_server()->GetURL(host, path);
  }

  GURL HttpsTestUrl(const std::string& host, const std::string& path) {
    return https_server_.GetURL(host, path);
  }

  base::test::ScopedLibcTimezoneOverride timezone_{"America/Los_Angeles"};
  base::SimpleTestClock clock_;
  base::SimpleTestTickClock tick_clock_;
  net::EmbeddedTestServer https_server_;
  bool service_created_eagerly_ = false;
  raw_ptr<DaoForegroundActivityService> service_ = nullptr;
};

IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                       CountsOnlySelectedHttpTabInForegroundWindow) {
  const GURL first_url = HttpTestUrl("first.test", "/first");
  const GURL second_url = HttpsTestUrl("second.test", "/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  Advance(base::Seconds(5));

  content::WebContents* second_contents =
      chrome::AddAndReturnTabAt(browser(), second_url, -1, /*foreground=*/true);
  ASSERT_TRUE(second_contents);
  ASSERT_TRUE(content::WaitForLoadStop(second_contents));
  DaoSplitView* split_view =
      BrowserView::GetBrowserViewForBrowser(browser())->dao_split_view();
  ASSERT_TRUE(
      split_view->SplitPane(browser()->tab_strip_model()->GetWebContentsAt(0),
                            SplitDirection::kHorizontal,
                            /*new_contents_first=*/false, second_contents));
  ASSERT_TRUE(split_view->IsSplitActive());
  Advance(base::Seconds(7));
  DaoSplitPaneView* first_pane = FindPaneForContents(
      split_view, browser()->tab_strip_model()->GetWebContentsAt(0));
  ASSERT_TRUE(first_pane);
  split_view->SetActivePane(first_pane);
  Advance(base::Seconds(11));

  Browser* background_window = CreateBrowser(browser()->profile());
  BrowserView::GetBrowserViewForBrowser(background_window)
      ->GetWidget()
      ->Activate();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(13));

  const auto totals = ForegroundMsByHost(Snapshot());
  ASSERT_EQ(2u, totals.size());
  EXPECT_EQ(Milliseconds(base::Seconds(16)), totals.at("first.test"));
  EXPECT_EQ(Milliseconds(base::Seconds(7)), totals.at("second.test"));
}

IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                       StopsForNavigationMinimizeSuspendAndClose) {
  const GURL first_url = HttpTestUrl("first.test", "/first");
  const GURL second_url = HttpTestUrl("second.test", "/second");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  Advance(base::Seconds(5));

  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           GURL(chrome::kChromeUIVersionURL)));
  Advance(base::Seconds(7));
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  const base::FilePath local_file =
      temp_dir.GetPath().AppendASCII("foreground-activity.html");
  ASSERT_TRUE(base::WriteFile(local_file, "<title>file fixture</title>"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
                                           net::FilePathToFileURL(local_file)));
  Advance(base::Seconds(13));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second_url));
  Advance(base::Seconds(11));

  views::Widget* widget =
      BrowserView::GetBrowserViewForBrowser(browser())->GetWidget();
  widget->Minimize();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(13));
  widget->Restore();
  widget->Activate();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(17));

  widget->Hide();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(23));
  widget->Show();
  widget->Activate();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(19));

  TestPowerMonitorSource::Suspend();
  base::RunLoop().RunUntilIdle();
  Advance(base::Hours(1));
  TestPowerMonitorSource::Resume();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(29));

  content::WebContents* internal_contents = chrome::AddAndReturnTabAt(
      browser(), GURL(chrome::kChromeUIVersionURL), -1, /*foreground=*/false);
  ASSERT_TRUE(internal_contents);
  ASSERT_TRUE(content::WaitForLoadStop(internal_contents));
  const int selected_index = browser()->tab_strip_model()->active_index();
  browser()->tab_strip_model()->CloseWebContentsAt(selected_index,
                                                   TabCloseTypes::CLOSE_NONE);
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(23));

  const auto totals = ForegroundMsByHost(Snapshot());
  ASSERT_EQ(2u, totals.size());
  EXPECT_EQ(Milliseconds(base::Seconds(5)), totals.at("first.test"));
  EXPECT_EQ(Milliseconds(base::Seconds(76)), totals.at("second.test"));
}

IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                       DirectWindowDestructionSelectsRemainingBrowser) {
  const GURL first_url = HttpTestUrl("first.test", "/first");
  const GURL second_url = HttpTestUrl("second.test", "/second");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  Advance(base::Seconds(5));

  Browser* second_browser = CreateBrowser(browser()->profile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second_browser, second_url));
  BrowserView::GetBrowserViewForBrowser(second_browser)
      ->GetWidget()
      ->Activate();
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(7));

  views::Widget* second_widget =
      BrowserView::GetBrowserViewForBrowser(second_browser)->GetWidget();
  static_cast<views::WidgetObserver*>(service_)->OnWidgetDestroying(
      second_widget);
  Advance(base::Seconds(13));

  CloseBrowserSynchronously(second_browser);
  base::RunLoop().RunUntilIdle();
  Advance(base::Seconds(11));

  const auto totals = ForegroundMsByHost(Snapshot());
  ASSERT_EQ(2u, totals.size());
  EXPECT_EQ(Milliseconds(base::Seconds(16)), totals.at("first.test"));
  EXPECT_EQ(Milliseconds(base::Seconds(7)), totals.at("second.test"));
}

IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                       TimeZoneChangeSettlesAndContinuesTiming) {
  const GURL url = HttpTestUrl("timezone.test", "/page");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  Advance(base::Seconds(5));

  DaoForegroundActivitySnapshot snapshot;
  {
    base::test::ScopedLibcTimezoneOverride utc_timezone("UTC");
    static_cast<device::mojom::TimeZoneMonitorClient*>(service_)
        ->OnTimeZoneChange("UTC");
    Advance(base::Seconds(7));
    snapshot = Snapshot();
  }

  int64_t morning_ms = 0;
  int64_t afternoon_ms = 0;
  for (const DaoForegroundActivityRow& row : snapshot.rows) {
    if (row.host != "timezone.test") {
      continue;
    }
    if (row.bucket == DaoForegroundActivityBucket::kMorning) {
      morning_ms += row.foreground_ms;
    } else if (row.bucket == DaoForegroundActivityBucket::kAfternoon) {
      afternoon_ms += row.foreground_ms;
    }
  }
  EXPECT_EQ(0, morning_ms);
  EXPECT_EQ(Milliseconds(base::Seconds(12)), afternoon_ms);
}

IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                       SnapshotCheckpointContinuesWithoutDoubleCount) {
  ASSERT_TRUE(Snapshot().available);
  const GURL url = HttpTestUrl("checkpoint.test", "/page");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  Advance(base::Seconds(5));

  service_->CheckpointForTesting();
  base::RunLoop().RunUntilIdle();

  Advance(base::Seconds(7));
  const auto second_totals = ForegroundMsByHost(Snapshot());
  ASSERT_EQ(1u, second_totals.size());
  EXPECT_EQ(Milliseconds(base::Seconds(12)),
            second_totals.at("checkpoint.test"));

  const auto repeated_totals = ForegroundMsByHost(Snapshot());
  ASSERT_EQ(1u, repeated_totals.size());
  EXPECT_EQ(Milliseconds(base::Seconds(12)),
            repeated_totals.at("checkpoint.test"));
}

IN_PROC_BROWSER_TEST_F(DaoForegroundActivityBrowserTest,
                       ShutdownCompletesAcceptedSnapshotExactlyOnce) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), HttpTestUrl("shutdown.test", "/page")));
  Advance(base::Seconds(5));
  ASSERT_TRUE(Snapshot().available);

  int callback_count = 0;
  std::optional<DaoForegroundActivitySnapshot> snapshot;
  const std::string local_date = LocalDate(clock_.Now());
  service_->GetSnapshot(
      local_date, local_date,
      base::BindOnce(
          [](int* callback_count,
             std::optional<DaoForegroundActivitySnapshot>* snapshot,
             DaoForegroundActivitySnapshot value) {
            ++*callback_count;
            *snapshot = std::move(value);
          },
          &callback_count, &snapshot));

  service_->Shutdown();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, callback_count);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_FALSE(snapshot->available);
}

}  // namespace
}  // namespace dao
