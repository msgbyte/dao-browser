// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_proactive_ranker.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "third_party/re2/src/re2/re2.h"

namespace dao {
namespace {

constexpr std::string_view kContentDisclosureKey = "captures_after_run";
constexpr std::string_view kNoContentDisclosureKey = "no_capture_before_run";

bool MatchesUrlPattern(const ProactiveCandidate& candidate,
                       const std::string& url) {
  return !candidate.scenario.url_pattern.empty() &&
         RE2::PartialMatch(url, candidate.scenario.url_pattern);
}

bool HasLargeText(const ProactivePageSignals& signals) {
  if (signals.is_cjk) {
    return signals.char_count > 3000;
  }
  return signals.word_count > 1500;
}

bool HasScenarioEvidence(const ProactivePageSignals& signals) {
  return signals.code_block_count > 0 || signals.table_count > 0 ||
         signals.answer_count > 0 || signals.heading_count > 0 ||
         HasLargeText(signals);
}

std::vector<std::string> ParsePageHints(const std::string& page_hints) {
  std::vector<std::string> hints;
  if (page_hints.empty()) {
    return hints;
  }

  std::optional<base::Value> parsed =
      base::JSONReader::Read(page_hints, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_list()) {
    return hints;
  }

  for (const base::Value& hint : parsed->GetList()) {
    if (!hint.is_string()) {
      continue;
    }
    std::string normalized = base::ToLowerASCII(hint.GetString());
    base::TrimWhitespaceASCII(normalized, base::TRIM_ALL, &normalized);
    if (!normalized.empty()) {
      hints.push_back(std::move(normalized));
    }
  }
  return hints;
}

bool IsHintBoundary(char c) {
  return !base::IsAsciiAlphaNumeric(c);
}

bool ContainsPageHint(std::string_view haystack, const std::string& hint) {
  if (hint.empty()) {
    return false;
  }

  size_t position = haystack.find(hint);
  while (position != std::string_view::npos) {
    const bool starts_on_boundary =
        position == 0 || IsHintBoundary(haystack[position - 1]);
    const size_t end = position + hint.size();
    const bool ends_on_boundary =
        end >= haystack.size() || IsHintBoundary(haystack[end]);
    if (starts_on_boundary && ends_on_boundary) {
      return true;
    }
    position = haystack.find(hint, position + 1);
  }
  return false;
}

bool HasPageHintEvidence(const ProactiveCandidate& candidate,
                         const ProactivePageSignals& signals) {
  std::vector<std::string> hints = ParsePageHints(candidate.scenario.page_hints);
  if (hints.empty()) {
    return true;
  }

  const std::string page_identity = base::ToLowerASCII(
      base::StrCat({signals.url, " ", signals.title, " ",
                    signals.meta_description}));
  for (const std::string& hint : hints) {
    if (ContainsPageHint(page_identity, hint)) {
      return true;
    }
  }
  return false;
}

bool IsHighValueSeedAction(const std::string& action_label) {
  return action_label == "review_code" || action_label == "analyze_issue" ||
         action_label == "analyze_progress" ||
         action_label == "summarize_doc" || action_label == "extract_answer";
}

bool IsForumThreadUrl(const std::string& url) {
  const std::string normalized = base::ToLowerASCII(url);
  return base::StartsWith(normalized, "https://discuss.") ||
         base::StartsWith(normalized, "http://discuss.");
}

bool HasProjectProgressEvidence(const ProactivePageSignals& signals) {
  return signals.table_count > 0 ||
         (signals.heading_count >= 4 && signals.link_count >= 12);
}

bool HasRequiredActionEvidence(const ProactiveCandidate& candidate,
                               const ProactivePageSignals& signals) {
  const std::string& action = candidate.scenario.action_label;
  if (action == "review_code") {
    return signals.has_code || signals.code_block_count > 0;
  }
  if (action == "extract_answer") {
    if (IsForumThreadUrl(signals.url)) {
      return signals.answer_count >= 2;
    }
    return signals.answer_count > 0;
  }
  if (action == "analyze_progress") {
    return HasProjectProgressEvidence(signals);
  }
  return true;
}

bool IsPersonalCandidate(const ProactiveCandidate& candidate) {
  return candidate.source == ProactiveCandidateSource::kPersonalScenario ||
         candidate.scenario.type == "personal";
}

double AcceptanceRate(const ScenarioDefinition& scenario) {
  if (scenario.times_triggered <= 0) {
    return 0.5;
  }
  return static_cast<double>(scenario.times_accepted) /
         static_cast<double>(scenario.times_triggered);
}

double DismissRate(const ScenarioDefinition& scenario) {
  if (scenario.times_triggered <= 0) {
    return 0.0;
  }
  return static_cast<double>(scenario.times_dismissed) /
         static_cast<double>(scenario.times_triggered);
}

bool HasPoorPersonalFit(const ProactiveCandidate& candidate) {
  if (!IsPersonalCandidate(candidate) ||
      candidate.scenario.times_triggered < 3) {
    return false;
  }
  const double accept_rate = AcceptanceRate(candidate.scenario);
  const double dismiss_rate = DismissRate(candidate.scenario);
  return dismiss_rate >= 0.60 && dismiss_rate > accept_rate;
}

std::string BuildExpectedOutcomeCode(const ProactiveCandidate& candidate) {
  const std::string& action = candidate.scenario.action_label;
  if (action == "review_code") {
    return "review_code";
  }
  if (action == "analyze_issue") {
    return "analyze_issue";
  }
  if (action == "analyze_progress") {
    return "analyze_progress";
  }
  if (action == "summarize_doc") {
    return "summarize_doc";
  }
  if (action == "extract_answer") {
    return "extract_answer";
  }
  return "default";
}

std::string BuildReasonCode(const ProactiveCandidate& candidate,
                            const ProactivePageSignals& signals,
                            const std::string& suppression_reason) {
  if (suppression_reason == "sensitive_page") {
    return "sensitive_page";
  }
  if (suppression_reason == "fatigue") {
    return "fatigue";
  }
  if (suppression_reason == "typing") {
    return "typing";
  }
  if (suppression_reason == "missing_action_evidence") {
    return "missing_action_evidence";
  }
  if (suppression_reason == "missing_page_hint") {
    return "missing_page_hint";
  }
  if (suppression_reason == "poor_personal_fit") {
    return "poor_personal_fit";
  }
  if (candidate.scenario.requires_page_content &&
      !signals.has_significant_content) {
    return "insufficient_content";
  }
  if (signals.code_block_count > 0 || signals.table_count > 0 ||
      signals.answer_count > 0) {
    return "structured_content";
  }
  return "matched_structure";
}

std::string BuildScoreDebugJson(const ProactiveScoreBreakdown& score,
                                const std::string& suppression_reason) {
  base::DictValue root;
  root.Set("relevance", score.relevance);
  root.Set("usefulness", score.usefulness);
  root.Set("user_fit", score.user_fit);
  root.Set("timing", score.timing);
  root.Set("interruption_cost", score.interruption_cost);
  root.Set("fatigue_penalty", score.fatigue_penalty);
  root.Set("privacy_sensitivity", score.privacy_sensitivity);
  root.Set("final_score", score.final_score);
  root.Set("suppression_reason", suppression_reason);

  std::string json;
  base::JSONWriter::Write(root, &json);
  return json;
}

}  // namespace

DaoAgentProactiveRanker::DaoAgentProactiveRanker(Options options)
    : options_(std::move(options)) {}

DaoAgentProactiveRanker::~DaoAgentProactiveRanker() = default;

// static
std::string DaoAgentProactiveRanker::PageContextSuppressionReason(
    const ProactivePageSignals& signals) {
  if (signals.is_sensitive) {
    return "sensitive_page";
  }
  if (signals.is_typing) {
    return "typing";
  }
  return std::string();
}

ProactivePageSignals::ProactivePageSignals() = default;
ProactivePageSignals::~ProactivePageSignals() = default;
ProactivePageSignals::ProactivePageSignals(const ProactivePageSignals&) =
    default;
ProactivePageSignals& ProactivePageSignals::operator=(
    const ProactivePageSignals&) = default;
ProactivePageSignals::ProactivePageSignals(ProactivePageSignals&&) = default;
ProactivePageSignals& ProactivePageSignals::operator=(ProactivePageSignals&&) =
    default;

ProactiveCandidate::ProactiveCandidate() = default;
ProactiveCandidate::~ProactiveCandidate() = default;
ProactiveCandidate::ProactiveCandidate(const ProactiveCandidate&) = default;
ProactiveCandidate& ProactiveCandidate::operator=(const ProactiveCandidate&) =
    default;
ProactiveCandidate::ProactiveCandidate(ProactiveCandidate&&) = default;
ProactiveCandidate& ProactiveCandidate::operator=(ProactiveCandidate&&) =
    default;

ProactiveFeedbackSignals::ProactiveFeedbackSignals() = default;
ProactiveFeedbackSignals::~ProactiveFeedbackSignals() = default;
ProactiveFeedbackSignals::ProactiveFeedbackSignals(
    const ProactiveFeedbackSignals&) = default;
ProactiveFeedbackSignals& ProactiveFeedbackSignals::operator=(
    const ProactiveFeedbackSignals&) = default;
ProactiveFeedbackSignals::ProactiveFeedbackSignals(ProactiveFeedbackSignals&&) =
    default;
ProactiveFeedbackSignals& ProactiveFeedbackSignals::operator=(
    ProactiveFeedbackSignals&&) = default;

ProactiveScoreBreakdown::ProactiveScoreBreakdown() = default;
ProactiveScoreBreakdown::~ProactiveScoreBreakdown() = default;
ProactiveScoreBreakdown::ProactiveScoreBreakdown(
    const ProactiveScoreBreakdown&) = default;
ProactiveScoreBreakdown& ProactiveScoreBreakdown::operator=(
    const ProactiveScoreBreakdown&) = default;
ProactiveScoreBreakdown::ProactiveScoreBreakdown(ProactiveScoreBreakdown&&) =
    default;
ProactiveScoreBreakdown& ProactiveScoreBreakdown::operator=(
    ProactiveScoreBreakdown&&) = default;

ProactiveDecision::ProactiveDecision() = default;
ProactiveDecision::~ProactiveDecision() = default;
ProactiveDecision::ProactiveDecision(const ProactiveDecision&) = default;
ProactiveDecision& ProactiveDecision::operator=(const ProactiveDecision&) =
    default;
ProactiveDecision::ProactiveDecision(ProactiveDecision&&) = default;
ProactiveDecision& ProactiveDecision::operator=(ProactiveDecision&&) = default;

ProactiveDecision DaoAgentProactiveRanker::Rank(
    const ProactiveCandidate& candidate,
    const ProactivePageSignals& signals,
    const ProactiveFeedbackSignals& feedback,
    base::Time now) const {
  ProactiveDecision decision;
  decision.candidate = candidate;
  const bool has_required_action_evidence =
      HasRequiredActionEvidence(candidate, signals);
  const bool has_page_hint_evidence = HasPageHintEvidence(candidate, signals);
  const bool has_poor_personal_fit = HasPoorPersonalFit(candidate);

  if (MatchesUrlPattern(candidate, signals.url)) {
    decision.score.relevance += 0.35;
  }
  if (candidate.scenario.requires_page_content) {
    if (signals.has_significant_content) {
      decision.score.relevance += 0.20;
    } else {
      decision.score.relevance -= 0.35;
    }
  }
  if (HasScenarioEvidence(signals)) {
    decision.score.relevance += 0.15;
  }
  if (!has_page_hint_evidence) {
    decision.score.relevance -= 0.40;
  } else if (!candidate.scenario.page_hints.empty()) {
    decision.score.relevance += 0.10;
  }

  if (candidate.source == ProactiveCandidateSource::kSeedScenario &&
      IsHighValueSeedAction(candidate.scenario.action_label)) {
    decision.score.usefulness += 0.20;
  }
  if (HasLargeText(signals)) {
    decision.score.usefulness += 0.10;
  }
  if (signals.code_block_count > 0 || signals.table_count > 0 ||
      signals.answer_count > 0) {
    decision.score.usefulness += 0.10;
  }
  if (!has_required_action_evidence) {
    decision.score.usefulness -= 0.45;
  }

  if (IsPersonalCandidate(candidate)) {
    const double accept_rate = AcceptanceRate(candidate.scenario);
    if (has_poor_personal_fit) {
      decision.score.user_fit -= 0.35;
    } else if (candidate.scenario.times_triggered <= 0) {
      decision.score.user_fit += 0.10;
    } else if (accept_rate >= 0.60) {
      decision.score.user_fit += 0.20;
    }
    if (candidate.scenario.times_triggered >= 3 &&
        DismissRate(candidate.scenario) > accept_rate) {
      decision.score.user_fit -= 0.10;
    }
  }

  decision.score.timing += 0.10;
  if (signals.is_typing) {
    decision.score.timing -= 0.30;
    decision.score.interruption_cost += 1.0;
  }

  if (!feedback.last_domain_action_shown.is_null() &&
      now >= feedback.last_domain_action_shown &&
      now - feedback.last_domain_action_shown <
          options_.min_domain_action_gap) {
    decision.score.interruption_cost += 0.20;
  }
  if (signals.form_count > 0 || signals.button_count > 20) {
    decision.score.interruption_cost += 0.10;
  }

  decision.score.fatigue_penalty =
      std::min(0.45, feedback.cooldown_score * 0.15);
  decision.score.privacy_sensitivity = signals.is_sensitive ? 1.0 : 0.0;

  decision.suppression_reason = PageContextSuppressionReason(signals);
  if (decision.suppression_reason.empty() && !has_required_action_evidence) {
    decision.suppression_reason = "missing_action_evidence";
  } else if (decision.suppression_reason.empty() && !has_page_hint_evidence) {
    decision.suppression_reason = "missing_page_hint";
  } else if (decision.suppression_reason.empty() && has_poor_personal_fit) {
    decision.suppression_reason = "poor_personal_fit";
  } else if (decision.suppression_reason.empty() &&
             feedback.cooldown_score >= 3.0) {
    decision.suppression_reason = "fatigue";
  }

  decision.score.final_score = std::clamp(
      decision.score.relevance + decision.score.usefulness +
          decision.score.user_fit + decision.score.timing -
          decision.score.interruption_cost - decision.score.fatigue_penalty -
          decision.score.privacy_sensitivity,
      0.0, 1.0);

  decision.reason =
      BuildReasonCode(candidate, signals, decision.suppression_reason);
  decision.expected_outcome = BuildExpectedOutcomeCode(candidate);
  decision.context_disclosure = candidate.scenario.requires_page_content
                                    ? std::string(kContentDisclosureKey)
                                    : std::string(kNoContentDisclosureKey);
  decision.score_debug_json =
      BuildScoreDebugJson(decision.score, decision.suppression_reason);

  if (decision.suppression_reason.empty() &&
      decision.score.final_score >= options_.panel_threshold) {
    decision.tier = ProactivePresentationTier::kAgentPanelCard;
  }

  return decision;
}

}  // namespace dao
