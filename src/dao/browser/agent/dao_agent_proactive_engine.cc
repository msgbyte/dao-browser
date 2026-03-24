// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_proactive_engine.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace dao {

namespace {

std::string GetDomainFromUrl(const GURL& url) {
  return net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

}  // namespace

// Inner class: observes the currently active tab's navigation.
class DaoAgentProactiveEngine::ActiveTabObserver
    : public content::WebContentsObserver {
 public:
  ActiveTabObserver(DaoAgentProactiveEngine* engine,
                    content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents), engine_(engine) {}

  // WebContentsObserver:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted()) {
      return;
    }
    const GURL& url = navigation_handle->GetURL();
    if (!url.SchemeIsHTTPOrHTTPS()) {
      return;
    }
    std::string domain = GetDomainFromUrl(url);
    engine_->OnNavigationCompleted(url.spec(), domain,
                                   web_contents()->GetTitle().empty()
                                       ? ""
                                       : base::UTF16ToUTF8(
                                             web_contents()->GetTitle()));
  }

 private:
  raw_ptr<DaoAgentProactiveEngine> engine_;
};

DaoAgentProactiveEngine::DaoAgentProactiveEngine(
    DaoAgentMemoryService* memory_service,
    Profile* profile)
    : memory_service_(memory_service),
      profile_(profile),
      domain_query_cache_(50) {}

DaoAgentProactiveEngine::~DaoAgentProactiveEngine() {
  Stop();
}

void DaoAgentProactiveEngine::SetDelegate(Delegate* delegate) {
  delegate_ = delegate;
}

void DaoAgentProactiveEngine::SetConfidenceThreshold(double threshold) {
  confidence_threshold_ = threshold;
}

void DaoAgentProactiveEngine::Start() {
  BrowserList::AddObserver(this);

  // Observe existing browsers for this profile.
  for (Browser* browser : *BrowserList::GetInstance()) {
    if (browser->profile() == profile_) {
      browser->tab_strip_model()->AddObserver(this);
    }
  }
}

void DaoAgentProactiveEngine::Stop() {
  BrowserList::RemoveObserver(this);

  for (Browser* browser : *BrowserList::GetInstance()) {
    if (browser->profile() == profile_) {
      browser->tab_strip_model()->RemoveObserver(this);
    }
  }

  active_tab_observer_.reset();
}

// BrowserListObserver:

void DaoAgentProactiveEngine::OnBrowserAdded(Browser* browser) {
  if (browser->profile() == profile_) {
    browser->tab_strip_model()->AddObserver(this);
  }
}

void DaoAgentProactiveEngine::OnBrowserRemoved(Browser* browser) {
  if (browser->profile() == profile_) {
    browser->tab_strip_model()->RemoveObserver(this);
  }
}

// TabStripModelObserver:

void DaoAgentProactiveEngine::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!selection.active_tab_changed()) {
    return;
  }

  content::WebContents* new_contents = selection.new_contents;
  if (!new_contents) {
    active_tab_observer_.reset();
    return;
  }

  // Attach observer to newly active tab.
  active_tab_observer_ =
      std::make_unique<ActiveTabObserver>(this, new_contents);

  // Also check current URL immediately.
  const GURL& url = new_contents->GetLastCommittedURL();
  if (url.SchemeIsHTTPOrHTTPS()) {
    std::string domain = GetDomainFromUrl(url);
    OnNavigationCompleted(url.spec(), domain,
                          base::UTF16ToUTF8(new_contents->GetTitle()));
  }
}

void DaoAgentProactiveEngine::OnNavigationCompleted(
    const std::string& url,
    const std::string& domain,
    const std::string& title) {
  if (domain.empty() || !memory_service_) {
    return;
  }

  // Check LRU cache to avoid re-querying the same domain.
  auto it = domain_query_cache_.Get(domain);
  if (it != domain_query_cache_.end()) {
    return;
  }
  domain_query_cache_.Put(domain, true);

  // Query memory for episodes on this domain.
  memory_service_->GetEpisodesByDomain(
      domain, 3,
      base::BindOnce(&DaoAgentProactiveEngine::OnEpisodesLoaded,
                     weak_factory_.GetWeakPtr(), domain));
}

void DaoAgentProactiveEngine::OnEpisodesLoaded(
    const std::string& domain,
    std::vector<Episode> episodes) {
  if (!delegate_ || episodes.empty()) {
    return;
  }

  // Find the best episode above threshold.
  const Episode* best = nullptr;
  for (const auto& ep : episodes) {
    if (ep.confidence >= confidence_threshold_) {
      if (!best || ep.confidence > best->confidence) {
        best = &ep;
      }
    }
  }

  if (!best) {
    return;
  }

  ProactiveSuggestion suggestion;
  suggestion.episode_id = best->id;
  suggestion.confidence = best->confidence;

  // Determine suggestion type based on whether there are multiple episodes.
  if (episodes.size() >= 2) {
    suggestion.type = "repeat_action";
    suggestion.text =
        "You usually interact with this page. Want me to help again?";
  } else {
    suggestion.type = "continue_conversation";
    suggestion.text = "Last time here, you discussed: " + best->intent;
  }

  delegate_->OnProactiveSuggestion(suggestion);
}

}  // namespace dao
