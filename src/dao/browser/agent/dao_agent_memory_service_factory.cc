// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_service_factory.h"

#include "base/files/file_path.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/prefs/pref_service.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/dao_pref_names.h"

namespace dao {

// static
DaoAgentMemoryService* DaoAgentMemoryServiceFactory::GetForProfile(
    Profile* profile) {
  if (!profile->GetPrefs()->GetBoolean(prefs::kDaoAgentMemoryEnabled)) {
    return nullptr;
  }
  return static_cast<DaoAgentMemoryService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoAgentMemoryServiceFactory* DaoAgentMemoryServiceFactory::GetInstance() {
  static base::NoDestructor<DaoAgentMemoryServiceFactory> instance;
  return instance.get();
}

DaoAgentMemoryServiceFactory::DaoAgentMemoryServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoAgentMemoryService",
          BrowserContextDependencyManager::GetInstance()) {}

DaoAgentMemoryServiceFactory::~DaoAgentMemoryServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoAgentMemoryServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<DaoAgentMemoryService>(profile->GetPath());
}

content::BrowserContext*
DaoAgentMemoryServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  // Return null for incognito profiles — no memory persistence in incognito.
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }
  return context;
}

}  // namespace dao
