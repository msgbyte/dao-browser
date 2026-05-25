// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_SKILL_SERVICE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_SKILL_SERVICE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "base/memory/weak_ptr.h"
#include "base/sequence_checker.h"
#include "base/task/sequenced_task_runner.h"
#include "components/keyed_service/core/keyed_service.h"
#include "dao/browser/agent/dao_agent_skill_types.h"

namespace dao {

// Profile-keyed service that manages agent skills on disk. Skills are stored
// as SKILL.md files under the DaoAgentSkills directory within the profile path.
// All disk I/O is posted to a background sequence. Callbacks are invoked on the
// calling (typically UI) thread.
//
// Directory layout:
//   DaoAgentSkills/
//     builtin/<skill_id>/SKILL.md
//     user/global/<skill_id>/SKILL.md
//     user/hosts/<host>/<skill_id>/SKILL.md
class DaoAgentSkillService : public KeyedService {
 public:
  explicit DaoAgentSkillService(const base::FilePath& profile_path);
  ~DaoAgentSkillService() override;

  DaoAgentSkillService(const DaoAgentSkillService&) = delete;
  DaoAgentSkillService& operator=(const DaoAgentSkillService&) = delete;

  // KeyedService:
  void Shutdown() override;

  // Scan all skill directories and return metadata for every skill found.
  void GetSkillRegistry(
      base::OnceCallback<void(std::vector<SkillRegistryEntry>)> callback);

  // Read the full SKILL.md for a given skill ID. Returns nullopt if not found.
  void GetSkillContent(
      const std::string& skill_id,
      base::OnceCallback<void(std::optional<SkillContent>)> callback);

  // Create or update a user skill. If |host| is empty, saves to
  // user/global/<skill_id>/SKILL.md. If |host| is non-empty, saves to
  // user/hosts/<host>/<skill_id>/SKILL.md.
  void SaveUserSkill(const std::string& skill_id,
                     const std::string& skill_md_content,
                     const std::string& host,
                     base::OnceCallback<void(bool)> callback);

  // Delete a user skill directory by ID.
  void DeleteUserSkill(const std::string& skill_id,
                       base::OnceCallback<void(bool)> callback);

  // Toggle the disabled state of a skill (works for both builtin and user
  // skills). Updates the `disabled:` field in the YAML frontmatter of the
  // skill's SKILL.md, inserting it if missing.
  void SetSkillDisabled(const std::string& skill_id,
                        bool disabled,
                        base::OnceCallback<void(bool)> callback);

  // Write built-in skills to disk if they don't already exist.
  void InitBuiltinSkills();

 private:
  // Background-thread implementations.
  static void InitBuiltinSkillsOnBackground(const base::FilePath& skills_path);
  static std::vector<SkillRegistryEntry> GetSkillRegistryOnBackground(
      const base::FilePath& skills_path);
  static std::optional<SkillContent> GetSkillContentOnBackground(
      const base::FilePath& skills_path,
      const std::string& skill_id);
  static bool SaveUserSkillOnBackground(const base::FilePath& skills_path,
                                        const std::string& skill_id,
                                        const std::string& skill_md_content,
                                        const std::string& host);
  static bool DeleteUserSkillOnBackground(const base::FilePath& skills_path,
                                          const std::string& skill_id);

  static bool SetSkillDisabledOnBackground(const base::FilePath& skills_path,
                                           const std::string& skill_id,
                                           bool disabled);

  // Parses a SKILL.md file content into metadata and instructions.
  static bool ParseSkillMd(const std::string& content,
                           SkillRegistryEntry* entry,
                           std::string* instructions);

  // Scans a single directory level for skill subdirectories containing
  // SKILL.md. Populates |source| and optionally |host| in each entry.
  static void ScanSkillDir(const base::FilePath& dir,
                           const std::string& source,
                           const std::string& host,
                           std::vector<SkillRegistryEntry>* out);

  base::FilePath skills_path_;
  scoped_refptr<base::SequencedTaskRunner> background_task_runner_;

  SEQUENCE_CHECKER(ui_sequence_checker_);
  base::WeakPtrFactory<DaoAgentSkillService> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_SKILL_SERVICE_H_
