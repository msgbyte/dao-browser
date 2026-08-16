// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_HOME_DAO_HOME_PROJECT_SERVICE_FACTORY_H_
#define DAO_BROWSER_HOME_DAO_HOME_PROJECT_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace dao {

class DaoHomeProjectService;

class DaoHomeProjectServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static DaoHomeProjectService* GetForProfile(Profile* profile);
  static DaoHomeProjectServiceFactory* GetInstance();

  DaoHomeProjectServiceFactory(const DaoHomeProjectServiceFactory&) = delete;
  DaoHomeProjectServiceFactory& operator=(const DaoHomeProjectServiceFactory&) =
      delete;

 private:
  friend base::NoDestructor<DaoHomeProjectServiceFactory>;

  DaoHomeProjectServiceFactory();
  ~DaoHomeProjectServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace dao

#endif  // DAO_BROWSER_HOME_DAO_HOME_PROJECT_SERVICE_FACTORY_H_
