// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_SERVICE_FACTORY_H_
#define DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_SERVICE_FACTORY_H_

#include <memory>

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace dao {

class DaoForegroundActivityService;

class DaoForegroundActivityServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static DaoForegroundActivityService* GetForProfile(Profile* profile);
  static DaoForegroundActivityServiceFactory* GetInstance();

  DaoForegroundActivityServiceFactory(
      const DaoForegroundActivityServiceFactory&) = delete;
  DaoForegroundActivityServiceFactory& operator=(
      const DaoForegroundActivityServiceFactory&) = delete;

 private:
  friend base::NoDestructor<DaoForegroundActivityServiceFactory>;

  DaoForegroundActivityServiceFactory();
  ~DaoForegroundActivityServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
  bool ServiceIsCreatedWithBrowserContext() const override;
};

}  // namespace dao

#endif  // DAO_BROWSER_ACTIVITY_DAO_FOREGROUND_ACTIVITY_SERVICE_FACTORY_H_
