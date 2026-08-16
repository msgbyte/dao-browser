// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/home/dao_home_project_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "dao/browser/home/dao_home_project_service.h"

namespace dao {

// static
DaoHomeProjectService* DaoHomeProjectServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<DaoHomeProjectService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoHomeProjectServiceFactory* DaoHomeProjectServiceFactory::GetInstance() {
  static base::NoDestructor<DaoHomeProjectServiceFactory> instance;
  return instance.get();
}

DaoHomeProjectServiceFactory::DaoHomeProjectServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoHomeProjectService",
          BrowserContextDependencyManager::GetInstance()) {}

DaoHomeProjectServiceFactory::~DaoHomeProjectServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoHomeProjectServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<DaoHomeProjectService>(profile->GetPath());
}

content::BrowserContext* DaoHomeProjectServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return profile->IsOffTheRecord() ? nullptr : context;
}

}  // namespace dao
