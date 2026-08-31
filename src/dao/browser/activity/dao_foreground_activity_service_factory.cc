// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/activity/dao_foreground_activity_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_manager_service_factory.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "dao/browser/activity/dao_foreground_activity_service.h"

namespace dao {

// static
DaoForegroundActivityService*
DaoForegroundActivityServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<DaoForegroundActivityService*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
DaoForegroundActivityServiceFactory*
DaoForegroundActivityServiceFactory::GetInstance() {
  static base::NoDestructor<DaoForegroundActivityServiceFactory> instance;
  return instance.get();
}

DaoForegroundActivityServiceFactory::DaoForegroundActivityServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoForegroundActivityService",
          BrowserContextDependencyManager::GetInstance()) {
  DependsOn(BrowserManagerServiceFactory::GetInstance());
}

DaoForegroundActivityServiceFactory::~DaoForegroundActivityServiceFactory() =
    default;

std::unique_ptr<KeyedService>
DaoForegroundActivityServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<DaoForegroundActivityService>(
      Profile::FromBrowserContext(context));
}

content::BrowserContext*
DaoForegroundActivityServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return profile->IsOffTheRecord() ? nullptr : context;
}

bool DaoForegroundActivityServiceFactory::ServiceIsCreatedWithBrowserContext()
    const {
  return true;
}

}  // namespace dao
