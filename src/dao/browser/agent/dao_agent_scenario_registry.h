// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_SCENARIO_REGISTRY_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_SCENARIO_REGISTRY_H_

#include <optional>
#include <string>
#include <vector>

#include "dao/browser/agent/dao_agent_memory_types.h"

namespace dao {

// In-memory registry merging hard-coded seed scenarios with user-learned
// personal scenarios from SQLite. All matching happens on the UI thread.
class DaoAgentScenarioRegistry {
 public:
  DaoAgentScenarioRegistry();
  ~DaoAgentScenarioRegistry();

  DaoAgentScenarioRegistry(const DaoAgentScenarioRegistry&) = delete;
  DaoAgentScenarioRegistry& operator=(const DaoAgentScenarioRegistry&) = delete;

  // Load personal scenarios from the store. Call after store is initialized.
  void SetPersonalScenarios(std::vector<ScenarioDefinition> personal);

  // Add a single personal scenario.
  void AddPersonalScenario(ScenarioDefinition scenario);

  // Remove a personal scenario by id.
  void RemovePersonalScenario(const std::string& id);

  // Match a URL against all scenarios. Returns the best-matching scenario,
  // checking seeds first (by specificity), then personal scenarios.
  std::optional<ScenarioDefinition> Match(const std::string& url) const;

  // Returns every matching scenario with seed matches first and personal
  // matches ordered by learned acceptance rate.
  std::vector<ScenarioDefinition> GetMatchingScenarios(
      const std::string& url) const;

  // Get all scenarios (seeds + personal).
  const std::vector<ScenarioDefinition>& seed_scenarios() const {
    return seed_scenarios_;
  }
  const std::vector<ScenarioDefinition>& personal_scenarios() const {
    return personal_scenarios_;
  }

 private:
  // Built-in seed scenarios, populated in constructor.
  std::vector<ScenarioDefinition> seed_scenarios_;
  // User-learned personal scenarios, loaded from SQLite.
  std::vector<ScenarioDefinition> personal_scenarios_;

  void InitSeedScenarios();
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_SCENARIO_REGISTRY_H_
