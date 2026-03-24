// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_

#include <map>
#include <string>

#include "base/containers/lru_cache.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_list_observer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

class Browser;
class Profile;

namespace content {
class WebContents;
}

namespace dao {

class DaoAgentMemoryService;

// Observes browser navigation events and emits proactive suggestions
// based on episodic memory matches. Uses a per-browser TabStripModelObserver
// and attaches a WebContentsObserver to the active tab.
//
// Lifecycle: Created by DaoAgentMemoryService or the WebUI handler,
// lives as long as the profile.
class DaoAgentProactiveEngine : public BrowserListObserver,
                                 public TabStripModelObserver {
 public:
  class Delegate {
   public:
    virtual ~Delegate() = default;
    virtual void OnProactiveSuggestion(const ProactiveSuggestion& suggestion) {}
  };

  DaoAgentProactiveEngine(DaoAgentMemoryService* memory_service,
                           Profile* profile);
  ~DaoAgentProactiveEngine() override;

  DaoAgentProactiveEngine(const DaoAgentProactiveEngine&) = delete;
  DaoAgentProactiveEngine& operator=(const DaoAgentProactiveEngine&) = delete;

  void SetDelegate(Delegate* delegate);
  void SetConfidenceThreshold(double threshold);
  double GetConfidenceThreshold() const { return confidence_threshold_; }

  // Start/stop observing.
  void Start();
  void Stop();

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

  void OnNavigationCompleted(const std::string& url,
                             const std::string& domain,
                             const std::string& title);
  void OnEpisodesLoaded(const std::string& domain,
                        std::vector<Episode> episodes);

  raw_ptr<DaoAgentMemoryService> memory_service_;
  raw_ptr<Profile> profile_;
  raw_ptr<Delegate> delegate_ = nullptr;
  double confidence_threshold_ = 0.7;

  // LRU cache to avoid querying the same domain repeatedly.
  base::LRUCache<std::string, bool> domain_query_cache_;

  std::unique_ptr<ActiveTabObserver> active_tab_observer_;

  base::WeakPtrFactory<DaoAgentProactiveEngine> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_ENGINE_H_
