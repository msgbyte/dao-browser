// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_RANKER_H_
#define DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_RANKER_H_

#include <string>

#include "base/time/time.h"
#include "dao/browser/agent/dao_agent_proactive_types.h"

namespace dao {

class DaoAgentProactiveRanker {
 public:
  struct Options {
    double panel_threshold = 0.75;
    double sidebar_threshold = 0.90;
    base::TimeDelta min_domain_action_gap = base::Minutes(10);
  };

  explicit DaoAgentProactiveRanker(Options options);
  ~DaoAgentProactiveRanker();

  DaoAgentProactiveRanker(const DaoAgentProactiveRanker&) = delete;
  DaoAgentProactiveRanker& operator=(const DaoAgentProactiveRanker&) = delete;

  ProactiveDecision Rank(const ProactiveCandidate& candidate,
                         const ProactivePageSignals& signals,
                         const ProactiveFeedbackSignals& feedback,
                         base::Time now) const;

  static std::string PageContextSuppressionReason(
      const ProactivePageSignals& signals);

 private:
  Options options_;
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_AGENT_PROACTIVE_RANKER_H_
