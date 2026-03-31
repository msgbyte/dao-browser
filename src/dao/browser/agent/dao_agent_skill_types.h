// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_SKILL_TYPES_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_SKILL_TYPES_H_

#include <string>
#include <vector>

namespace dao {

// Metadata for a single skill, parsed from the YAML frontmatter of SKILL.md.
struct SkillRegistryEntry {
  SkillRegistryEntry();
  ~SkillRegistryEntry();
  SkillRegistryEntry(const SkillRegistryEntry&);
  SkillRegistryEntry& operator=(const SkillRegistryEntry&);
  SkillRegistryEntry(SkillRegistryEntry&&);
  SkillRegistryEntry& operator=(SkillRegistryEntry&&);

  std::string id;
  std::string name;
  std::string description;
  std::string source;  // "builtin" or "user"
  std::vector<std::string> hosts;
  bool requires_page_content = false;
};

// Full skill content: metadata plus the instruction body from SKILL.md.
struct SkillContent {
  SkillContent();
  ~SkillContent();
  SkillContent(const SkillContent&);
  SkillContent& operator=(const SkillContent&);
  SkillContent(SkillContent&&);
  SkillContent& operator=(SkillContent&&);

  SkillRegistryEntry metadata;
  std::string instructions;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_SKILL_TYPES_H_
