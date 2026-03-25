// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_

#include <map>
#include <set>
#include <string>

#include "base/containers/lru_cache.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_agent_scenario_registry.h"

class Browser;
class Profile;

namespace content {
class WebContents;
}

namespace dao {

class DaoAgentMemoryService;

// Observes browser navigation events and emits proactive suggestions based on:
// 1. Scenario matching (seed + personal) — URL pattern + page hints
// 2. Episodic memory matches (legacy behavior)
// 3. Learning pipeline triggers
//
// Lifecycle: Owned by DaoAgentMemoryService (profile-scoped, always running).
class DaoAgentProactiveEngine : public BrowserListObserver,
                                 public TabStripModelObserver {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnProactiveSuggestion(
        const ProactiveSuggestion& suggestion) = 0;
  };

  DaoAgentProactiveEngine(DaoAgentMemoryService* memory_service,
                           Profile* profile);
  ~DaoAgentProactiveEngine() override;

  DaoAgentProactiveEngine(const DaoAgentProactiveEngine&) = delete;
  DaoAgentProactiveEngine& operator=(const DaoAgentProactiveEngine&) = delete;

  void SetDelegate(Delegate* delegate);
  void SetConfidenceThreshold(double threshold);
  double GetConfidenceThreshold() const { return confidence_threshold_; }

  // Access the in-memory scenario registry.
  DaoAgentScenarioRegistry& scenario_registry() { return scenario_registry_; }

  // Start/stop observing.
  void Start();
  void Stop();
  bool is_running() const { return is_running_; }

  // BrowserListObserver:
  void OnBrowserAdded(Browser* browser) override;
  void OnBrowserRemoved(Browser* browser) override;

  // TabStripModelObserver:
  void OnTabStripModelChanged(
      TabStripModel* tab_strip_model,
      const TabStripModelChange& change,
      const TabStripSelectionChange& selection) override;

 private:
  class ActiveTabObserver;

  // Dedup key: (url, scenario_id). Prevents re-showing the same scenario
  // on the same URL in the same session.
  using DedupKey = std::pair<std::string, std::string>;

  void OnNavigationCompleted(const std::string& url,
                             const std::string& domain,
                             const std::string& title,
                             content::WebContents* web_contents);

  // Dwell timer callback — fires 15 seconds after navigation.
  void OnDwellTimerFired(std::string url,
                         std::string domain,
                         base::WeakPtr<content::WebContents> weak_contents);

  // Content analysis JS result callback.
  void OnContentAnalysisResult(std::string url,
                               std::string domain,
                               const ScenarioDefinition& scenario,
                               int tab_id,
                               base::Value result);

  // Cooldown check callback.
  void OnCooldownScoreReceived(const std::string& url,
                               const ScenarioDefinition& scenario,
                               int tab_id,
                               double cooldown_score);

  // Legacy episode-based suggestions (fallback when no scenario matches).
  void OnEpisodesLoaded(const std::string& domain,
                        std::vector<Episode> episodes);

  raw_ptr<DaoAgentMemoryService> memory_service_;
  raw_ptr<Profile> profile_;
  raw_ptr<Delegate> delegate_ = nullptr;
  double confidence_threshold_ = 0.7;
  bool is_running_ = false;

  DaoAgentScenarioRegistry scenario_registry_;

  // Dedup: (url, scenario_id) pairs already shown this session.
  std::set<DedupKey> shown_scenarios_;

  // Dwell timer — one-shot, 15 seconds.
  base::OneShotTimer dwell_timer_;

  std::unique_ptr<ActiveTabObserver> active_tab_observer_;

  base::WeakPtrFactory<DaoAgentProactiveEngine> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_
