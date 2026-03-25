// Copyright 2024 The Dao Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_agent_scenario_registry.h"

#include <algorithm>

#include "base/logging.h"
#include "third_party/re2/src/re2/re2.h"

namespace dao {

namespace {

bool MatchesUrlPattern(const std::string& url, const std::string& pattern) {
  return RE2::PartialMatch(url, pattern);
}

}  // namespace

DaoAgentScenarioRegistry::DaoAgentScenarioRegistry() {
  InitSeedScenarios();
}

DaoAgentScenarioRegistry::~DaoAgentScenarioRegistry() = default;

void DaoAgentScenarioRegistry::SetPersonalScenarios(
    std::vector<ScenarioDefinition> personal) {
  personal_scenarios_ = std::move(personal);
}

void DaoAgentScenarioRegistry::AddPersonalScenario(
    ScenarioDefinition scenario) {
  // Remove existing entry with the same id if present.
  RemovePersonalScenario(scenario.id);
  personal_scenarios_.push_back(std::move(scenario));
}

void DaoAgentScenarioRegistry::RemovePersonalScenario(const std::string& id) {
  personal_scenarios_.erase(
      std::remove_if(personal_scenarios_.begin(), personal_scenarios_.end(),
                     [&id](const ScenarioDefinition& s) {
                       return s.id == id;
                     }),
      personal_scenarios_.end());
}

std::optional<ScenarioDefinition> DaoAgentScenarioRegistry::Match(
    const std::string& url) const {
  // Priority 1: Seed scenarios — most specific URL pattern wins.
  const ScenarioDefinition* best_seed = nullptr;
  size_t best_pattern_len = 0;

  for (const auto& s : seed_scenarios_) {
    if (MatchesUrlPattern(url, s.url_pattern)) {
      if (s.url_pattern.size() > best_pattern_len) {
        best_seed = &s;
        best_pattern_len = s.url_pattern.size();
      }
    }
  }
  if (best_seed) {
    return *best_seed;
  }

  // Priority 2: Personal scenarios — highest acceptance rate wins.
  const ScenarioDefinition* best_personal = nullptr;
  double best_rate = -1.0;

  for (const auto& s : personal_scenarios_) {
    if (MatchesUrlPattern(url, s.url_pattern)) {
      double rate = s.times_triggered > 0
                        ? static_cast<double>(s.times_accepted) /
                              s.times_triggered
                        : 0.5;  // Neutral default for new scenarios.
      if (rate > best_rate) {
        best_personal = &s;
        best_rate = rate;
      }
    }
  }
  if (best_personal) {
    return *best_personal;
  }

  return std::nullopt;
}

void DaoAgentScenarioRegistry::InitSeedScenarios() {
  // Seed 1: GitHub PR Review
  {
    ScenarioDefinition s;
    s.id = "seed_github_pr";
    s.type = "seed";
    s.name = "Review this PR";
    s.description = "Read the PR diff and provide a code review with suggestions";
    s.url_pattern = R"(^https://github\.com/[^/]+/[^/]+/pull/\d+)";
    s.action_prompt =
        "You are a code reviewer. Review the following pull request changes.\n"
        "Provide:\n"
        "1. A brief summary of what this PR does\n"
        "2. Potential issues or bugs (if any)\n"
        "3. Suggestions for improvement\n"
        "4. Overall assessment (approve / request changes / needs discussion)\n"
        "\n"
        "PR content:\n{page_content}";
    s.action_label = "review_code";
    s.requires_page_content = true;
    seed_scenarios_.push_back(std::move(s));
  }

  // Seed 2: GitHub Issue Analysis
  {
    ScenarioDefinition s;
    s.id = "seed_github_issue";
    s.type = "seed";
    s.name = "Analyze this issue";
    s.description =
        "Summarize the issue, identify key points, and suggest next steps";
    s.url_pattern = R"(^https://github\.com/[^/]+/[^/]+/issues/\d+)";
    s.action_prompt =
        "Analyze this GitHub issue. Provide:\n"
        "1. Issue summary (1-2 sentences)\n"
        "2. Key discussion points from comments\n"
        "3. Current status and blockers\n"
        "4. Suggested next steps\n"
        "\n"
        "Issue content:\n{page_content}";
    s.action_label = "analyze_issue";
    s.requires_page_content = true;
    seed_scenarios_.push_back(std::move(s));
  }

  // Seed 3: Linear Project Progress
  {
    ScenarioDefinition s;
    s.id = "seed_linear_project";
    s.type = "seed";
    s.name = "Analyze project progress";
    s.description =
        "Summarize project status, identify blockers, and flag at-risk items";
    s.url_pattern = R"(^https://linear\.app/[^/]+/project/)";
    s.action_prompt =
        "Analyze this project board. Provide:\n"
        "1. Overall progress summary\n"
        "2. Items that appear blocked or at risk\n"
        "3. Priority recommendations\n"
        "4. Key metrics (completed/in-progress/todo counts if visible)\n"
        "\n"
        "Page content:\n{page_content}";
    s.action_label = "analyze_progress";
    s.requires_page_content = true;
    seed_scenarios_.push_back(std::move(s));
  }

  // Seed 4: Documentation — Smart Summary
  {
    ScenarioDefinition s;
    s.id = "seed_docs_summary";
    s.type = "seed";
    s.name = "Summarize this doc";
    s.description =
        "Extract key points from documentation for quick understanding";
    s.url_pattern = R"((docs\.|documentation|readme|wiki|\.md))";
    s.page_hints = R"(["documentation","API","guide","reference"])";
    s.action_prompt =
        "Summarize this documentation page. Provide:\n"
        "1. What this page is about (1 sentence)\n"
        "2. Key concepts or API methods covered (bullet points)\n"
        "3. Important caveats or gotchas\n"
        "Keep it concise — this is a quick reference, not a full read.\n"
        "\n"
        "Documentation:\n{page_content}";
    s.action_label = "summarize_doc";
    s.requires_page_content = true;
    seed_scenarios_.push_back(std::move(s));
  }

  // Seed 5: Stack Overflow / Forum — Quick Answer
  {
    ScenarioDefinition s;
    s.id = "seed_stackoverflow";
    s.type = "seed";
    s.name = "Extract the answer";
    s.description = "Find the best answer and summarize the solution";
    s.url_pattern = R"((stackoverflow\.com/questions|stackexchange\.com|discuss\.))";
    s.action_prompt =
        "This is a Q&A page. Extract:\n"
        "1. The question (1 sentence)\n"
        "2. The best/accepted answer (summarized)\n"
        "3. Any important caveats from comments\n"
        "Skip the noise — give the user the solution directly.\n"
        "\n"
        "Page content:\n{page_content}";
    s.action_label = "extract_answer";
    s.requires_page_content = true;
    seed_scenarios_.push_back(std::move(s));
  }
}

}  // namespace dao
