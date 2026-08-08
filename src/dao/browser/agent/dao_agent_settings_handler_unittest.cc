// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_settings_handler.h"

#include <limits>
#include <string>

#include "base/time/time.h"
#include "base/values.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/prefs/testing_pref_service.h"
#include "dao/browser/dao_pref_names.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

class DaoAgentSettingsHandlerTest : public testing::Test {
protected:
  void SetUp() override {
    prefs_.registry()->RegisterDictionaryPref(prefs::kDaoAgentSettings);
    prefs_.registry()->RegisterIntegerPref(
        prefs::kDaoAgentSettingsMigrationVersion, 0);
    prefs_.registry()->RegisterDictionaryPref(prefs::kDaoAgentUsageStats);
    prefs_.registry()->RegisterBooleanPref(prefs::kDaoAgentMemoryEnabled,
                                           false);
    prefs_.registry()->RegisterBooleanPref(prefs::kDaoDreamEnabled, false);
    prefs_.registry()->RegisterBooleanPref(prefs::kDaoDreamDebug, false);
    prefs_.registry()->RegisterListPref(prefs::kDaoDreamExcludedDomains);
  }

  TestingPrefServiceSimple prefs_;
};

TEST_F(DaoAgentSettingsHandlerTest, UsageStatsDefaultToZero) {
  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(0.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_EQ(0.0, stats.FindDouble("totalTokens").value_or(-1));
  EXPECT_TRUE(stats.FindDict("toolCalls"));
}

TEST_F(DaoAgentSettingsHandlerTest, RejectsInvalidLegacyUsageStats) {
  base::Value invalid("{\"apiCalls\":-1,\"toolCalls\":{}}");
  EXPECT_FALSE(MigrateLegacyDaoAgentUsageStats(&prefs_, &invalid));
  EXPECT_EQ(
      0.0,
      BuildDaoAgentUsageStats(&prefs_).FindDouble("apiCalls").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, MigratesValidLegacyUsageStats) {
  base::Value legacy(
      R"({"apiCalls":2,"toolCalls":{"web_search":3},"promptTokens":4,"completionTokens":5,"totalTokens":9,"estimatedCost":0.15,"lastReset":1234})");

  EXPECT_TRUE(MigrateLegacyDaoAgentUsageStats(&prefs_, &legacy));

  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(2.0, stats.FindDouble("apiCalls").value_or(-1));
  ASSERT_TRUE(stats.FindDict("toolCalls"));
  EXPECT_EQ(3.0,
            stats.FindDict("toolCalls")->FindDouble("web_search").value_or(-1));
  EXPECT_EQ(4.0, stats.FindDouble("promptTokens").value_or(-1));
  EXPECT_EQ(5.0, stats.FindDouble("completionTokens").value_or(-1));
  EXPECT_EQ(9.0, stats.FindDouble("totalTokens").value_or(-1));
  EXPECT_EQ(0.15, stats.FindDouble("estimatedCost").value_or(-1));
  EXPECT_EQ(1234.0, stats.FindDouble("lastReset").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, MigratesPartialLegacyUsageStats) {
  base::Value legacy(
      R"({"apiCalls":9,"promptTokens":4,"completionTokens":5})");

  EXPECT_TRUE(MigrateLegacyDaoAgentUsageStats(&prefs_, &legacy));

  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(9.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_EQ(4.0, stats.FindDouble("promptTokens").value_or(-1));
  EXPECT_EQ(5.0, stats.FindDouble("completionTokens").value_or(-1));
  EXPECT_EQ(9.0, stats.FindDouble("totalTokens").value_or(-1));
  EXPECT_EQ(0.0, stats.FindDouble("estimatedCost").value_or(-1));
  EXPECT_TRUE(stats.FindDict("toolCalls")->empty());
}

TEST_F(DaoAgentSettingsHandlerTest,
       RejectsMalformedPartialLegacyUsageStats) {
  for (const std::string& malformed : {
           R"({"apiCalls":-1})",
           R"({"toolCalls":{"":1}})",
           R"({"promptTokens":4,"completionTokens":5,"totalTokens":8})",
           R"({"estimatedCost":1e999})",
       }) {
    base::Value legacy(malformed);
    EXPECT_FALSE(MigrateLegacyDaoAgentUsageStats(&prefs_, &legacy));
    EXPECT_TRUE(prefs_.GetDict(prefs::kDaoAgentUsageStats).empty());
  }
}

TEST_F(DaoAgentSettingsHandlerTest,
       CanonicalUsageStatsStillRequireCompleteSchema) {
  {
    ScopedDictPrefUpdate update(&prefs_, prefs::kDaoAgentUsageStats);
    update->Set("apiCalls", 9.0);
  }

  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(0.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_EQ(0.0, stats.FindDouble("totalTokens").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, ProfileUsageStatsWinAfterMigration) {
  base::Value first(
      R"({"apiCalls":9,"toolCalls":{"web_search":2},"promptTokens":10,"completionTokens":5,"totalTokens":15,"estimatedCost":0.25,"lastReset":1000})");
  ASSERT_TRUE(MigrateLegacyDaoAgentUsageStats(&prefs_, &first));
  base::Value later(
      R"({"apiCalls":99,"toolCalls":{},"promptTokens":0,"completionTokens":0,"totalTokens":0,"estimatedCost":0,"lastReset":2000})");
  EXPECT_FALSE(MigrateLegacyDaoAgentUsageStats(&prefs_, &later));
  EXPECT_EQ(
      9.0,
      BuildDaoAgentUsageStats(&prefs_).FindDouble("apiCalls").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, MigratesUsageStatsIntoSettingsSnapshot) {
  base::DictValue legacy;
  legacy.Set(
      "dao_agent_stats",
      R"({"apiCalls":1,"toolCalls":{},"promptTokens":2,"completionTokens":3,"totalTokens":5,"estimatedCost":0.1,"lastReset":1000})");

  const base::DictValue snapshot =
      MigrateLegacyDaoAgentSettings(&prefs_, legacy);

  const base::DictValue* usage_stats = snapshot.FindDict("usageStats");
  ASSERT_TRUE(usage_stats);
  EXPECT_EQ(1.0, usage_stats->FindDouble("apiCalls").value_or(-1));
  EXPECT_FALSE(snapshot.FindDict("values")->contains("usageStats"));
  EXPECT_EQ(2, snapshot.FindInt("migrationVersion").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, RejectsInvalidUsageDeltas) {
  RecordDaoAgentApiUsage(&prefs_, 1.0, 2.0, 3.0, 0.5);
  RecordDaoAgentApiUsage(&prefs_, -1.0, 0.0, 0.0, 0.0);
  RecordDaoAgentApiUsage(&prefs_, 0.0, std::numeric_limits<double>::infinity(),
                         0.0, 0.0);

  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(1.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_EQ(2.0, stats.FindDouble("promptTokens").value_or(-1));
  EXPECT_EQ(3.0, stats.FindDouble("completionTokens").value_or(-1));
  EXPECT_EQ(5.0, stats.FindDouble("totalTokens").value_or(-1));
  EXPECT_EQ(0.5, stats.FindDouble("estimatedCost").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, RejectsToolNamesOverUsageBound) {
  RecordDaoAgentToolUsage(&prefs_,
                          std::string(kMaxDaoAgentUsageToolNameBytes + 1, 'a'));
  RecordDaoAgentToolUsage(&prefs_, "");

  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_TRUE(stats.FindDict("toolCalls")->empty());
}

TEST_F(DaoAgentSettingsHandlerTest, AccumulatesAndResetsUsageStats) {
  RecordDaoAgentApiUsage(&prefs_, 1.0, 10.0, 4.0, 0.5);
  RecordDaoAgentApiUsage(&prefs_, 1.0, 2.0, 3.0, 0.25);
  RecordDaoAgentToolUsage(&prefs_, "web_search");
  RecordDaoAgentToolUsage(&prefs_, "web_search");

  base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(2.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_EQ(12.0, stats.FindDouble("promptTokens").value_or(-1));
  EXPECT_EQ(7.0, stats.FindDouble("completionTokens").value_or(-1));
  EXPECT_EQ(19.0, stats.FindDouble("totalTokens").value_or(-1));
  EXPECT_EQ(0.75, stats.FindDouble("estimatedCost").value_or(-1));
  ASSERT_TRUE(stats.FindDict("toolCalls"));
  EXPECT_EQ(2.0,
            stats.FindDict("toolCalls")->FindDouble("web_search").value_or(-1));

  ResetDaoAgentUsageStats(&prefs_,
                          base::Time::FromMillisecondsSinceUnixEpoch(987654));
  stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(0.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_TRUE(stats.FindDict("toolCalls")->empty());
  EXPECT_EQ(987654.0, stats.FindDouble("lastReset").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, ToolUsagePreservesExistingApiUsage) {
  RecordDaoAgentApiUsage(&prefs_, 3.0, 20.0, 10.0, 1.5);
  RecordDaoAgentToolUsage(&prefs_, "web_search");

  const base::DictValue stats = BuildDaoAgentUsageStats(&prefs_);
  EXPECT_EQ(3.0, stats.FindDouble("apiCalls").value_or(-1));
  EXPECT_EQ(20.0, stats.FindDouble("promptTokens").value_or(-1));
  EXPECT_EQ(10.0, stats.FindDouble("completionTokens").value_or(-1));
  EXPECT_EQ(30.0, stats.FindDouble("totalTokens").value_or(-1));
  EXPECT_EQ(1.5, stats.FindDouble("estimatedCost").value_or(-1));
  ASSERT_TRUE(stats.FindDict("toolCalls"));
  EXPECT_EQ(1.0,
            stats.FindDict("toolCalls")->FindDouble("web_search").value_or(-1));
}

TEST_F(DaoAgentSettingsHandlerTest, MigratesKnownMissingValuesOnlyOnce) {
  {
    ScopedDictPrefUpdate update(&prefs_, prefs::kDaoAgentSettings);
    update->Set("dao_agent_model", "settings-model");
  }
  base::DictValue legacy;
  legacy.Set("dao_agent_model", "legacy-model");
  legacy.Set("dao_agent_api_key", "legacy-key");
  legacy.Set("runtime_only_value", "ignored");

  base::DictValue snapshot = MigrateLegacyDaoAgentSettings(&prefs_, legacy);

  EXPECT_EQ(kDaoAgentSettingsMigrationVersion,
            snapshot.FindInt("migrationVersion").value_or(-1));
  const base::DictValue* values = snapshot.FindDict("values");
  ASSERT_TRUE(values);
  EXPECT_EQ("settings-model", *values->FindString("dao_agent_model"));
  EXPECT_EQ("legacy-key", *values->FindString("dao_agent_api_key"));
  EXPECT_FALSE(values->contains("runtime_only_value"));

  base::DictValue later_legacy;
  later_legacy.Set("dao_agent_api_key", "later-key");
  snapshot = MigrateLegacyDaoAgentSettings(&prefs_, later_legacy);
  EXPECT_EQ("legacy-key",
            *snapshot.FindDict("values")->FindString("dao_agent_api_key"));
}

TEST_F(DaoAgentSettingsHandlerTest, ValidatesNativeBooleanSettings) {
  EXPECT_TRUE(SetDaoAgentSetting(&prefs_, kDaoAgentMemoryEnabledSetting,
                                 base::Value("true")));
  EXPECT_TRUE(prefs_.GetBoolean(prefs::kDaoAgentMemoryEnabled));
  EXPECT_FALSE(SetDaoAgentSetting(&prefs_, kDaoAgentMemoryEnabledSetting,
                                  base::Value("yes")));
  EXPECT_TRUE(prefs_.GetBoolean(prefs::kDaoAgentMemoryEnabled));
}

TEST_F(DaoAgentSettingsHandlerTest, NormalizesDreamExcludedDomains) {
  EXPECT_TRUE(SetDaoAgentSetting(
      &prefs_, kDaoDreamExcludedDomainsSetting,
      base::Value(R"(["HTTPS://Example.com/path","sub.example.com"] )")));

  const base::ListValue& domains =
      prefs_.GetList(prefs::kDaoDreamExcludedDomains);
  ASSERT_EQ(2u, domains.size());
  EXPECT_EQ("example.com", domains[0].GetString());
  EXPECT_EQ("sub.example.com", domains[1].GetString());
}

}  // namespace
}  // namespace dao
