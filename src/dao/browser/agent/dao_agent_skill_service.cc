// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_skill_service.h"

#include <string>

#include "base/files/file_enumerator.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"

namespace dao {

namespace {

constexpr char kSkillsDirName[] = "DaoAgentSkills";
constexpr char kSkillFileName[] = "SKILL.md";

// Built-in skill: summary
constexpr char kSummarySkillMd[] = R"(---
name: summary
description: Summarize the current web page into key points
requiresPageContent: true
---
# Summary

## Instructions
1. The user's message already contains a `<current-webpage>` block with the page content in markdown — use it directly.
2. Analyze the content and produce a concise summary with:
   - A one-sentence overview
   - 3-7 key points as bullet items
   - Any notable quotes or data points
3. Format the summary in clean markdown.
4. Keep the summary under 300 words unless the content is highly technical.
)";

// Built-in skill: create-skill
constexpr char kCreateSkillSkillMd[] = R"(---
name: create-skill
description: Create a new custom skill interactively
---
# Create Skill

## Instructions
You are helping the user create a new Dao Agent skill. Walk them through this process:

1. **Ask what they want to automate**: "What task would you like this skill to perform?"
2. **Ask about scope**: "Should this skill work on all pages, or only specific websites? If specific, which domains?"
3. **Ask about page content**: "Does this skill need to read the current page's content to work?"
4. **Generate the SKILL.md**: Based on their answers, generate the complete SKILL.md content with proper YAML frontmatter and step-by-step instructions.
5. **Show the preview**: Display the generated SKILL.md content and ask for confirmation.
6. **Save the skill**: Once the user confirms, call the `save_skill` tool with the skill ID, complete SKILL.md content, and target host (empty string for global).

### SKILL.md Format Reference
The skill name must be lowercase with hyphens, no spaces. Example:
```yaml
---
name: my-skill-name
description: Brief description of what the skill does
hosts:
  - "*"
requiresPageContent: false
---
# Skill Title

## Instructions
1. Step one...
2. Step two...
```
)";

// Trims leading and trailing whitespace from a string.
std::string TrimString(const std::string& s) {
  std::string result;
  base::TrimWhitespaceASCII(s, base::TRIM_ALL, &result);
  return result;
}

}  // namespace

DaoAgentSkillService::DaoAgentSkillService(
    const base::FilePath& profile_path)
    : skills_path_(profile_path.AppendASCII(kSkillsDirName)),
      background_task_runner_(base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
           base::TaskShutdownBehavior::BLOCK_SHUTDOWN})) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  InitBuiltinSkills();
}

DaoAgentSkillService::~DaoAgentSkillService() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
}

void DaoAgentSkillService::Shutdown() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  weak_factory_.InvalidateWeakPtrs();
}

void DaoAgentSkillService::InitBuiltinSkills() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTask(
      FROM_HERE,
      base::BindOnce(&DaoAgentSkillService::InitBuiltinSkillsOnBackground,
                     skills_path_));
}

// static
void DaoAgentSkillService::InitBuiltinSkillsOnBackground(
    const base::FilePath& skills_path) {
  // Ensure the builtin directory exists.
  base::FilePath builtin_dir = skills_path.AppendASCII("builtin");
  if (!base::CreateDirectory(builtin_dir)) {
    LOG(ERROR) << "Failed to create builtin skills directory: "
               << builtin_dir.value();
    return;
  }

  // Write summary skill if missing.
  base::FilePath summary_dir = builtin_dir.AppendASCII("summary");
  base::FilePath summary_file = summary_dir.AppendASCII(kSkillFileName);
  if (!base::PathExists(summary_file)) {
    if (base::CreateDirectory(summary_dir)) {
      base::WriteFile(summary_file, kSummarySkillMd);
    }
  }

  // Write create-skill skill if missing.
  base::FilePath create_skill_dir = builtin_dir.AppendASCII("create-skill");
  base::FilePath create_skill_file =
      create_skill_dir.AppendASCII(kSkillFileName);
  if (!base::PathExists(create_skill_file)) {
    if (base::CreateDirectory(create_skill_dir)) {
      base::WriteFile(create_skill_file, kCreateSkillSkillMd);
    }
  }
}

void DaoAgentSkillService::GetSkillRegistry(
    base::OnceCallback<void(std::vector<SkillRegistryEntry>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentSkillService::GetSkillRegistryOnBackground,
                     skills_path_),
      std::move(callback));
}

// static
std::vector<SkillRegistryEntry>
DaoAgentSkillService::GetSkillRegistryOnBackground(
    const base::FilePath& skills_path) {
  std::vector<SkillRegistryEntry> results;

  // Scan builtin/*
  base::FilePath builtin_dir = skills_path.AppendASCII("builtin");
  ScanSkillDir(builtin_dir, "builtin", std::string(), &results);

  // Scan user/global/*
  base::FilePath user_global_dir =
      skills_path.AppendASCII("user").AppendASCII("global");
  ScanSkillDir(user_global_dir, "user", std::string(), &results);

  // Scan user/hosts/<host>/*
  base::FilePath hosts_dir =
      skills_path.AppendASCII("user").AppendASCII("hosts");
  if (base::PathExists(hosts_dir)) {
    base::FileEnumerator host_enum(hosts_dir, false,
                                   base::FileEnumerator::DIRECTORIES);
    for (base::FilePath host_path = host_enum.Next(); !host_path.empty();
         host_path = host_enum.Next()) {
      std::string host_name = host_path.BaseName().MaybeAsASCII();
      if (host_name.empty()) {
        continue;
      }
      // Scan each skill under this host directory.
      base::FileEnumerator skill_enum(host_path, false,
                                      base::FileEnumerator::DIRECTORIES);
      for (base::FilePath skill_path = skill_enum.Next(); !skill_path.empty();
           skill_path = skill_enum.Next()) {
        base::FilePath skill_file = skill_path.AppendASCII(kSkillFileName);
        if (!base::PathExists(skill_file)) {
          continue;
        }
        std::string content;
        if (!base::ReadFileToString(skill_file, &content)) {
          continue;
        }
        SkillRegistryEntry entry;
        std::string instructions;
        if (!ParseSkillMd(content, &entry, &instructions)) {
          continue;
        }
        entry.id = skill_path.BaseName().MaybeAsASCII();
        entry.source = "user";
        // Add this host to the hosts list.
        entry.hosts.push_back(host_name);
        results.push_back(std::move(entry));
      }
    }
  }

  return results;
}

// static
void DaoAgentSkillService::ScanSkillDir(
    const base::FilePath& dir,
    const std::string& source,
    const std::string& host,
    std::vector<SkillRegistryEntry>* out) {
  if (!base::PathExists(dir)) {
    return;
  }

  base::FileEnumerator enumerator(dir, false,
                                  base::FileEnumerator::DIRECTORIES);
  for (base::FilePath path = enumerator.Next(); !path.empty();
       path = enumerator.Next()) {
    base::FilePath skill_file = path.AppendASCII(kSkillFileName);
    if (!base::PathExists(skill_file)) {
      continue;
    }

    std::string content;
    if (!base::ReadFileToString(skill_file, &content)) {
      continue;
    }

    SkillRegistryEntry entry;
    std::string instructions;
    if (!ParseSkillMd(content, &entry, &instructions)) {
      continue;
    }

    entry.id = path.BaseName().MaybeAsASCII();
    entry.source = source;
    if (!host.empty()) {
      entry.hosts.push_back(host);
    }
    out->push_back(std::move(entry));
  }
}

void DaoAgentSkillService::GetSkillContent(
    const std::string& skill_id,
    base::OnceCallback<void(std::optional<SkillContent>)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentSkillService::GetSkillContentOnBackground,
                     skills_path_, skill_id),
      std::move(callback));
}

// static
std::optional<SkillContent> DaoAgentSkillService::GetSkillContentOnBackground(
    const base::FilePath& skills_path,
    const std::string& skill_id) {
  // Search in all possible locations: builtin, user/global, user/hosts/*/
  std::vector<base::FilePath> search_paths;
  search_paths.push_back(
      skills_path.AppendASCII("builtin").AppendASCII(skill_id));
  search_paths.push_back(skills_path.AppendASCII("user")
                             .AppendASCII("global")
                             .AppendASCII(skill_id));

  // Also search under all host directories.
  base::FilePath hosts_dir =
      skills_path.AppendASCII("user").AppendASCII("hosts");
  if (base::PathExists(hosts_dir)) {
    base::FileEnumerator host_enum(hosts_dir, false,
                                   base::FileEnumerator::DIRECTORIES);
    for (base::FilePath host_path = host_enum.Next(); !host_path.empty();
         host_path = host_enum.Next()) {
      search_paths.push_back(host_path.AppendASCII(skill_id));
    }
  }

  for (const auto& skill_dir : search_paths) {
    base::FilePath skill_file = skill_dir.AppendASCII(kSkillFileName);
    if (!base::PathExists(skill_file)) {
      continue;
    }

    std::string content;
    if (!base::ReadFileToString(skill_file, &content)) {
      continue;
    }

    SkillContent result;
    if (!ParseSkillMd(content, &result.metadata, &result.instructions)) {
      continue;
    }

    result.metadata.id = skill_id;

    // Determine source from path.
    if (skill_dir.value().find("/builtin/") != std::string::npos) {
      result.metadata.source = "builtin";
    } else {
      result.metadata.source = "user";
    }

    // If under hosts/<host>/, extract the host.
    std::string path_str = skill_dir.value();
    std::string hosts_prefix = hosts_dir.value() + "/";
    if (path_str.find(hosts_prefix) == 0) {
      std::string remainder = path_str.substr(hosts_prefix.size());
      size_t slash_pos = remainder.find('/');
      if (slash_pos != std::string::npos) {
        result.metadata.hosts.push_back(remainder.substr(0, slash_pos));
      }
    }

    return result;
  }

  return std::nullopt;
}

void DaoAgentSkillService::SaveUserSkill(
    const std::string& skill_id,
    const std::string& skill_md_content,
    const std::string& host,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentSkillService::SaveUserSkillOnBackground,
                     skills_path_, skill_id, skill_md_content, host),
      std::move(callback));
}

// static
bool DaoAgentSkillService::SaveUserSkillOnBackground(
    const base::FilePath& skills_path,
    const std::string& skill_id,
    const std::string& skill_md_content,
    const std::string& host) {
  base::FilePath skill_dir;
  if (host.empty()) {
    skill_dir = skills_path.AppendASCII("user")
                    .AppendASCII("global")
                    .AppendASCII(skill_id);
  } else {
    skill_dir = skills_path.AppendASCII("user")
                    .AppendASCII("hosts")
                    .AppendASCII(host)
                    .AppendASCII(skill_id);
  }

  if (!base::CreateDirectory(skill_dir)) {
    LOG(ERROR) << "Failed to create skill directory: " << skill_dir.value();
    return false;
  }

  base::FilePath skill_file = skill_dir.AppendASCII(kSkillFileName);
  return base::WriteFile(skill_file, skill_md_content);
}

void DaoAgentSkillService::DeleteUserSkill(
    const std::string& skill_id,
    base::OnceCallback<void(bool)> callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(ui_sequence_checker_);
  background_task_runner_->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(&DaoAgentSkillService::DeleteUserSkillOnBackground,
                     skills_path_, skill_id),
      std::move(callback));
}

// static
bool DaoAgentSkillService::DeleteUserSkillOnBackground(
    const base::FilePath& skills_path,
    const std::string& skill_id) {
  // Only delete from user directories, never builtin.
  std::vector<base::FilePath> search_paths;

  // Check user/global/<skill_id>
  search_paths.push_back(skills_path.AppendASCII("user")
                             .AppendASCII("global")
                             .AppendASCII(skill_id));

  // Check user/hosts/<host>/<skill_id>
  base::FilePath hosts_dir =
      skills_path.AppendASCII("user").AppendASCII("hosts");
  if (base::PathExists(hosts_dir)) {
    base::FileEnumerator host_enum(hosts_dir, false,
                                   base::FileEnumerator::DIRECTORIES);
    for (base::FilePath host_path = host_enum.Next(); !host_path.empty();
         host_path = host_enum.Next()) {
      search_paths.push_back(host_path.AppendASCII(skill_id));
    }
  }

  for (const auto& skill_dir : search_paths) {
    if (base::PathExists(skill_dir)) {
      return base::DeletePathRecursively(skill_dir);
    }
  }

  LOG(WARNING) << "Skill not found for deletion: " << skill_id;
  return false;
}

// static
bool DaoAgentSkillService::ParseSkillMd(const std::string& content,
                                        SkillRegistryEntry* entry,
                                        std::string* instructions) {
  // Find the YAML frontmatter delimiters (---).
  size_t first_delimiter = content.find("---");
  if (first_delimiter == std::string::npos) {
    return false;
  }

  size_t after_first = first_delimiter + 3;
  // Skip to the end of the first "---" line.
  size_t first_newline = content.find('\n', after_first);
  if (first_newline == std::string::npos) {
    return false;
  }

  size_t second_delimiter = content.find("\n---", first_newline);
  if (second_delimiter == std::string::npos) {
    return false;
  }

  // Extract frontmatter between the two delimiters.
  std::string frontmatter =
      content.substr(first_newline + 1,
                     second_delimiter - first_newline - 1);

  // Everything after the second "---" line is instructions.
  size_t instructions_start = content.find('\n', second_delimiter + 4);
  if (instructions_start != std::string::npos) {
    *instructions = TrimString(content.substr(instructions_start + 1));
  } else {
    *instructions = "";
  }

  // Parse frontmatter line by line.
  std::vector<std::string> lines = base::SplitString(
      frontmatter, "\n", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);

  bool in_hosts_list = false;
  for (const auto& line : lines) {
    std::string trimmed = TrimString(line);
    if (trimmed.empty()) {
      continue;
    }

    // Check if this is a YAML list item (continuation of hosts).
    if (in_hosts_list) {
      if (trimmed.size() >= 2 && trimmed[0] == '-' && trimmed[1] == ' ') {
        std::string host_value = TrimString(trimmed.substr(2));
        // Remove surrounding quotes if present.
        if (host_value.size() >= 2 &&
            ((host_value.front() == '"' && host_value.back() == '"') ||
             (host_value.front() == '\'' && host_value.back() == '\''))) {
          host_value = host_value.substr(1, host_value.size() - 2);
        }
        if (!host_value.empty()) {
          entry->hosts.push_back(host_value);
        }
        continue;
      }
      // No longer in hosts list if line doesn't start with "-".
      in_hosts_list = false;
    }

    // Parse key: value pairs.
    size_t colon_pos = trimmed.find(':');
    if (colon_pos == std::string::npos) {
      continue;
    }

    std::string key = TrimString(trimmed.substr(0, colon_pos));
    std::string value = TrimString(trimmed.substr(colon_pos + 1));

    if (key == "name") {
      entry->name = value;
    } else if (key == "description") {
      entry->description = value;
    } else if (key == "requiresPageContent") {
      entry->requires_page_content = (value == "true");
    } else if (key == "hosts") {
      // Check for inline array format: [a, b, c]
      if (!value.empty() && value.front() == '[') {
        // Remove brackets.
        std::string inner = value.substr(1);
        size_t bracket_end = inner.find(']');
        if (bracket_end != std::string::npos) {
          inner = inner.substr(0, bracket_end);
        }
        std::vector<std::string> parts = base::SplitString(
            inner, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
        for (auto& part : parts) {
          std::string h = TrimString(part);
          // Remove surrounding quotes if present.
          if (h.size() >= 2 &&
              ((h.front() == '"' && h.back() == '"') ||
               (h.front() == '\'' && h.back() == '\''))) {
            h = h.substr(1, h.size() - 2);
          }
          if (!h.empty()) {
            entry->hosts.push_back(h);
          }
        }
      } else if (value.empty()) {
        // Multi-line list follows.
        in_hosts_list = true;
      }
    }
  }

  return true;
}

}  // namespace dao
