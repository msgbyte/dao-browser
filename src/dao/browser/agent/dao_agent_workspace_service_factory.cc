// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_workspace_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "dao/browser/agent/dao_agent_workspace_service.h"

namespace dao {

// static
DaoAgentWorkspaceService* DaoAgentWorkspaceServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<DaoAgentWorkspaceService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoAgentWorkspaceServiceFactory*
DaoAgentWorkspaceServiceFactory::GetInstance() {
  static base::NoDestructor<DaoAgentWorkspaceServiceFactory> instance;
  return instance.get();
}

DaoAgentWorkspaceServiceFactory::DaoAgentWorkspaceServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoAgentWorkspaceService",
          BrowserContextDependencyManager::GetInstance()) {}

DaoAgentWorkspaceServiceFactory::~DaoAgentWorkspaceServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoAgentWorkspaceServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<DaoAgentWorkspaceService>(profile->GetPath());
}

content::BrowserContext*
DaoAgentWorkspaceServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  if (profile->IsOffTheRecord()) {
    return nullptr;
  }
  return context;
}

}  // namespace dao
