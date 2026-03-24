// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_SERVICE_FACTORY_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace dao {

class DaoAgentMemoryService;

class DaoAgentMemoryServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static DaoAgentMemoryService* GetForProfile(Profile* profile);
  static DaoAgentMemoryServiceFactory* GetInstance();

  DaoAgentMemoryServiceFactory(const DaoAgentMemoryServiceFactory&) = delete;
  DaoAgentMemoryServiceFactory& operator=(
      const DaoAgentMemoryServiceFactory&) = delete;

 private:
  friend class base::NoDestructor<DaoAgentMemoryServiceFactory>;

  DaoAgentMemoryServiceFactory();
  ~DaoAgentMemoryServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_SERVICE_FACTORY_H_
