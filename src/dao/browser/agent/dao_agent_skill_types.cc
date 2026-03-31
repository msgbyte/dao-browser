// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_skill_types.h"

namespace dao {

SkillRegistryEntry::SkillRegistryEntry() = default;
SkillRegistryEntry::~SkillRegistryEntry() = default;
SkillRegistryEntry::SkillRegistryEntry(const SkillRegistryEntry&) = default;
SkillRegistryEntry& SkillRegistryEntry::operator=(const SkillRegistryEntry&) =
    default;
SkillRegistryEntry::SkillRegistryEntry(SkillRegistryEntry&&) = default;
SkillRegistryEntry& SkillRegistryEntry::operator=(SkillRegistryEntry&&) =
    default;

SkillContent::SkillContent() = default;
SkillContent::~SkillContent() = default;
SkillContent::SkillContent(const SkillContent&) = default;
SkillContent& SkillContent::operator=(const SkillContent&) = default;
SkillContent::SkillContent(SkillContent&&) = default;
SkillContent& SkillContent::operator=(SkillContent&&) = default;

}  // namespace dao
