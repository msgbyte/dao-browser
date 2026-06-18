// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DAO_BROWSER_AGENT_DAO_DREAM_MATERIAL_COLLECTOR_H_
#define DAO_BROWSER_AGENT_DAO_DREAM_MATERIAL_COLLECTOR_H_

#include <string>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/values.h"

class Profile;

namespace dao {

class DaoAgentMemoryService;

// Collects the material pack for one dream run: browsing history
// (domain + title granularity), search keywords, agent conversation
// excerpts, and action-feedback stats for the window
// [window_start, window_end). All queries run in parallel; the callback
// fires on the UI thread with a Dict shaped as:
// {
//   "window": {"start": "...", "end": "..."},
//   "history": [{"domain","visit_count","titles":[..],
//                "foreground_seconds":N,"total_seconds":N,
//                "duration_level":"light|medium|deep",
//                "buckets":{"morning":N,"afternoon":N,"evening":N,"night":N}}],
//   "search_queries": ["...", ...],
//   "conversations": [{"session_id","messages":["...",...]}],
//   "feedback": [{"scenario_id","name","shown","clicked","dismissed"}],
//   "stats": {"history_domains":N,"search_queries":N,
//             "conversation_sessions":N,"feedback_scenarios":N}
// }
// PRIVACY INVARIANT: no full URL appears anywhere in the output.
class DreamMaterialCollector {
 public:
  static constexpr int kMaxDomains = 50;
  static constexpr int kMaxSearchQueries = 30;
  static constexpr int kMaxConversationSessions = 10;
  static constexpr int kMaxTitlesPerDomain = 5;
  static constexpr int kMaxTextChars = 240;

  using CollectCallback = base::OnceCallback<void(base::DictValue)>;

  DreamMaterialCollector(Profile* profile,
                         DaoAgentMemoryService* memory_service);
  ~DreamMaterialCollector();

  DreamMaterialCollector(const DreamMaterialCollector&) = delete;
  DreamMaterialCollector& operator=(const DreamMaterialCollector&) = delete;

  // Only one collection may be in flight at a time.
  void Collect(base::Time window_start,
               base::Time window_end,
               CollectCallback callback);

  // Extracts a search query from a known search-engine URL. Returns the
  // empty string when `url_spec` is not a recognized search URL. Public
  // and static for testing.
  static std::string ExtractSearchQuery(const std::string& url_spec);

 private:
  void OnHistoryResults(base::ListValue domains, base::ListValue queries);
  void OnConversationsLoaded(base::ListValue sessions);
  void OnFeedbackLoaded(base::ListValue feedback);
  void OnPartDone();

  raw_ptr<Profile> profile_;
  raw_ptr<DaoAgentMemoryService> memory_service_;

  base::Time window_start_;
  base::Time window_end_;
  CollectCallback callback_;
  base::RepeatingClosure barrier_;

  base::ListValue history_part_;
  base::ListValue search_part_;
  base::ListValue conversations_part_;
  base::ListValue feedback_part_;

  base::CancelableTaskTracker history_tracker_;
  base::WeakPtrFactory<DreamMaterialCollector> weak_factory_{this};
};

}  // namespace dao

#endif  // DAO_BROWSER_AGENT_DAO_DREAM_MATERIAL_COLLECTOR_H_
