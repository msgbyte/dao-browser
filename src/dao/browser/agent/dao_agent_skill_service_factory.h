// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_SKILL_SERVICE_FACTORY_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_SKILL_SERVICE_FACTORY_H_

#include "base/no_destructor.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"

class Profile;

namespace dao {

class DaoAgentSkillService;

class DaoAgentSkillServiceFactory
    : public BrowserContextKeyedServiceFactory {
 public:
  static DaoAgentSkillService* GetForProfile(Profile* profile);
  static DaoAgentSkillServiceFactory* GetInstance();

  DaoAgentSkillServiceFactory(const DaoAgentSkillServiceFactory&) = delete;
  DaoAgentSkillServiceFactory& operator=(
      const DaoAgentSkillServiceFactory&) = delete;

 private:
  friend class base::NoDestructor<DaoAgentSkillServiceFactory>;

  DaoAgentSkillServiceFactory();
  ~DaoAgentSkillServiceFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
  content::BrowserContext* GetBrowserContextToUse(
      content::BrowserContext* context) const override;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_SKILL_SERVICE_FACTORY_H_
