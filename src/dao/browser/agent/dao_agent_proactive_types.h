// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_TYPES_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_TYPES_H_

#include <string>

#include "base/time/time.h"
#include "dao/browser/agent/dao_agent_memory_types.h"

namespace dao {

enum class ProactivePresentationTier {
  kSilent,
  kAgentPanelCard,
  kSidebarNudge,
};

enum class ProactiveCandidateSource {
  kSeedScenario,
  kPersonalScenario,
  kEpisodeResume,
};

struct ProactivePageSignals {
  ProactivePageSignals();
  ~ProactivePageSignals();
  ProactivePageSignals(const ProactivePageSignals&);
  ProactivePageSignals& operator=(const ProactivePageSignals&);
  ProactivePageSignals(ProactivePageSignals&&);
  ProactivePageSignals& operator=(ProactivePageSignals&&);

  std::string url;
  std::string domain;
  std::string title;
  std::string meta_description;
  std::string language;
  int word_count = 0;
  int char_count = 0;
  int code_block_count = 0;
  int table_count = 0;
  int form_count = 0;
  int button_count = 0;
  int link_count = 0;
  int heading_count = 0;
  int answer_count = 0;
  int password_input_count = 0;
  int payment_input_count = 0;
  bool has_code = false;
  bool has_significant_content = false;
  bool is_cjk = false;
  bool is_typing = false;
  bool is_sensitive = false;
};

struct ProactiveCandidate {
  ProactiveCandidate();
  ~ProactiveCandidate();
  ProactiveCandidate(const ProactiveCandidate&);
  ProactiveCandidate& operator=(const ProactiveCandidate&);
  ProactiveCandidate(ProactiveCandidate&&);
  ProactiveCandidate& operator=(ProactiveCandidate&&);

  ProactiveCandidateSource source = ProactiveCandidateSource::kSeedScenario;
  ScenarioDefinition scenario;
};

struct ProactiveFeedbackSignals {
  ProactiveFeedbackSignals();
  ~ProactiveFeedbackSignals();
  ProactiveFeedbackSignals(const ProactiveFeedbackSignals&);
  ProactiveFeedbackSignals& operator=(const ProactiveFeedbackSignals&);
  ProactiveFeedbackSignals(ProactiveFeedbackSignals&&);
  ProactiveFeedbackSignals& operator=(ProactiveFeedbackSignals&&);

  double cooldown_score = 0.0;
  base::Time last_domain_action_shown;
};

struct ProactiveScoreBreakdown {
  ProactiveScoreBreakdown();
  ~ProactiveScoreBreakdown();
  ProactiveScoreBreakdown(const ProactiveScoreBreakdown&);
  ProactiveScoreBreakdown& operator=(const ProactiveScoreBreakdown&);
  ProactiveScoreBreakdown(ProactiveScoreBreakdown&&);
  ProactiveScoreBreakdown& operator=(ProactiveScoreBreakdown&&);

  double relevance = 0.0;
  double usefulness = 0.0;
  double user_fit = 0.0;
  double timing = 0.0;
  double interruption_cost = 0.0;
  double fatigue_penalty = 0.0;
  double privacy_sensitivity = 0.0;
  double final_score = 0.0;
};

struct ProactiveDecision {
  ProactiveDecision();
  ~ProactiveDecision();
  ProactiveDecision(const ProactiveDecision&);
  ProactiveDecision& operator=(const ProactiveDecision&);
  ProactiveDecision(ProactiveDecision&&);
  ProactiveDecision& operator=(ProactiveDecision&&);

  ProactiveCandidate candidate;
  ProactiveScoreBreakdown score;
  ProactivePresentationTier tier = ProactivePresentationTier::kSilent;
  std::string reason;              // Stable WebUI i18n code.
  std::string expected_outcome;    // Stable WebUI i18n code.
  std::string context_disclosure;  // Stable WebUI i18n code.
  std::string suppression_reason;
  std::string score_debug_json;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_TYPES_H_
