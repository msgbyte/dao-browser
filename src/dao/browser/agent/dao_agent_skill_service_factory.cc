// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_skill_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "dao/browser/agent/dao_agent_skill_service.h"

namespace dao {

// static
DaoAgentSkillService* DaoAgentSkillServiceFactory::GetForProfile(
    Profile* profile) {
  // Skills are always available (no pref gate).
  return static_cast<DaoAgentSkillService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoAgentSkillServiceFactory* DaoAgentSkillServiceFactory::GetInstance() {
  static base::NoDestructor<DaoAgentSkillServiceFactory> instance;
  return instance.get();
}

DaoAgentSkillServiceFactory::DaoAgentSkillServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoAgentSkillService",
          BrowserContextDependencyManager::GetInstance()) {}

DaoAgentSkillServiceFactory::~DaoAgentSkillServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoAgentSkillServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<DaoAgentSkillService>(profile->GetPath());
}

content::BrowserContext*
DaoAgentSkillServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // Return null for incognito profiles — no skill persistence in incognito.
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }
  return context;
}

}  // namespace dao
