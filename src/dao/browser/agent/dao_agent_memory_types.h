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
};

struct StorageStats {
  int64_t total_size_bytes = 0;
  int conversation_count = 0;
  int summary_count = 0;
  int episode_count = 0;
  int preference_count = 0;
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
  std::vector<ConversationMessage> recent_messages;
  std::optional<ConversationSummary> relevant_summary;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_MEMORY_TYPES_H_
