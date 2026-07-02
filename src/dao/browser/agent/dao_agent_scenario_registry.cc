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

double AcceptanceRate(const ScenarioDefinition& scenario) {
  if (scenario.times_triggered <= 0) {
    return 0.5;
  }
  return static_cast<double>(scenario.times_accepted) /
         static_cast<double>(scenario.times_triggered);
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
  std::vector<ScenarioDefinition> matches = GetMatchingScenarios(url);
  if (!matches.empty()) {
    return matches.front();
  }
  return std::nullopt;
}

std::vector<ScenarioDefinition> DaoAgentScenarioRegistry::GetMatchingScenarios(
    const std::string& url) const {
  std::vector<ScenarioDefinition> seed_matches;
  std::vector<ScenarioDefinition> personal_matches;

  for (const auto& scenario : seed_scenarios_) {
    if (MatchesUrlPattern(url, scenario.url_pattern)) {
      seed_matches.push_back(scenario);
    }
  }
  for (const auto& scenario : personal_scenarios_) {
    if (MatchesUrlPattern(url, scenario.url_pattern)) {
      personal_matches.push_back(scenario);
    }
  }

  std::stable_sort(seed_matches.begin(), seed_matches.end(),
                   [](const ScenarioDefinition& lhs,
                      const ScenarioDefinition& rhs) {
                     return lhs.url_pattern.size() > rhs.url_pattern.size();
                   });
  std::stable_sort(personal_matches.begin(), personal_matches.end(),
                   [](const ScenarioDefinition& lhs,
                      const ScenarioDefinition& rhs) {
                     return AcceptanceRate(lhs) > AcceptanceRate(rhs);
                   });

  std::vector<ScenarioDefinition> matches;
  matches.reserve(seed_matches.size() + personal_matches.size());
  matches.insert(matches.end(), seed_matches.begin(), seed_matches.end());
  matches.insert(matches.end(), personal_matches.begin(),
                 personal_matches.end());
  return matches;
}

void DaoAgentScenarioRegistry::InitSeedScenarios() {
  // Seed 1: Pull Request / Merge Request Review
  {
    ScenarioDefinition s;
    s.id = "seed_github_pr";
    s.type = "seed";
    s.name = "Review this change";
    s.description =
        "Read the PR or MR diff and provide a code review with suggestions";
    s.url_pattern =
        R"((^https://github\.com/[^/]+/[^/]+/pull/\d+|/-/merge_requests/\d+))";
    s.page_hints =
        R"(["pull request","pull requests","merge request","merge requests","code review","diff"])";
    s.action_prompt =
        "You are a code reviewer. Review the following pull request or merge "
        "request changes.\n"
        "Provide:\n"
        "1. A brief summary of what this change does\n"
        "2. Potential issues or bugs (if any)\n"
        "3. Suggestions for improvement\n"
        "4. Overall assessment (approve / request changes / needs discussion)\n"
        "\n"
        "Change content:\n{page_content}";
    s.action_label = "review_code";
    s.requires_page_content = true;
    seed_scenarios_.push_back(std::move(s));
  }

  // Seed 2: Issue Analysis
  {
    ScenarioDefinition s;
    s.id = "seed_github_issue";
    s.type = "seed";
    s.name = "Analyze this issue";
    s.description =
        "Summarize the issue, identify key points, and suggest next steps";
    s.url_pattern =
        R"((^https://github\.com/[^/]+/[^/]+/issues/\d+|/-/issues/\d+))";
    s.page_hints = R"(["issue","bug","blocked","blocker"])";
    s.action_prompt =
        "Analyze this issue. Provide:\n"
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
    s.url_pattern =
        R"((docs\.|documentation|readme|wiki|\.md|/(docs|api|reference|guide)(/|$|[?#])))";
    s.page_hints = R"(["docs","documentation","API","guide","reference"])";
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
    s.url_pattern =
        R"(^https://(([^/]+\.)?stackoverflow\.com/questions/|[^/]+\.stackexchange\.com/questions/|discuss\.[^/]+/t/))";
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
