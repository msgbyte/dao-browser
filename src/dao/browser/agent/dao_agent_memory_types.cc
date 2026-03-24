// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_memory_types.h"

namespace dao {

ConversationMessage::ConversationMessage() = default;
ConversationMessage::~ConversationMessage() = default;
ConversationMessage::ConversationMessage(const ConversationMessage&) = default;
ConversationMessage& ConversationMessage::operator=(
    const ConversationMessage&) = default;
ConversationMessage::ConversationMessage(ConversationMessage&&) = default;
ConversationMessage& ConversationMessage::operator=(ConversationMessage&&) =
    default;

ConversationSummary::ConversationSummary() = default;
ConversationSummary::~ConversationSummary() = default;
ConversationSummary::ConversationSummary(const ConversationSummary&) = default;
ConversationSummary& ConversationSummary::operator=(
    const ConversationSummary&) = default;
ConversationSummary::ConversationSummary(ConversationSummary&&) = default;
ConversationSummary& ConversationSummary::operator=(ConversationSummary&&) =
    default;

Preference::Preference() = default;
Preference::~Preference() = default;
Preference::Preference(const Preference&) = default;
Preference& Preference::operator=(const Preference&) = default;
Preference::Preference(Preference&&) = default;
Preference& Preference::operator=(Preference&&) = default;

Episode::Episode() = default;
Episode::~Episode() = default;
Episode::Episode(const Episode&) = default;
Episode& Episode::operator=(const Episode&) = default;
Episode::Episode(Episode&&) = default;
Episode& Episode::operator=(Episode&&) = default;

ProactiveSuggestion::ProactiveSuggestion() = default;
ProactiveSuggestion::~ProactiveSuggestion() = default;
ProactiveSuggestion::ProactiveSuggestion(const ProactiveSuggestion&) = default;
ProactiveSuggestion& ProactiveSuggestion::operator=(
    const ProactiveSuggestion&) = default;
ProactiveSuggestion::ProactiveSuggestion(ProactiveSuggestion&&) = default;
ProactiveSuggestion& ProactiveSuggestion::operator=(ProactiveSuggestion&&) =
    default;

MemoryContext::MemoryContext() = default;
MemoryContext::~MemoryContext() = default;
MemoryContext::MemoryContext(const MemoryContext&) = default;
MemoryContext& MemoryContext::operator=(const MemoryContext&) = default;
MemoryContext::MemoryContext(MemoryContext&&) = default;
MemoryContext& MemoryContext::operator=(MemoryContext&&) = default;

}  // namespace dao
