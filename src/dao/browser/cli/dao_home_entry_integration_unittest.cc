// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_local_agent_integration.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace dao {
namespace {

TEST(DaoHomeEntryIntegrationTest, InstallsUpdatesAndPreservesCustomHomeSkill) {
  base::ScopedTempDir temp;
  ASSERT_TRUE(temp.CreateUniqueTempDir());
  const auto home = temp.GetPath().AppendASCII("user");
  const auto helper = temp.GetPath().AppendASCII("dao-browser");
  ASSERT_TRUE(base::WriteFile(helper, "helper"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home, helper, "shared", true));
  const auto skill = home.AppendASCII(".agents/skills/dao-home/SKILL.md");
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(skill, &text));
  EXPECT_EQ(BuildDaoHomeEntrySkill(helper), text);
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home, helper, "claude", true));
  const auto claude_skill =
      home.AppendASCII(".claude/skills/dao-home/SKILL.md");
  ASSERT_TRUE(base::ReadFileToString(claude_skill, &text));
  EXPECT_EQ(BuildDaoHomeEntrySkill(helper), text);
  // Both installation choices can update their managed Home entry in place.
  ASSERT_TRUE(base::WriteFile(skill,
                              "<!-- dao-browser: managed by Dao Browser -->\n"
                              "Outdated Home skill"));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home, helper, "shared", true));
  ASSERT_TRUE(UpdateDaoLocalAgentIntegration(home, helper, "claude", true));
  ASSERT_TRUE(base::ReadFileToString(skill, &text));
  EXPECT_EQ(BuildDaoHomeEntrySkill(helper), text);
  EXPECT_TRUE(UpdateDaoLocalAgentIntegration(home, helper, "claude", false));
  EXPECT_FALSE(base::PathExists(claude_skill));
  ASSERT_TRUE(base::WriteFile(skill, "custom home skill"));
  EXPECT_FALSE(UpdateDaoLocalAgentIntegration(home, helper, "shared", true));
  EXPECT_TRUE(UpdateDaoLocalAgentIntegration(home, helper, "shared", false));
  ASSERT_TRUE(base::ReadFileToString(skill, &text));
  EXPECT_EQ("custom home skill", text);
}

}  // namespace
}  // namespace dao
