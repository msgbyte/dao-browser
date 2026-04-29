// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_proactive_engine.h"

#include "base/functional/bind.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace dao {

namespace {

constexpr base::TimeDelta kDwellDelay = base::Seconds(15);
constexpr double kCooldownThreshold = 3.0;

std::string GetDomainFromUrl(const GURL& url) {
  return net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

// Content analysis snippet that returns page metadata.
constexpr char kContentAnalysisScript[] = R"js(
(function() {
  var text = document.body ? document.body.innerText : '';
  return {
    wordCount: text.split(/\s+/).length,
    charCount: text.length,
    lang: document.documentElement.lang || '',
    title: document.title,
    url: location.href,
    hasCode: document.querySelectorAll('pre, code').length > 0
  };
})()
)js";

// Content significance: Latin > 1500 words OR CJK > 3000 characters.
bool IsContentSignificant(const base::Value& analysis) {
  if (!analysis.is_dict()) {
    return false;
  }
  int word_count = analysis.GetDict().FindInt("wordCount").value_or(0);
  int char_count = analysis.GetDict().FindInt("charCount").value_or(0);
  const std::string* lang =
      analysis.GetDict().FindString("lang");
  bool is_cjk = lang && (base::StartsWith(*lang, "zh") ||
                          base::StartsWith(*lang, "ja") ||
                          base::StartsWith(*lang, "ko"));
  if (is_cjk) {
    return char_count > 3000;
  }
  return word_count > 1500;
}

bool IsSkippableUrl(const GURL& url) {
  return !url.SchemeIsHTTPOrHTTPS();
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
    // Support SPA navigation: do NOT filter IsSameDocument().
    const GURL& url = navigation_handle->GetURL();
    if (IsSkippableUrl(url)) {
      return;
    }
    std::string domain = GetDomainFromUrl(url);
    engine_->OnNavigationCompleted(url.spec(), domain,
                                   base::UTF16ToUTF8(
                                       web_contents()->GetTitle()),
                                   web_contents());
  }

 private:
  raw_ptr<DaoAgentProactiveEngine> engine_;
};

DaoAgentProactiveEngine::DaoAgentProactiveEngine(
    DaoAgentMemoryService* memory_service,
    Profile* profile)
    : memory_service_(memory_service),
      profile_(profile) {}

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
  if (is_running_) {
    return;
  }
  is_running_ = true;
  BrowserList::AddObserver(this);

  // Observe existing browsers for this profile.
  for (Browser* browser : chrome::FindAllBrowsersWithProfile(profile_)) {
    browser->tab_strip_model()->AddObserver(this);
  }
}

void DaoAgentProactiveEngine::Stop() {
  if (!is_running_) {
    return;
  }
  is_running_ = false;
  dwell_timer_.Stop();
  BrowserList::RemoveObserver(this);

  for (Browser* browser : chrome::FindAllBrowsersWithProfile(profile_)) {
    browser->tab_strip_model()->RemoveObserver(this);
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
    dwell_timer_.Stop();
    return;
  }

  // Attach observer to newly active tab.
  active_tab_observer_ =
      std::make_unique<ActiveTabObserver>(this, new_contents);

  // Cancel any pending dwell timer (tab switch = new context).
  dwell_timer_.Stop();

  // Also check current URL immediately.
  const GURL& url = new_contents->GetLastCommittedURL();
  if (!IsSkippableUrl(url)) {
    std::string domain = GetDomainFromUrl(url);
    OnNavigationCompleted(url.spec(), domain,
                          base::UTF16ToUTF8(new_contents->GetTitle()),
                          new_contents);
  }
}

void DaoAgentProactiveEngine::OnNavigationCompleted(
    const std::string& url,
    const std::string& domain,
    const std::string& title,
    content::WebContents* web_contents) {
  if (domain.empty() || !memory_service_) {
    return;
  }

  // Cancel previous dwell timer.
  dwell_timer_.Stop();

  // Start dwell timer — fires after 15 seconds on the same page.
  base::WeakPtr<content::WebContents> weak_contents =
      web_contents->GetWeakPtr();
  dwell_timer_.Start(
      FROM_HERE, kDwellDelay,
      base::BindOnce(&DaoAgentProactiveEngine::OnDwellTimerFired,
                     weak_factory_.GetWeakPtr(), url, domain,
                     std::move(weak_contents)));
}

void DaoAgentProactiveEngine::OnDwellTimerFired(
    std::string url,
    std::string domain,
    base::WeakPtr<content::WebContents> weak_contents) {
  content::WebContents* web_contents = weak_contents.get();
  if (!web_contents) {
    return;
  }

  // Check if web contents is still alive and on the same URL.
  content::RenderFrameHost* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }

  // Try scenario matching first.
  auto scenario = scenario_registry_.Match(url);
  if (!scenario.has_value()) {
    // No scenario match — fall back to legacy episode-based suggestions.
    memory_service_->GetEpisodesByDomain(
        domain, 3,
        base::BindOnce(&DaoAgentProactiveEngine::OnEpisodesLoaded,
                       weak_factory_.GetWeakPtr(), domain));
    return;
  }

  // Check dedup: have we already shown this (url, scenario_id)?
  DedupKey key = {url, scenario->id};
  if (shown_scenarios_.count(key)) {
    return;
  }

  int tab_id = web_contents->GetPrimaryMainFrame()->GetRoutingID();

  if (scenario->requires_page_content) {
    // Run content analysis JS to check significance.
    rfh->ExecuteJavaScriptInIsolatedWorld(
        base::UTF8ToUTF16(std::string_view(kContentAnalysisScript)),
        base::BindOnce(&DaoAgentProactiveEngine::OnContentAnalysisResult,
                       weak_factory_.GetWeakPtr(), url, domain,
                       *scenario, tab_id),
        content::ISOLATED_WORLD_ID_CONTENT_END);
  } else {
    // No page content needed — check cooldown directly.
    memory_service_->GetCooldownScore(
        domain, scenario->id,
        base::BindOnce(&DaoAgentProactiveEngine::OnCooldownScoreReceived,
                       weak_factory_.GetWeakPtr(), url, *scenario, tab_id));
  }
}

void DaoAgentProactiveEngine::OnContentAnalysisResult(
    std::string url,
    std::string domain,
    const ScenarioDefinition& scenario,
    int tab_id,
    base::Value result) {
  // Check content significance.
  if (!IsContentSignificant(result)) {
    return;
  }

  // Content is significant — check cooldown.
  memory_service_->GetCooldownScore(
      domain, scenario.id,
      base::BindOnce(&DaoAgentProactiveEngine::OnCooldownScoreReceived,
                     weak_factory_.GetWeakPtr(), url, scenario, tab_id));
}

void DaoAgentProactiveEngine::OnCooldownScoreReceived(
    const std::string& url,
    const ScenarioDefinition& scenario,
    int tab_id,
    double cooldown_score) {
  if (cooldown_score >= kCooldownThreshold) {
    return;  // Suppressed by cooldown.
  }

  if (!delegate_) {
    return;
  }

  // Mark as shown for dedup. Cap size to avoid unbounded growth.
  if (shown_scenarios_.size() > 5000) {
    shown_scenarios_.clear();
  }
  shown_scenarios_.insert({url, scenario.id});

  // Build structured suggestion.
  ProactiveSuggestion suggestion;
  suggestion.text = scenario.name;
  suggestion.confidence = 1.0 - (cooldown_score / kCooldownThreshold);
  suggestion.action_type = (scenario.type == "seed")
                               ? DaoAgentActionType::kSeedScenario
                               : DaoAgentActionType::kPersonalScenario;
  suggestion.scenario_id = scenario.id;
  suggestion.scenario_name = scenario.name;
  suggestion.action_label = scenario.action_label;
  suggestion.action_prompt = scenario.action_prompt;
  suggestion.requires_page_content = scenario.requires_page_content;
  suggestion.tab_id = tab_id;

  delegate_->OnProactiveSuggestion(suggestion);
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
