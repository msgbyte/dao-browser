// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_local_agent_integration.h"

#include <string_view>

#include "base/apple/bundle_locations.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "dao/browser/cli/dao_browser_entry_skill.h"
#include "dao/browser/cli/dao_lite_entry_skill.h"

namespace dao {
namespace {

// Lines Dao writes into files it owns. Removing them hands a file to the user.
constexpr char kSkillMarker[] = "<!-- dao-browser: managed by Dao Browser -->";
constexpr std::string_view kHelperSuffix = "/Contents/Helpers/dao-browser";

bool IsDaoHelperLink(const base::FilePath& link) {
  base::FilePath target;
  return base::ReadSymbolicLink(link, &target) &&
         base::EndsWith(target.value(), kHelperSuffix);
}

void UpdateHelperLink(const base::FilePath& home,
                      const base::FilePath& helper_path,
                      bool enabled) {
  const base::FilePath link =
      home.Append(".local").Append("bin").Append("dao-browser");
  const bool exists = base::PathExists(link) || base::IsLink(link);
  if (!enabled) {
    if (exists && IsDaoHelperLink(link)) {
      base::DeleteFile(link);
    }
    return;
  }
  if (exists) {
    base::FilePath target;
    if (!IsDaoHelperLink(link) ||
        (base::ReadSymbolicLink(link, &target) && target == helper_path)) {
      return;
    }
    base::DeleteFile(link);
  }
  if (!base::CreateDirectory(link.DirName()) ||
      !base::CreateSymbolicLink(helper_path, link)) {
    LOG(WARNING) << "Could not link dao-browser into ~/.local/bin";
  }
}

// Installs and status reads run in order, so the settings page sees the
// result of the switch it just changed.
scoped_refptr<base::SequencedTaskRunner> GetIntegrationTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN}));
  return *runner;
}

base::FilePath GetHelperPath() {
  return base::apple::OuterBundlePath()
      .Append("Contents")
      .Append("Helpers")
      .Append("dao-browser");
}

bool IsDaoManagedFile(const base::FilePath& path) {
  std::string contents;
  return base::ReadFileToString(path, &contents) &&
         contents.find(kSkillMarker) != std::string::npos;
}

void UpdateSkill(const base::FilePath& skills_dir,
                 std::string_view name,
                 const std::string& contents,
                 bool enabled) {
  const base::FilePath skill_dir = skills_dir.Append(name);
  const base::FilePath skill = skill_dir.Append("SKILL.md");
  const bool exists = base::PathExists(skill);
  if (base::IsLink(skill_dir) || base::IsLink(skill) ||
      (exists && !IsDaoManagedFile(skill))) {
    return;
  }
  if (!enabled) {
    if (exists) {
      base::DeleteFile(skill);
      if (base::IsDirectoryEmpty(skill_dir)) {
        base::DeleteFile(skill_dir);
      }
    }
    return;
  }
  std::string current;
  if (exists && base::ReadFileToString(skill, &current) &&
      current == contents) {
    return;
  }
  if (!base::CreateDirectory(skill_dir) || !base::WriteFile(skill, contents)) {
    LOG(WARNING) << "Could not install the dao-browser skill in " << skills_dir;
  }
}

}  // namespace

std::string BuildDaoEntrySkill(const base::FilePath& helper_path, bool lite) {
  // Mark the copy as Dao's right after the front matter, and point agents
  // without ~/.local/bin on PATH at this app's CLI.
  std::string skill = lite ? kDaoLiteEntrySkill : kDaoBrowserEntrySkill;
  const size_t front_matter_end = skill.find("\n---\n", 3);
  if (front_matter_end != std::string::npos) {
    skill.insert(front_matter_end + 5, base::StrCat({kSkillMarker, "\n"}));
  }
  return base::StrCat({skill, "\nIf `dao-browser` is not on PATH, run `",
                       helper_path.value(), "` instead.\n"});
}

void UpdateDaoLocalAgentIntegration(const base::FilePath& home,
                                    const base::FilePath& helper_path,
                                    bool enabled) {
  if (home.empty() || helper_path.empty() ||
      (enabled && !base::PathExists(helper_path))) {
    return;
  }
  UpdateHelperLink(home, helper_path, enabled);
  for (bool lite : {false, true}) {
    const char* name = lite ? "dao-lite" : "dao-browser";
    const std::string skill = BuildDaoEntrySkill(helper_path, lite);
    // Always maintain the shared folder and only existing agent folders.
    UpdateSkill(home.Append(".agents").Append("skills"), name, skill,
                enabled);
    for (const char* agent_dir : {".claude", ".codex"}) {
      const base::FilePath root = home.Append(agent_dir);
      if (base::DirectoryExists(root)) {
        UpdateSkill(root.Append("skills"), name, skill, enabled);
      }
    }
  }
}

void ScheduleDaoLocalAgentIntegrationUpdate(bool enabled) {
  // Browser tests run with --test-type; never touch the developer's home.
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("test-type")) {
    return;
  }
  base::FilePath home;
  if (!base::PathService::Get(base::DIR_HOME, &home)) {
    return;
  }
  GetIntegrationTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce(&UpdateDaoLocalAgentIntegration, home,
                                GetHelperPath(), enabled));
}

base::DictValue GetDaoLocalAgentIntegrationStatus(
    const base::FilePath& home,
    const base::FilePath& helper_path) {
  base::ListValue agents;
  auto add = [&](std::string_view id, const base::FilePath& root,
                 bool always_present) {
    bool installed = true;
    for (const char* name : {"dao-browser", "dao-lite"}) {
      const base::FilePath skill =
          root.Append("skills").Append(name).Append("SKILL.md");
      installed &= base::PathExists(skill) && IsDaoManagedFile(skill);
    }
    agents.Append(
        base::DictValue()
            .Set("id", id)
            .Set("detected", always_present || base::DirectoryExists(root))
            .Set("installed", installed));
  };
  add("claude", home.Append(".claude"), false);
  add("codex", home.Append(".codex"), false);
  add("shared", home.Append(".agents"), true);
  const base::FilePath link =
      home.Append(".local").Append("bin").Append("dao-browser");
  base::FilePath target;
  const bool linked = base::ReadSymbolicLink(link, &target) &&
                      base::EndsWith(target.value(), kHelperSuffix);
  return base::DictValue()
      .Set("agents", std::move(agents))
      .Set("cliLinked", linked);
}

void RequestDaoLocalAgentIntegrationStatus(
    base::OnceCallback<void(base::DictValue)> callback) {
  base::FilePath home;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("test-type") ||
      !base::PathService::Get(base::DIR_HOME, &home)) {
    std::move(callback).Run(base::DictValue()
                                .Set("agents", base::ListValue())
                                .Set("cliLinked", false));
    return;
  }
  GetIntegrationTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&GetDaoLocalAgentIntegrationStatus, home, GetHelperPath()),
      std::move(callback));
}

}  // namespace dao
