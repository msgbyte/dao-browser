// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/import/dao_migration_service_factory.h"

#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "dao/browser/import/dao_migration_service.h"

namespace dao::import {

DaoMigrationService* DaoMigrationServiceFactory::GetForProfile(
    Profile* profile) {
  return static_cast<DaoMigrationService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}

DaoMigrationServiceFactory* DaoMigrationServiceFactory::GetInstance() {
  static base::NoDestructor<DaoMigrationServiceFactory> instance;
  return instance.get();
}

DaoMigrationServiceFactory::DaoMigrationServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "DaoMigrationService",
          BrowserContextDependencyManager::GetInstance()) {}

DaoMigrationServiceFactory::~DaoMigrationServiceFactory() = default;

std::unique_ptr<KeyedService>
DaoMigrationServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<DaoMigrationService>(
      Profile::FromBrowserContext(context));
}

content::BrowserContext* DaoMigrationServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return profile->IsOffTheRecord() ? nullptr : context;
}

}  // namespace dao::import
