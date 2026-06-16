// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_TYPES_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_TYPES_H_

#include <optional>
#include <string>
#include <vector>

#include "base/time/time.h"

namespace dao {

// Action types for proactive suggestions.
enum class DaoAgentActionType {
  kNone,               // Legacy memory-based suggestion
  kSeedScenario,       // Matched a built-in seed scenario
  kPersonalScenario,   // Matched a user-learned scenario
  kLearningProposal,   // Learning pipeline proposes a new pattern
};

// Defines a scenario (seed or personal) for proactive actions.
struct ScenarioDefinition {
  ScenarioDefinition();
  ~ScenarioDefinition();
  ScenarioDefinition(const ScenarioDefinition&);
  ScenarioDefinition& operator=(const ScenarioDefinition&);
  ScenarioDefinition(ScenarioDefinition&&);
  ScenarioDefinition& operator=(ScenarioDefinition&&);

  std::string id;
  std::string type;  // "seed" or "personal"
  std::string name;
  std::string description;
  std::string url_pattern;          // Regex for URL matching
  std::string page_hints;           // JSON array of keywords/selectors
  std::string action_prompt;        // LLM prompt template ({page_content})
  std::string action_label;         // Category: "review_code", etc.
  bool requires_page_content = true;
  int times_triggered = 0;
  int times_accepted = 0;
  int times_dismissed = 0;
  base::Time created_at;
  base::Time last_triggered_at;
};

// Records user interaction with a scenario action.
struct ActionFeedback {
  ActionFeedback();
  ~ActionFeedback();
  ActionFeedback(const ActionFeedback&);
  ActionFeedback& operator=(const ActionFeedback&);
  ActionFeedback(ActionFeedback&&);
  ActionFeedback& operator=(ActionFeedback&&);

  int64_t id = 0;
  std::string scenario_id;
  std::string action_label;
  std::string domain;
  std::string url;
  double trigger_confidence = 0.0;
  std::string outcome;  // "shown","clicked","dismissed","ignored","completed","error"
  base::Time timestamp;
  std::string session_id;
};

struct ConversationMessage {
  ConversationMessage();
  ~ConversationMessage();
  ConversationMessage(const ConversationMessage&);
  ConversationMessage& operator=(const ConversationMessage&);
  ConversationMessage(ConversationMessage&&);
  ConversationMessage& operator=(ConversationMessage&&);

  int64_t id = 0;
  std::string session_id;
  std::string role;      // "user", "assistant", "tool", "system"
  std::string content;
  base::Time timestamp;
  std::string page_url;
  std::string page_title;
};

struct ConversationSummary {
  ConversationSummary();
  ~ConversationSummary();
  ConversationSummary(const ConversationSummary&);
  ConversationSummary& operator=(const ConversationSummary&);
  ConversationSummary(ConversationSummary&&);
  ConversationSummary& operator=(ConversationSummary&&);

  int64_t id = 0;
  std::string session_id;
  std::string summary;
  int message_count = 0;
  base::Time first_timestamp;
  base::Time last_timestamp;
  std::string primary_domain;
};

struct Preference {
  Preference();
  ~Preference();
  Preference(const Preference&);
  Preference& operator=(const Preference&);
  Preference(Preference&&);
  Preference& operator=(Preference&&);

  int64_t id = 0;
  std::string key;
  std::string value;
  double confidence = 0.5;
  int evidence_count = 1;
  base::Time last_updated;
};

struct Episode {
  Episode();
  ~Episode();
  Episode(const Episode&);
  Episode& operator=(const Episode&);
  Episode(Episode&&);
  Episode& operator=(Episode&&);

  int64_t id = 0;
  std::string domain;
  std::string path_template;
  std::string url;
  std::string title;
  std::string intent;
  std::string entities;    // JSON array
  std::string tools_used;  // JSON array
  std::string outcome;
  base::Time timestamp;
  double confidence = 0.7;
  std::string user_action;   // What the user asked the agent to do
  std::string action_result; // "helpful", "not_helpful", or empty
};

struct StorageStats {
  int64_t total_size_bytes = 0;
  int conversation_count = 0;
  int summary_count = 0;
  int episode_count = 0;
  int preference_count = 0;
};

struct MemorySqlCell {
  MemorySqlCell();
  ~MemorySqlCell();
  MemorySqlCell(const MemorySqlCell&);
  MemorySqlCell& operator=(const MemorySqlCell&);
  MemorySqlCell(MemorySqlCell&&);
  MemorySqlCell& operator=(MemorySqlCell&&);

  std::string type;  // "integer", "real", "text", "blob", or "null"
  std::string value;
};

struct MemorySqlQueryResult {
  MemorySqlQueryResult();
  ~MemorySqlQueryResult();
  MemorySqlQueryResult(const MemorySqlQueryResult&);
  MemorySqlQueryResult& operator=(const MemorySqlQueryResult&);
  MemorySqlQueryResult(MemorySqlQueryResult&&);
  MemorySqlQueryResult& operator=(MemorySqlQueryResult&&);

  bool ok = false;
  std::string error;
  std::vector<std::string> columns;
  std::vector<std::vector<MemorySqlCell>> rows;
  bool truncated = false;
};

// One Dream Analysis run's archived output (see DaoDreamService).
struct DreamReport {
  DreamReport();
  ~DreamReport();
  DreamReport(const DreamReport&);
  DreamReport& operator=(const DreamReport&);
  DreamReport(DreamReport&&);
  DreamReport& operator=(DreamReport&&);

  int64_t id = 0;
  std::string dream_date;        // "YYYY-MM-DD" local dream-day attribution
  std::string report_markdown;
  std::string habit_candidates;  // JSON array (LLM "habits" output)
  std::string material_stats;    // JSON: per-source counts
  std::string status;            // "completed" | "failed"
  int attempt_count = 0;
  std::string trigger_kind;      // "nightly" | "catchup" | "manual"
  std::string debug_material_json;  // material pack when debug mode on
  base::Time viewed_at;          // null = unread
  base::Time created_at;
};

struct ProactiveSuggestion {
  ProactiveSuggestion();
  ~ProactiveSuggestion();
  ProactiveSuggestion(const ProactiveSuggestion&);
  ProactiveSuggestion& operator=(const ProactiveSuggestion&);
  ProactiveSuggestion(ProactiveSuggestion&&);
  ProactiveSuggestion& operator=(ProactiveSuggestion&&);

  int64_t episode_id = 0;
  std::string text;
  double confidence = 0.0;
  std::string type;  // "repeat_action" or "continue_conversation"

  // Scenario-based fields (populated when action_type != kNone).
  DaoAgentActionType action_type = DaoAgentActionType::kNone;
  std::string scenario_id;
  std::string scenario_name;
  std::string action_label;
  std::string action_prompt;
  bool requires_page_content = false;
  int tab_id = -1;
};

struct MemoryContext {
  MemoryContext();
  ~MemoryContext();
  MemoryContext(const MemoryContext&);
  MemoryContext& operator=(const MemoryContext&);
  MemoryContext(MemoryContext&&);
  MemoryContext& operator=(MemoryContext&&);

  std::vector<Preference> preferences;
  std::vector<Episode> episodes;
  std::optional<ConversationSummary> relevant_summary;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_TYPES_H_
