// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_memory_service_factory.h"
#include "dao/browser/agent/dao_dream_service.h"
#include "dao/browser/dao_pref_names.h"

namespace dao {

// static
DaoDreamService* DaoDreamServiceFactory::GetForProfile(Profile* profile) {
  if (!profile->GetPrefs()->GetBoolean(prefs::kDaoAgentMemoryEnabled) ||
      !profile->GetPrefs()->GetBoolean(prefs::kDaoDreamEnabled)) {
    return nullptr;
  }
  return static_cast<DaoDreamService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoDreamServiceFactory* DaoDreamServiceFactory::GetInstance() {
  static base::NoDestructor<DaoDreamServiceFactory> instance;
  return instance.get();
}

DaoDreamServiceFactory::DaoDreamServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoDreamService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(DaoAgentMemoryServiceFactory::GetInstance());
}

DaoDreamServiceFactory::~DaoDreamServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoDreamServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  DaoAgentMemoryService* memory =
      DaoAgentMemoryServiceFactory::GetForProfile(profile);
  if (!memory) {
    return nullptr;
  }
  return std::make_unique<DaoDreamService>(profile, memory);
}

content::BrowserContext* DaoDreamServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }
  return context;
}

}  // namespace dao
