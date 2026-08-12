// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_IMPORT_DAO_MIGRATION_SERVICE_FACTORY_H_
#define DAO_BROWSER_IMPORT_DAO_MIGRATION_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace dao::import {

class DaoMigrationService;

class DaoMigrationServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static DaoMigrationService* GetForProfile(Profile* profile);
  static DaoMigrationServiceFactory* GetInstance();

 private:
  friend class base::NoDestructor<DaoMigrationServiceFactory>;

  DaoMigrationServiceFactory();
  ~DaoMigrationServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace dao::import

#endif  // DAO_BROWSER_IMPORT_DAO_MIGRATION_SERVICE_FACTORY_H_
