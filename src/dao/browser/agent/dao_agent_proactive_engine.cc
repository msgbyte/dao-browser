// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_proactive_engine.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/check_op.h"
#include "base/functional/bind.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_finder.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/isolated_world_ids.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "dao/browser/agent/dao_agent_proactive_ranker.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace dao {

namespace {

constexpr base::TimeDelta kDwellDelay = base::Seconds(15);

constexpr std::array<std::string_view, 14> kSensitiveUrlKeywords = {
    "bank",     "billing", "checkout", "payment", "password",
    "security", "account", "settings", "inbox",   "mail",
    "messages", "health",  "medical",  "patient"};

constexpr std::array<std::string_view, 8> kDiffPageKeywords = {
    "/pull/",       "/compare/",     "/commit/", "/merge_requests/",
    "pull request", "merge request", "diff",     "patch"};

constexpr std::array<std::string_view, 7> kTrackingQueryParamNames = {
    "fbclid", "gclid",  "igshid", "mc_cid",
    "mc_eid", "msclkid", "vero_id"};

constexpr std::array<std::string_view, 6> kDocumentationPathSegments = {
    "docs", "documentation", "reference", "guide", "readme", "wiki"};

std::string GetRegistrableDomainFromUrl(const GURL& url) {
  return net::registry_controlled_domains::GetDomainAndRegistry(
      url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
}

template <size_t N>
bool ContainsAnyToken(std::string_view haystack,
                      const std::array<std::string_view, N>& needles) {
  for (std::string_view needle : needles) {
    if (haystack.find(needle) != std::string_view::npos) {
      return true;
    }
  }
  return false;
}

bool IsCjkLanguage(std::string_view language) {
  return base::StartsWith(language, "zh") || base::StartsWith(language, "ja") ||
         base::StartsWith(language, "ko");
}

bool LooksLikeDiffPage(const ProactivePageSignals& signals) {
  const std::string haystack = base::ToLowerASCII(
      signals.url + " " + signals.title + " " + signals.meta_description);
  return ContainsAnyToken(haystack, kDiffPageKeywords) &&
         (signals.has_code || signals.code_block_count > 0 ||
          signals.table_count > 0);
}

bool PathHasSegment(const std::string& path, std::string_view segment) {
  size_t position = 0;
  while (position < path.size()) {
    while (position < path.size() && path[position] == '/') {
      ++position;
    }
    const size_t segment_start = position;
    while (position < path.size() && path[position] != '/') {
      ++position;
    }
    if (std::string_view(path).substr(segment_start,
                                      position - segment_start) == segment) {
      return true;
    }
  }
  return false;
}

bool LooksLikeDocumentationUrl(const GURL& url) {
  if (!url.is_valid()) {
    return false;
  }
  const std::string host = base::ToLowerASCII(url.host());
  if (host == "docs" || base::StartsWith(host, "docs.")) {
    return true;
  }
  const std::string path = base::ToLowerASCII(url.path());
  for (std::string_view segment : kDocumentationPathSegments) {
    if (PathHasSegment(path, segment)) {
      return true;
    }
  }
  return false;
}

bool IsSensitivePage(const ProactivePageSignals& signals) {
  if (signals.password_input_count > 0 || signals.payment_input_count > 0) {
    return true;
  }

  const GURL page_url(signals.url);
  return IsSensitiveUrlForProactiveSuggestion(page_url, signals.domain);
}

bool IsSignificantContent(const ProactivePageSignals& signals) {
  if (signals.is_cjk) {
    return signals.char_count > 3000;
  }
  if (signals.word_count > 1500) {
    return true;
  }
  return LooksLikeDiffPage(signals);
}

bool IsExpectedUrl(const std::string& expected_url, const GURL& actual_url) {
  return actual_url.is_valid() && actual_url.spec() == expected_url;
}

bool IsExpectedUrl(const std::string& expected_url,
                   const std::string& actual_url) {
  return actual_url == expected_url;
}

std::string CanonicalizeProactiveDomain(std::string domain) {
  domain = base::ToLowerASCII(domain);
  return base::StartsWith(domain, "www.") ? domain.substr(4) : domain;
}

bool IsTrackingQueryParam(std::string_view key) {
  std::string normalized = base::ToLowerASCII(key);
  if (base::StartsWith(normalized, "utm_")) {
    return true;
  }
  std::string_view normalized_view(normalized);
  return std::find(kTrackingQueryParamNames.begin(),
                   kTrackingQueryParamNames.end(),
                   normalized_view) != kTrackingQueryParamNames.end();
}

std::string CanonicalizeProactivePageUrlForDedup(const std::string& raw_url) {
  const GURL url(raw_url);
  if (!url.is_valid()) {
    return raw_url;
  }

  std::string path = std::string(url.path());
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }

  std::vector<std::string> query_parts;
  std::string query = std::string(url.query());
  for (size_t start = 0; start <= query.size();) {
    const size_t end = query.find('&', start);
    const size_t length =
        end == std::string::npos ? query.size() - start : end - start;
    std::string part = query.substr(start, length);
    if (!part.empty()) {
      const size_t equals = part.find('=');
      std::string key =
          equals == std::string::npos ? part : part.substr(0, equals);
      if (!IsTrackingQueryParam(key)) {
        query_parts.push_back(std::move(part));
      }
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1;
  }
  std::sort(query_parts.begin(), query_parts.end());

  std::string canonical =
      base::StrCat({url.scheme(), "://",
                    CanonicalizeProactiveDomain(std::string(url.host()))});
  if (url.has_port()) {
    base::StrAppend(&canonical, {":", url.port()});
  }
  canonical += path.empty() ? "/" : path;
  if (!query_parts.empty()) {
    base::StrAppend(&canonical, {"?", base::JoinString(query_parts, "&")});
  }
  return canonical;
}

bool IsCurrentEvaluationPage(
    const std::string& expected_url,
    uint64_t expected_navigation_generation,
    uint64_t current_navigation_generation,
    const base::WeakPtr<content::WebContents>& weak_contents) {
  content::WebContents* web_contents = weak_contents.get();
  return web_contents &&
         expected_navigation_generation == current_navigation_generation &&
         IsExpectedUrl(expected_url, web_contents->GetLastCommittedURL());
}

ProactivePageSignals BuildSignalsFromAnalysisResult(const std::string& url,
                                                    const std::string& domain,
                                                    const base::Value& result) {
  ProactivePageSignals signals;
  signals.url = url;
  signals.domain = domain;

  if (result.is_dict()) {
    const auto& dict = result.GetDict();
    if (const std::string* value = dict.FindString("url")) {
      signals.url = *value;
    }
    if (const std::string* value = dict.FindString("title")) {
      signals.title = *value;
    }
    if (const std::string* value = dict.FindString("metaDescription")) {
      signals.meta_description = *value;
    }
    if (const std::string* value = dict.FindString("lang")) {
      signals.language = base::ToLowerASCII(*value);
    }
    signals.word_count = dict.FindInt("wordCount").value_or(0);
    signals.char_count = dict.FindInt("charCount").value_or(0);
    signals.has_code = dict.FindBool("hasCode").value_or(false);
    signals.code_block_count = dict.FindInt("codeBlockCount").value_or(0);
    signals.table_count = dict.FindInt("tableCount").value_or(0);
    signals.form_count = dict.FindInt("formCount").value_or(0);
    signals.button_count = dict.FindInt("buttonCount").value_or(0);
    signals.link_count = dict.FindInt("linkCount").value_or(0);
    signals.heading_count = dict.FindInt("headingCount").value_or(0);
    signals.answer_count = dict.FindInt("answerCount").value_or(0);
    signals.password_input_count =
        dict.FindInt("passwordInputCount").value_or(0);
    signals.payment_input_count = dict.FindInt("paymentInputCount").value_or(0);
    signals.is_typing = dict.FindBool("isTyping").value_or(false);
  }

  signals.is_cjk = IsCjkLanguage(signals.language);
  signals.has_significant_content = IsSignificantContent(signals);
  signals.is_sensitive = IsSensitivePage(signals);
  return signals;
}

ProactiveCandidateSource GetCandidateSource(
    const ScenarioDefinition& scenario) {
  return scenario.type == "personal"
             ? ProactiveCandidateSource::kPersonalScenario
             : ProactiveCandidateSource::kSeedScenario;
}

constexpr char kContentAnalysisScript[] = R"js(
(function() {
  var text = document.body ? document.body.innerText : '';
  var trimmed = text.trim();
  var words = trimmed ? trimmed.split(/\s+/).length : 0;
  var metaDescription =
      document.querySelector('meta[name="description"]')?.content ||
      document.querySelector('meta[property="og:description"]')?.content ||
      '';
  var codeBlocks = document.querySelectorAll(
      'pre, code, .blob-code, .diff, [class*="diff"], [class*="code"]');
  var inputs = Array.from(document.querySelectorAll('input'));
  var paymentInputCount = inputs.filter(function(input) {
    var attrs = [
      input.type,
      input.inputMode,
      input.autocomplete,
      input.name,
      input.id,
      input.placeholder,
      input.getAttribute('aria-label') || ''
    ].join(' ').toLowerCase();
    return /(card|cc-|billing|payment|cvv|cvc|expiry|exp|iban)/.test(attrs);
  }).length;
  var active = document.activeElement;
  var answers = document.querySelectorAll(
      '.answer, .answercell, [data-answerid], [itemprop="acceptedAnswer"], ' +
      '[itemprop="suggestedAnswer"], .accepted-answer, ' +
      '[class*="accepted-answer"], article[data-post-id], .topic-post, ' +
      '.post-stream article');
  var isTyping =
      !!active &&
      ((active.tagName === 'INPUT' &&
        active.type !== 'button' &&
        active.type !== 'checkbox' &&
        active.type !== 'radio' &&
        active.type !== 'submit') ||
       active.tagName === 'TEXTAREA' ||
       active.isContentEditable);
  return {
    wordCount: words,
    charCount: text.length,
    lang: document.documentElement.lang || '',
    title: document.title,
    url: location.href,
    metaDescription: metaDescription,
    hasCode: codeBlocks.length > 0,
    codeBlockCount: codeBlocks.length,
    tableCount:
        document.querySelectorAll(
            'table, [role="table"], [class*="table"], [role="grid"]').length,
    formCount: document.querySelectorAll('form, [role="form"]').length,
    buttonCount:
        document.querySelectorAll(
            'button, [role="button"], input[type="button"], input[type="submit"]').length,
    linkCount: document.querySelectorAll('a[href]').length,
    headingCount: document.querySelectorAll('h1, h2, h3, h4, h5, h6').length,
    answerCount: answers.length,
    passwordInputCount:
        document.querySelectorAll('input[type="password"]').length,
    paymentInputCount: paymentInputCount,
    isTyping: isTyping
  };
})()
)js";

bool IsSkippableUrl(const GURL& url) {
  return !url.SchemeIsHTTPOrHTTPS();
}

std::string NormalizePathForEpisodeMatch(std::string path) {
  while (path.size() > 1 && path.back() == '/') {
    path.pop_back();
  }
  return path;
}

bool DoesHostMatchEpisodeDomain(const Episode& episode, const GURL& url) {
  const std::string domain = base::ToLowerASCII(episode.domain);
  if (domain.empty()) {
    return true;
  }
  const std::string host = base::ToLowerASCII(url.host());
  return host == domain || base::EndsWith(host, "." + domain);
}

}  // namespace

bool DoesLegacyEpisodeMatchPageForProactiveSuggestion(
    const Episode& episode,
    const std::string& page_url) {
  if (episode.path_template.empty()) {
    return false;
  }
  const GURL url(page_url);
  if (!url.is_valid()) {
    return false;
  }
  if (!DoesHostMatchEpisodeDomain(episode, url)) {
    return false;
  }
  return NormalizePathForEpisodeMatch(episode.path_template) ==
         NormalizePathForEpisodeMatch(std::string(url.path()));
}

std::string GetDomainForProactiveSuggestion(const GURL& url) {
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS()) {
    return std::string();
  }

  std::string domain = GetRegistrableDomainFromUrl(url);
  if (!domain.empty()) {
    return domain;
  }
  return std::string(url.host());
}

bool IsSensitiveUrlForProactiveSuggestion(const GURL& url,
                                          const std::string& domain) {
  if (LooksLikeDocumentationUrl(url)) {
    return false;
  }

  const std::string host_and_path =
      base::ToLowerASCII(domain + " " + std::string(url.host()) + " " +
                         std::string(url.path()));
  return ContainsAnyToken(host_and_path, kSensitiveUrlKeywords);
}

struct DaoAgentProactiveEngine::PendingScenarioEvaluation {
  std::string url;
  std::string domain;
  int tab_id = -1;
  uint64_t navigation_generation = 0;
  base::Time evaluation_time;
  ProactivePageSignals signals;
  std::vector<ProactiveDecision> decisions;
  base::WeakPtr<content::WebContents> weak_contents;
  size_t pending_cooldowns = 0;
};

// Inner class: observes the currently active tab's navigation.
class DaoAgentProactiveEngine::ActiveTabObserver
    : public content::WebContentsObserver {
 public:
  ActiveTabObserver(DaoAgentProactiveEngine* engine,
                    content::WebContents* web_contents)
      : content::WebContentsObserver(web_contents), engine_(engine) {}

  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override {
    if (!navigation_handle->IsInPrimaryMainFrame() ||
        !navigation_handle->HasCommitted()) {
      return;
    }
    const GURL& url = navigation_handle->GetURL();
    if (IsSkippableUrl(url)) {
      engine_->InvalidateActivePageEvaluations();
      return;
    }
    std::string domain = GetDomainForProactiveSuggestion(url);
    engine_->OnNavigationCompleted(
        url.spec(), domain, base::UTF16ToUTF8(web_contents()->GetTitle()),
        web_contents());
  }

 private:
  raw_ptr<DaoAgentProactiveEngine> engine_;
};

DaoAgentProactiveEngine::DaoAgentProactiveEngine(
    DaoAgentMemoryService* memory_service,
    Profile* profile)
    : memory_service_(memory_service), profile_(profile) {}

DaoAgentProactiveEngine::~DaoAgentProactiveEngine() {
  Stop();
}

void DaoAgentProactiveEngine::SetDelegate(Delegate* delegate) {
  delegate_ = delegate;
}

void DaoAgentProactiveEngine::SetConfidenceThreshold(double threshold) {
  confidence_threshold_ = threshold;
}

void DaoAgentProactiveEngine::RecordShownScenarioForFeedback(
    const std::string& url,
    const std::string& domain,
    const std::string& action_label,
    const std::string& scenario_id,
    base::Time shown_time) {
  if (url.empty() || scenario_id.empty() || shown_time.is_null()) {
    return;
  }
  if (shown_scenarios_.size() > 5000) {
    shown_scenarios_.clear();
  }
  shown_scenarios_.insert(
      DedupKey{CanonicalizeProactivePageUrlForDedup(url), scenario_id});
  if (domain.empty() || action_label.empty()) {
    return;
  }
  last_domain_action_shown_[DomainActionKey{domain, action_label}] =
      shown_time;
}

bool DaoAgentProactiveEngine::HasShownScenarioForTesting(
    const std::string& url,
    const std::string& scenario_id) const {
  return shown_scenarios_.find(
             DedupKey{CanonicalizeProactivePageUrlForDedup(url),
                      scenario_id}) !=
         shown_scenarios_.end();
}

base::Time DaoAgentProactiveEngine::GetLastDomainActionShownForTesting(
    const std::string& domain,
    const std::string& action_label) const {
  auto it =
      last_domain_action_shown_.find(DomainActionKey{domain, action_label});
  if (it == last_domain_action_shown_.end()) {
    return base::Time();
  }
  return it->second;
}

void DaoAgentProactiveEngine::Start() {
  if (is_running_) {
    return;
  }
  is_running_ = true;
  BrowserList::AddObserver(this);

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
  InvalidateActivePageEvaluations();
  BrowserList::RemoveObserver(this);

  for (Browser* browser : chrome::FindAllBrowsersWithProfile(profile_)) {
    browser->tab_strip_model()->RemoveObserver(this);
  }

  active_tab_observer_.reset();
}

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

void DaoAgentProactiveEngine::OnTabStripModelChanged(
    TabStripModel* tab_strip_model,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (!selection.active_tab_changed()) {
    return;
  }

  content::WebContents* new_contents = selection.new_contents;
  if (!new_contents) {
    InvalidateActivePageEvaluations();
    active_tab_observer_.reset();
    dwell_timer_.Stop();
    return;
  }

  active_tab_observer_ =
      std::make_unique<ActiveTabObserver>(this, new_contents);
  dwell_timer_.Stop();
  InvalidateActivePageEvaluations();

  const GURL& url = new_contents->GetLastCommittedURL();
  if (!IsSkippableUrl(url)) {
    std::string domain = GetDomainForProactiveSuggestion(url);
    OnNavigationCompleted(url.spec(), domain,
                          base::UTF16ToUTF8(new_contents->GetTitle()),
                          new_contents);
  }
}

void DaoAgentProactiveEngine::InvalidateActivePageEvaluations() {
  ++active_navigation_generation_;
}

void DaoAgentProactiveEngine::OnNavigationCompleted(
    const std::string& url,
    const std::string& domain,
    const std::string& title,
    content::WebContents* web_contents) {
  dwell_timer_.Stop();
  const uint64_t navigation_generation = ++active_navigation_generation_;

  if (domain.empty() || !memory_service_) {
    return;
  }

  base::WeakPtr<content::WebContents> weak_contents =
      web_contents->GetWeakPtr();
  dwell_timer_.Start(
      FROM_HERE, kDwellDelay,
      base::BindOnce(&DaoAgentProactiveEngine::OnDwellTimerFired,
                     weak_factory_.GetWeakPtr(), url, domain,
                     navigation_generation, std::move(weak_contents)));
}

void DaoAgentProactiveEngine::OnDwellTimerFired(
    std::string url,
    std::string domain,
    uint64_t navigation_generation,
    base::WeakPtr<content::WebContents> weak_contents) {
  content::WebContents* web_contents = weak_contents.get();
  if (!web_contents) {
    return;
  }

  content::RenderFrameHost* rfh = web_contents->GetPrimaryMainFrame();
  if (!rfh || !rfh->IsRenderFrameLive()) {
    return;
  }
  if (!IsCurrentEvaluationPage(url, navigation_generation,
                               active_navigation_generation_, weak_contents)) {
    return;
  }

  std::vector<ScenarioDefinition> scenarios =
      scenario_registry_.GetMatchingScenarios(url);
  const int tab_id = sessions::SessionTabHelper::IdForTab(web_contents).id();
  rfh->ExecuteJavaScriptInIsolatedWorld(
      base::UTF8ToUTF16(std::string_view(kContentAnalysisScript)),
      base::BindOnce(&DaoAgentProactiveEngine::OnContentAnalysisResult,
                     weak_factory_.GetWeakPtr(), url, domain,
                     navigation_generation, std::move(weak_contents),
                     std::move(scenarios), tab_id),
      content::ISOLATED_WORLD_ID_CONTENT_END);
}

void DaoAgentProactiveEngine::OnContentAnalysisResult(
    std::string url,
    std::string domain,
    uint64_t navigation_generation,
    base::WeakPtr<content::WebContents> weak_contents,
    std::vector<ScenarioDefinition> scenarios,
    int tab_id,
    base::Value result) {
  if (!IsCurrentEvaluationPage(url, navigation_generation,
                               active_navigation_generation_, weak_contents)) {
    return;
  }

  auto evaluation = std::make_shared<PendingScenarioEvaluation>();
  evaluation->url = url;
  evaluation->domain = domain;
  evaluation->tab_id = tab_id;
  evaluation->navigation_generation = navigation_generation;
  evaluation->evaluation_time = base::Time::Now();
  evaluation->signals = BuildSignalsFromAnalysisResult(url, domain, result);
  evaluation->weak_contents = std::move(weak_contents);

  if (!IsExpectedUrl(url, evaluation->signals.url)) {
    return;
  }

  if (scenarios.empty()) {
    if (!DaoAgentProactiveRanker::PageContextSuppressionReason(
             evaluation->signals)
             .empty()) {
      return;
    }
    memory_service_->GetEpisodesByDomain(
        domain, 3,
        base::BindOnce(&DaoAgentProactiveEngine::OnEpisodesLoaded,
                       weak_factory_.GetWeakPtr(), url, domain,
                       navigation_generation, evaluation->weak_contents,
                       evaluation->tab_id));
    return;
  }

  for (const ScenarioDefinition& scenario : scenarios) {
    DedupKey key = {CanonicalizeProactivePageUrlForDedup(url), scenario.id};
    if (shown_scenarios_.count(key)) {
      continue;
    }

    ++evaluation->pending_cooldowns;
    memory_service_->GetCooldownScore(
        domain, scenario.id,
        base::BindOnce(
            &DaoAgentProactiveEngine::OnScenarioCooldownScoreReceived,
            weak_factory_.GetWeakPtr(), evaluation, scenario));
  }

  if (evaluation->pending_cooldowns == 0) {
    return;
  }
}

void DaoAgentProactiveEngine::OnScenarioCooldownScoreReceived(
    std::shared_ptr<PendingScenarioEvaluation> evaluation,
    const ScenarioDefinition& scenario,
    double cooldown_score) {
  if (!IsCurrentEvaluationPage(
          evaluation->url, evaluation->navigation_generation,
          active_navigation_generation_, evaluation->weak_contents)) {
    return;
  }

  DaoAgentProactiveRanker::Options options;
  options.panel_threshold = confidence_threshold_;
  DaoAgentProactiveRanker ranker(options);

  ProactiveCandidate candidate;
  candidate.source = GetCandidateSource(scenario);
  candidate.scenario = scenario;

  ProactiveFeedbackSignals feedback;
  feedback.cooldown_score = cooldown_score;
  auto shown_it = last_domain_action_shown_.find(
      {evaluation->domain, scenario.action_label});
  if (shown_it != last_domain_action_shown_.end()) {
    feedback.last_domain_action_shown = shown_it->second;
  }

  evaluation->decisions.push_back(ranker.Rank(
      candidate, evaluation->signals, feedback, evaluation->evaluation_time));

  DCHECK_GT(evaluation->pending_cooldowns, 0u);
  --evaluation->pending_cooldowns;
  if (evaluation->pending_cooldowns == 0) {
    FinalizeScenarioEvaluation(std::move(evaluation));
  }
}

void DaoAgentProactiveEngine::FinalizeScenarioEvaluation(
    std::shared_ptr<PendingScenarioEvaluation> evaluation) {
  if (!delegate_ || evaluation->decisions.empty()) {
    return;
  }
  if (!IsCurrentEvaluationPage(
          evaluation->url, evaluation->navigation_generation,
          active_navigation_generation_, evaluation->weak_contents)) {
    return;
  }

  const ProactiveDecision* best = nullptr;
  for (const ProactiveDecision& decision : evaluation->decisions) {
    if (decision.tier != ProactivePresentationTier::kAgentPanelCard) {
      continue;
    }
    if (!best || decision.score.final_score > best->score.final_score) {
      best = &decision;
    }
  }

  if (!best) {
    return;
  }

  const ScenarioDefinition& scenario = best->candidate.scenario;
  ProactiveSuggestion suggestion;
  suggestion.text = scenario.name;
  suggestion.confidence = best->score.final_score;
  suggestion.action_type =
      (best->candidate.source == ProactiveCandidateSource::kPersonalScenario)
          ? DaoAgentActionType::kPersonalScenario
          : DaoAgentActionType::kSeedScenario;
  suggestion.scenario_id = scenario.id;
  suggestion.scenario_name = scenario.name;
  suggestion.action_label = scenario.action_label;
  suggestion.action_prompt = scenario.action_prompt;
  suggestion.reason = best->reason;
  suggestion.expected_outcome = best->expected_outcome;
  suggestion.context_disclosure = best->context_disclosure;
  suggestion.suppression_reason = best->suppression_reason;
  suggestion.score_debug_json = best->score_debug_json;
  suggestion.url = evaluation->signals.url;
  suggestion.domain = evaluation->signals.domain;
  suggestion.requires_page_content = scenario.requires_page_content;
  suggestion.tab_id = evaluation->tab_id;

  delegate_->OnProactiveSuggestion(suggestion);
}

void DaoAgentProactiveEngine::OnEpisodesLoaded(
    std::string url,
    std::string domain,
    uint64_t navigation_generation,
    base::WeakPtr<content::WebContents> weak_contents,
    int tab_id,
    std::vector<Episode> episodes) {
  if (!delegate_ || episodes.empty()) {
    return;
  }
  if (!IsCurrentEvaluationPage(url, navigation_generation,
                               active_navigation_generation_, weak_contents)) {
    return;
  }

  const Episode* best = nullptr;
  std::vector<const Episode*> matching_episodes;
  for (const auto& ep : episodes) {
    if (!DoesLegacyEpisodeMatchPageForProactiveSuggestion(ep, url)) {
      continue;
    }
    matching_episodes.push_back(&ep);
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
  suggestion.url = std::move(url);
  suggestion.domain = std::move(domain);
  suggestion.tab_id = tab_id;

  if (matching_episodes.size() >= 2) {
    suggestion.type = "repeat_action";
  } else {
    suggestion.type = "continue_conversation";
    suggestion.text = best->intent;
  }

  delegate_->OnProactiveSuggestion(suggestion);
}

}  // namespace dao
