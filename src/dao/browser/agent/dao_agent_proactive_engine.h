// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "base/containers/lru_cache.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/agent/dao_agent_memory_types.h"
#include "dao/browser/agent/dao_agent_scenario_registry.h"

class Browser;
class GURL;
class Profile;

namespace content {
class WebContents;
}

namespace dao {

class DaoAgentMemoryService;

std::string GetDomainForProactiveSuggestion(const GURL& url);

bool IsSensitiveUrlForProactiveSuggestion(const GURL& url,
                                          const std::string& domain);

bool DoesLegacyEpisodeMatchPageForProactiveSuggestion(
    const Episode& episode,
    const std::string& page_url);

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

  void RecordShownScenarioForFeedback(const std::string& url,
                                      const std::string& domain,
                                      const std::string& action_label,
                                      const std::string& scenario_id,
                                      base::Time shown_time);

  bool HasShownScenarioForTesting(const std::string& url,
                                  const std::string& scenario_id) const;
  base::Time GetLastDomainActionShownForTesting(
      const std::string& domain,
      const std::string& action_label) const;

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
  struct PendingScenarioEvaluation;

  // Dedup key: (url, scenario_id). Prevents re-showing the same scenario
  // on the same URL in the same session.
  using DedupKey = std::pair<std::string, std::string>;
  using DomainActionKey = std::pair<std::string, std::string>;

  void InvalidateActivePageEvaluations();
  void OnNavigationCompleted(const std::string& url,
                             const std::string& domain,
                             const std::string& title,
                             content::WebContents* web_contents);

  // Dwell timer callback — fires 15 seconds after navigation.
  void OnDwellTimerFired(std::string url,
                         std::string domain,
                         uint64_t navigation_generation,
                         base::WeakPtr<content::WebContents> weak_contents);

  // Content analysis JS result callback.
  void OnContentAnalysisResult(
      std::string url,
      std::string domain,
      uint64_t navigation_generation,
      base::WeakPtr<content::WebContents> weak_contents,
      std::vector<ScenarioDefinition> scenarios,
      int tab_id,
      base::Value result);

  void OnScenarioCooldownScoreReceived(
      std::shared_ptr<PendingScenarioEvaluation> evaluation,
      const ScenarioDefinition& scenario,
      double cooldown_score);
  void FinalizeScenarioEvaluation(
      std::shared_ptr<PendingScenarioEvaluation> evaluation);

  // Legacy episode-based suggestions (fallback when no scenario matches).
  void OnEpisodesLoaded(std::string url,
                        std::string domain,
                        uint64_t navigation_generation,
                        base::WeakPtr<content::WebContents> weak_contents,
                        int tab_id,
                        std::vector<Episode> episodes);

  raw_ptr<DaoAgentMemoryService> memory_service_;
  raw_ptr<Profile> profile_;
  raw_ptr<Delegate> delegate_ = nullptr;
  double confidence_threshold_ = 0.75;
  bool is_running_ = false;

  DaoAgentScenarioRegistry scenario_registry_;

  // Dedup: (url, scenario_id) pairs already shown this session.
  std::set<DedupKey> shown_scenarios_;
  std::map<DomainActionKey, base::Time> last_domain_action_shown_;

  // Dwell timer — one-shot, 15 seconds.
  base::OneShotTimer dwell_timer_;

  // Incremented whenever the active tab's committed page context changes or is
  // invalidated, including same-URL reloads and active-tab switches.
  uint64_t active_navigation_generation_ = 0;

  std::unique_ptr<ActiveTabObserver> active_tab_observer_;

  base::WeakPtrFactory<DaoAgentProactiveEngine> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_
