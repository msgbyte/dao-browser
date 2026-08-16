// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_experience.h"

#include <string>

#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(DaoHomeExperienceTest, ParsesBoundedStartSurfaceContract) {
  auto result = ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github","linear","bilibili"],
    "source_slots":["github","bilibili"]
  })");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ((std::vector<std::string>{"github", "linear", "bilibili"}),
            result->primary_actions);
  EXPECT_EQ((std::vector<std::string>{"github", "bilibili"}),
            result->source_slots);
}

TEST(DaoHomeExperienceTest, RejectsDuplicateIdentifiers) {
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github","github"],
    "source_slots":["github"]
  })")
                   .has_value());
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github"],
    "source_slots":["github","github"]
  })")
                   .has_value());
}

TEST(DaoHomeExperienceTest, RejectsMoreThanTwelvePrimaryActions) {
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":[
      "action-01","action-02","action-03","action-04","action-05",
      "action-06","action-07","action-08","action-09","action-10",
      "action-11","action-12","action-13"
    ],
    "source_slots":["github"]
  })")
                   .has_value());
}

TEST(DaoHomeExperienceTest, RejectsMoreThanThreeSourceSlots) {
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github"],
    "source_slots":["github","linear","bilibili","youtube"]
  })")
                   .has_value());
}

TEST(DaoHomeExperienceTest, RejectsInvalidIdentifierSyntax) {
  for (const std::string& identifier : std::vector<std::string>{
           "", "GitHub", "-github", "github.source", std::string(65, 'a')}) {
    const std::string json = R"({"kind":"start_surface","primary_actions":[")" +
                             identifier + R"("],"source_slots":["github"]})";
    EXPECT_FALSE(ParseHomeExperience(json).has_value()) << identifier;
  }
}

TEST(DaoHomeExperienceTest, RejectsUnknownTopLevelFields) {
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github"],
    "source_slots":["github"],
    "report":{"visits":42}
  })")
                   .has_value());
}

TEST(DaoHomeExperienceTest, RejectsNonStartSurfaceKind) {
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"dashboard",
    "primary_actions":["github"],
    "source_slots":["github"]
  })")
                   .has_value());
}

TEST(DaoHomeExperienceTest, RejectsMissingOrMalformedFields) {
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github"]
  })")
                   .has_value());
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":"github",
    "source_slots":["github"]
  })")
                   .has_value());
  EXPECT_FALSE(ParseHomeExperience(R"({
    "kind":"start_surface",
    "primary_actions":["github"],
    "source_slots":[7]
  })")
                   .has_value());
}

}  // namespace
}  // namespace dao
