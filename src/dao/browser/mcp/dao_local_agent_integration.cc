// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/mcp/dao_local_agent_integration.h"

#include <string_view>

#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "dao/browser/cli/dao_browser_entry_skill.h"
#include "dao/browser/cli/dao_lite_entry_skill.h"

#if BUILDFLAG(IS_MAC)
#include "base/apple/bundle_locations.h"
#endif

namespace dao {
namespace {

// Lines Dao writes into files it owns. Removing them hands a file to the user.
constexpr char kSkillMarker[] = "<!-- dao-browser: managed by Dao Browser -->";

bool IsDaoManagedFile(const base::FilePath& path,
                     std::string_view marker = kSkillMarker) {
  std::string contents;
  return !base::IsLink(path) && !base::IsLink(path.DirName()) &&
         base::ReadFileToString(path, &contents) &&
         contents.find(marker) != std::string::npos;
}

base::FilePath GetLauncherPath(const base::FilePath& home) {
  return home.AppendASCII(".local").AppendASCII("bin").AppendASCII(
#if BUILDFLAG(IS_WIN)
      "dao-browser.cmd"
#else
      "dao-browser"
#endif
  );
}

#if BUILDFLAG(IS_WIN)
constexpr char kLauncherMarker[] = "rem dao-browser: managed by Dao Browser";

std::string BuildHelperLauncher(const base::FilePath& helper_path) {
  std::string path = helper_path.AsUTF8Unsafe();
  base::ReplaceSubstringsAfterOffset(&path, 0, "%", "%%");
  // Decode UTF-8 paths without expanding exclamation marks from the parent shell.
  return base::StrCat({
      "@echo off\r\n", kLauncherMarker,
      "\r\nsetlocal DisableDelayedExpansion\r\n"
      "for /f \"tokens=2 delims=:\" %%c in ('chcp') do set \"_DAO_CODE_PAGE=%%c\"\r\n"
      "chcp 65001 >nul\r\n\"", path,
      "\" %*\r\nset \"_DAO_EXIT_CODE=%errorlevel%\"\r\n"
      "chcp %_DAO_CODE_PAGE% >nul\r\nexit /b %_DAO_EXIT_CODE%\r\n"});
}

bool IsDaoHelperLink(const base::FilePath& link) {
  return IsDaoManagedFile(link, kLauncherMarker);
}

bool IsHelperLinkCurrent(const base::FilePath& link,
                        const base::FilePath& helper_path) {
  std::string contents;
  return base::ReadFileToString(link, &contents) &&
         contents == BuildHelperLauncher(helper_path);
}
#else
constexpr std::string_view kHelperSuffix = "/Contents/Helpers/dao-browser";

bool IsDaoHelperLink(const base::FilePath& link) {
  base::FilePath target;
  return base::ReadSymbolicLink(link, &target) &&
         base::EndsWith(target.value(), kHelperSuffix);
}

bool IsHelperLinkCurrent(const base::FilePath& link,
                        const base::FilePath& helper_path) {
  base::FilePath target;
  return base::ReadSymbolicLink(link, &target) && target == helper_path;
}
#endif

bool UpdateHelperLink(const base::FilePath& home,
                      const base::FilePath& helper_path,
                      bool installed) {
  const base::FilePath link = GetLauncherPath(home);
  const bool exists = base::PathExists(link) || base::IsLink(link);
  if (!installed) {
    return !exists || !IsDaoHelperLink(link) || base::DeleteFile(link);
  }
  if (exists) {
    if (!IsDaoHelperLink(link)) {
      return false;
    }
    if (IsHelperLinkCurrent(link, helper_path)) {
      return true;
    }
  }
#if BUILDFLAG(IS_WIN)
  return !base::IsLink(link.DirName()) &&
         base::CreateDirectory(link.DirName()) &&
         base::ImportantFileWriter::WriteFileAtomically(
             link, BuildHelperLauncher(helper_path));
#else
  if (exists && !base::DeleteFile(link)) {
    return false;
  }
  return base::CreateDirectory(link.DirName()) &&
         base::CreateSymbolicLink(helper_path, link);
#endif
}

// Installs and status reads run in order, so the settings page sees the
// result of the operation it just requested.
scoped_refptr<base::SequencedTaskRunner> GetIntegrationTaskRunner() {
  static base::NoDestructor<scoped_refptr<base::SequencedTaskRunner>> runner(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT,
           base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN}));
  return *runner;
}

base::FilePath GetHelperPath() {
#if BUILDFLAG(IS_WIN)
  base::FilePath module_dir;
  if (!base::PathService::Get(base::DIR_MODULE, &module_dir)) {
    return {};
  }
  return module_dir.AppendASCII("dao-browser.exe");
#else
  return base::apple::OuterBundlePath()
      .AppendASCII("Contents")
      .AppendASCII("Helpers")
      .AppendASCII("dao-browser");
#endif
}

bool UpdateSkill(const base::FilePath& skills_dir,
                 std::string_view name,
                 const std::string& contents,
                 bool installed) {
  const base::FilePath skill_dir = skills_dir.AppendASCII(name);
  const base::FilePath skill = skill_dir.AppendASCII("SKILL.md");
  const bool exists = base::PathExists(skill) || base::IsLink(skill);
  if (base::IsLink(skill_dir) || (exists && !IsDaoManagedFile(skill))) {
    return !installed;
  }
  if (!installed) {
    if (!exists) {
      return true;
    }
    if (!base::DeleteFile(skill)) {
      return false;
    }
    return !base::DirectoryExists(skill_dir) ||
           !base::IsDirectoryEmpty(skill_dir) || base::DeleteFile(skill_dir);
  }
  std::string current;
  if (exists && base::ReadFileToString(skill, &current) && current == contents) {
    return true;
  }
  return base::CreateDirectory(skill_dir) &&
         base::ImportantFileWriter::WriteFileAtomically(skill, contents);
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
#if BUILDFLAG(IS_WIN)
  std::string path = helper_path.AsUTF8Unsafe();
  base::ReplaceSubstringsAfterOffset(&path, 0, "'", "''");
  return base::StrCat({skill,
                       "\nIf `dao-browser` is not on PATH, run `& '", path,
                       "'` in PowerShell instead.\n"});
#else
  return base::StrCat({skill, "\nIf `dao-browser` is not on PATH, run `",
                       helper_path.AsUTF8Unsafe(), "` instead.\n"});
#endif
}

bool UpdateDaoLocalAgentIntegration(const base::FilePath& home,
                                   const base::FilePath& helper_path,
                                   std::string_view id,
                                   bool installed) {
  if (home.empty() || helper_path.empty() ||
      (installed && !base::PathExists(helper_path))) {
    return false;
  }
  if (id == "cli") {
    return UpdateHelperLink(home, helper_path, installed);
  }
  if (id != "shared" && id != "claude") {
    return false;
  }
  const auto root = home.AppendASCII(id == "shared" ? ".agents/skills"
                                                  : ".claude/skills");
  bool success = true;
  for (bool lite : {false, true}) {
    success = UpdateSkill(root, lite ? "dao-lite" : "dao-browser",
                          BuildDaoEntrySkill(helper_path, lite), installed) &&
              success;
  }
  if (id == "shared" && (success || !installed)) {
    // Remove legacy copies only after both shared skills are ready.
    for (const char* name : {"dao-browser", "dao-lite"}) {
      success = UpdateSkill(home.AppendASCII(".codex/skills"), name, "", false) &&
                success;
    }
  }
  return success;
}

void RequestDaoLocalAgentIntegrationUpdate(
    std::string id,
    bool installed,
    base::OnceCallback<void(base::DictValue)> callback) {
  base::FilePath home;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("test-type") ||
      !base::PathService::Get(base::DIR_HOME, &home)) {
    std::move(callback).Run(base::DictValue()
                                .Set("agents", base::ListValue())
                                .Set("cliLinked", false)
                                .Set("cliNeedsUpdate", false)
                                .Set("success", false));
    return;
  }
  GetIntegrationTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](const base::FilePath& home, const base::FilePath& helper,
             const std::string& id, bool installed) {
            const bool success =
                UpdateDaoLocalAgentIntegration(home, helper, id, installed);
            auto status = GetDaoLocalAgentIntegrationStatus(home, helper);
            status.Set("success", success);
            return status;
          },
          home, GetHelperPath(), std::move(id), installed),
      std::move(callback));
}

base::DictValue GetDaoLocalAgentIntegrationStatus(
    const base::FilePath& home,
    const base::FilePath& helper_path) {
  base::ListValue agents;
  const bool legacy =
      IsDaoManagedFile(home.AppendASCII(".codex/skills/dao-browser/SKILL.md")) ||
      IsDaoManagedFile(home.AppendASCII(".codex/skills/dao-lite/SKILL.md"));
  auto add = [&](std::string_view id, const base::FilePath& root, bool shared) {
    bool installed = shared && legacy;
    bool complete = true;
    for (bool lite : {false, true}) {
      const auto skill = root.AppendASCII("skills")
                             .AppendASCII(lite ? "dao-lite" : "dao-browser")
                             .AppendASCII("SKILL.md");
      const bool managed = IsDaoManagedFile(skill);
      installed |= managed;
      std::string contents;
      complete &= managed && base::ReadFileToString(skill, &contents) &&
                  contents == BuildDaoEntrySkill(helper_path, lite);
    }
    agents.Append(
        base::DictValue()
            .Set("id", id)
            .Set("detected", shared || base::DirectoryExists(root))
            .Set("installed", installed)
            .Set("needsUpdate", installed && (!complete || (shared && legacy))));
  };
  add("shared", home.AppendASCII(".agents"), true);
  add("claude", home.AppendASCII(".claude"), false);
  const base::FilePath link = GetLauncherPath(home);
  const bool linked = IsDaoHelperLink(link);
  return base::DictValue()
      .Set("agents", std::move(agents))
      .Set("cliLinked", linked)
      .Set("cliNeedsUpdate", linked && !IsHelperLinkCurrent(link, helper_path));
}

void RequestDaoLocalAgentIntegrationStatus(
    base::OnceCallback<void(base::DictValue)> callback) {
  base::FilePath home;
  if (base::CommandLine::ForCurrentProcess()->HasSwitch("test-type") ||
      !base::PathService::Get(base::DIR_HOME, &home)) {
    std::move(callback).Run(base::DictValue()
                                .Set("agents", base::ListValue())
                                .Set("cliLinked", false)
                                .Set("cliNeedsUpdate", false));
    return;
  }
  GetIntegrationTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&GetDaoLocalAgentIntegrationStatus, home, GetHelperPath()),
      std::move(callback));
}

}  // namespace dao
