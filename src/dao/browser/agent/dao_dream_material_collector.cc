// Copyright 2026 Dao Browser Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dao/browser/agent/dao_dream_material_collector.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "base/barrier_closure.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "dao/browser/agent/dao_agent_memory_service.h"
#include "net/base/url_util.h"
#include "url/gurl.h"

namespace dao {

namespace {

// Search engines whose query parameter we extract. The URL itself is
// discarded after extraction (privacy invariant: URLs never leave C++).
struct SearchEngine {
  const char* host_suffix;
  const char* query_param;
};
constexpr SearchEngine kSearchEngines[] = {
    {"google.com", "q"},     {"bing.com", "q"},
    {"duckduckgo.com", "q"}, {"kagi.com", "q"},
    {"baidu.com", "wd"},     {"search.brave.com", "q"},
};
constexpr int kMediumForegroundSeconds = 5 * 60;
constexpr int kDeepForegroundSeconds = 30 * 60;

// Local-time bucket for a visit: morning 06-12, afternoon 12-18,
// evening 18-22, night 22-06.
const char* BucketFor(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  if (e.hour >= 6 && e.hour < 12) {
    return "morning";
  }
  if (e.hour >= 12 && e.hour < 18) {
    return "afternoon";
  }
  if (e.hour >= 18 && e.hour < 22) {
    return "evening";
  }
  return "night";
}

// "YYYY-MM-DD HH:MM" in local time, for the window labels in the pack.
std::string FormatLocalYmdHm(base::Time t) {
  base::Time::Exploded e;
  t.LocalExplode(&e);
  return base::StringPrintf("%04d-%02d-%02d %02d:%02d", e.year, e.month,
                            e.day_of_month, e.hour, e.minute);
}

struct DomainAgg {
  int visit_count = 0;
  int foreground_seconds = 0;
  int total_seconds = 0;
  std::vector<std::string> titles;
  std::map<std::string, int> buckets;
};

int MaterialSeconds(base::TimeDelta duration) {
  if (duration <= base::Seconds(0)) {
    return 0;
  }
  return static_cast<int>(
      std::min<int64_t>(duration.InSeconds(), std::numeric_limits<int>::max()));
}

base::TimeDelta ForegroundDurationFor(const history::AnnotatedVisit& visit) {
  const base::TimeDelta foreground =
      visit.context_annotations.total_foreground_duration;
  if (foreground >= base::Seconds(0)) {
    return foreground;
  }
  if (visit.visit_row.visit_duration > base::Seconds(0)) {
    return visit.visit_row.visit_duration;
  }
  return base::Seconds(0);
}

base::TimeDelta TotalDurationFor(const history::AnnotatedVisit& visit,
                                 base::TimeDelta foreground) {
  if (visit.visit_row.visit_duration > foreground) {
    return visit.visit_row.visit_duration;
  }
  return foreground;
}

const char* DurationLevelFor(int foreground_seconds) {
  if (foreground_seconds >= kDeepForegroundSeconds) {
    return "deep";
  }
  if (foreground_seconds >= kMediumForegroundSeconds) {
    return "medium";
  }
  return "light";
}

std::string TruncateMaterialText(const std::string& text) {
  std::u16string utf16 = base::UTF8ToUTF16(text);
  if (utf16.size() <=
      static_cast<size_t>(DreamMaterialCollector::kMaxTextChars)) {
    return text;
  }
  utf16.resize(DreamMaterialCollector::kMaxTextChars);
  return base::UTF16ToUTF8(utf16);
}

}  // namespace

DreamMaterialCollector::DreamMaterialCollector(
    Profile* profile,
    DaoAgentMemoryService* memory_service)
    : profile_(profile), memory_service_(memory_service) {}

DreamMaterialCollector::~DreamMaterialCollector() = default;

// static
std::string DreamMaterialCollector::ExtractSearchQuery(
    const std::string& url_spec) {
  GURL url(url_spec);
  if (!url.is_valid() || !url.has_host()) {
    return std::string();
  }
  const std::string host(url.host());
  for (const auto& engine : kSearchEngines) {
    const std::string suffix(engine.host_suffix);
    const bool match =
        host == suffix ||
        (host.size() > suffix.size() + 1 &&
         host.compare(host.size() - suffix.size() - 1, std::string::npos,
                      "." + suffix) == 0);
    if (!match) {
      continue;
    }
    std::string query;
    if (net::GetValueForKeyInQuery(url, engine.query_param, &query) &&
        !query.empty()) {
      return query;
    }
    return std::string();
  }
  return std::string();
}

void DreamMaterialCollector::Collect(base::Time window_start,
                                     base::Time window_end,
                                     CollectCallback callback) {
  DCHECK(callback_.is_null()) << "Collection already in flight";
  window_start_ = window_start;
  window_end_ = window_end;
  callback_ = std::move(callback);
  history_part_.clear();
  search_part_.clear();
  conversations_part_.clear();
  feedback_part_.clear();

  // 3 parts: history (+search, same query), conversations, feedback.
  barrier_ = base::BarrierClosure(
      3, base::BindOnce(&DreamMaterialCollector::OnPartDone,
                        weak_factory_.GetWeakPtr()));

  // Part 1: history → domains + search queries.
  history::HistoryService* history = HistoryServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!history) {
    barrier_.Run();
  } else {
    history::QueryOptions options;
    options.begin_time = window_start;
    options.end_time = window_end;
    options.max_count = 0;  // everything in range
    options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
    history->GetAnnotatedVisits(
        options, /*compute_redirect_chain_start_properties=*/false,
        /*get_unclustered_visits_only=*/false,
        base::BindOnce(
            [](base::WeakPtr<DreamMaterialCollector> self,
               std::vector<history::AnnotatedVisit> visits) {
              if (!self) {
                return;
              }
              std::map<std::string, DomainAgg> by_domain;
              std::vector<std::string> queries;
              std::set<std::string> seen_queries;
              for (const auto& visit : visits) {
                const GURL& url = visit.url_row.url();
                // Search-query extraction first (uses URL, then drops it).
                std::string q = ExtractSearchQuery(url.spec());
                if (!q.empty() && seen_queries.insert(q).second &&
                    queries.size() <
                        static_cast<size_t>(kMaxSearchQueries)) {
                  queries.push_back(TruncateMaterialText(q));
                }
                // Domain aggregation — only domain + title survive.
                const std::string domain(url.host());
                if (domain.empty()) {
                  continue;
                }
                DomainAgg& agg = by_domain[domain];
                agg.visit_count++;
                const base::TimeDelta foreground =
                    ForegroundDurationFor(visit);
                agg.foreground_seconds += MaterialSeconds(foreground);
                agg.total_seconds +=
                    MaterialSeconds(TotalDurationFor(visit, foreground));
                agg.buckets[BucketFor(visit.visit_row.visit_time)]++;
                const std::string title =
                    TruncateMaterialText(
                        base::UTF16ToUTF8(visit.url_row.title()));
                if (!title.empty() &&
                    agg.titles.size() <
                        static_cast<size_t>(kMaxTitlesPerDomain) &&
                    std::find(agg.titles.begin(), agg.titles.end(),
                              title) == agg.titles.end()) {
                  agg.titles.push_back(title);
                }
              }
              // Top-N domains by foreground attention, then visit count.
              std::vector<std::pair<std::string, DomainAgg>> sorted(
                  std::make_move_iterator(by_domain.begin()),
                  std::make_move_iterator(by_domain.end()));
              std::sort(sorted.begin(), sorted.end(),
                        [](const auto& a, const auto& b) {
                          if (a.second.foreground_seconds !=
                              b.second.foreground_seconds) {
                            return a.second.foreground_seconds >
                                   b.second.foreground_seconds;
                          }
                          if (a.second.visit_count != b.second.visit_count) {
                            return a.second.visit_count >
                                   b.second.visit_count;
                          }
                          return a.first < b.first;
                        });
              if (sorted.size() > static_cast<size_t>(kMaxDomains)) {
                sorted.resize(kMaxDomains);
              }
              base::ListValue domains;
              for (auto& [domain, agg] : sorted) {
                base::DictValue d;
                d.Set("domain", domain);
                d.Set("visit_count", agg.visit_count);
                d.Set("foreground_seconds", agg.foreground_seconds);
                d.Set("total_seconds", agg.total_seconds);
                d.Set("duration_level",
                      DurationLevelFor(agg.foreground_seconds));
                base::ListValue titles;
                for (auto& t : agg.titles) {
                  titles.Append(t);
                }
                d.Set("titles", std::move(titles));
                base::DictValue buckets;
                for (auto& [name, count] : agg.buckets) {
                  buckets.Set(name, count);
                }
                d.Set("buckets", std::move(buckets));
                domains.Append(std::move(d));
              }
              base::ListValue query_list;
              for (auto& q : queries) {
                query_list.Append(q);
              }
              self->OnHistoryResults(std::move(domains),
                                     std::move(query_list));
            },
            weak_factory_.GetWeakPtr()),
        &history_tracker_);
  }

  // Part 2: agent conversations in window. The user's questions carry the
  // intent; keep the first 2 user messages per session.
  memory_service_->LoadRecentMessages(
      500,
      base::BindOnce(
          [](base::WeakPtr<DreamMaterialCollector> self,
             std::vector<ConversationMessage> messages) {
            if (!self) {
              return;
            }
            std::map<std::string, std::vector<std::string>> by_session;
            std::vector<std::string> session_order;
            for (const auto& msg : messages) {
              if (msg.role != "user") {
                continue;
              }
              if (msg.timestamp < self->window_start_ ||
                  msg.timestamp >= self->window_end_) {
                continue;
              }
              auto it = by_session.find(msg.session_id);
              if (it == by_session.end()) {
                if (by_session.size() >=
                    static_cast<size_t>(kMaxConversationSessions)) {
                  continue;
                }
                session_order.push_back(msg.session_id);
                it = by_session
                         .emplace(msg.session_id,
                                  std::vector<std::string>())
                         .first;
              }
              if (it->second.size() < 2) {
                it->second.push_back(TruncateMaterialText(msg.content));
              }
            }
            base::ListValue sessions;
            for (const auto& sid : session_order) {
              base::DictValue s;
              s.Set("session_id", sid);
              base::ListValue msgs;
              for (const auto& m : by_session[sid]) {
                msgs.Append(m);
              }
              s.Set("messages", std::move(msgs));
              sessions.Append(std::move(s));
            }
            self->OnConversationsLoaded(std::move(sessions));
          },
          weak_factory_.GetWeakPtr()));

  // Part 3: proactive-action feedback. Scenario stat columns already
  // aggregate lifetime counts; v1 surfaces those coarse signals.
  memory_service_->GetPersonalScenarios(base::BindOnce(
      [](base::WeakPtr<DreamMaterialCollector> self,
         std::vector<ScenarioDefinition> scenarios) {
        if (!self) {
          return;
        }
        base::ListValue feedback;
        for (const auto& s : scenarios) {
          if (s.times_triggered == 0) {
            continue;
          }
          base::DictValue f;
          f.Set("scenario_id", s.id);
          f.Set("name", TruncateMaterialText(s.name));
          f.Set("shown", s.times_triggered);
          f.Set("clicked", s.times_accepted);
          f.Set("dismissed", s.times_dismissed);
          feedback.Append(std::move(f));
        }
        self->OnFeedbackLoaded(std::move(feedback));
      },
      weak_factory_.GetWeakPtr()));
}

void DreamMaterialCollector::OnHistoryResults(base::ListValue domains,
                                              base::ListValue queries) {
  history_part_ = std::move(domains);
  search_part_ = std::move(queries);
  barrier_.Run();
}

void DreamMaterialCollector::OnConversationsLoaded(
    base::ListValue sessions) {
  conversations_part_ = std::move(sessions);
  barrier_.Run();
}

void DreamMaterialCollector::OnFeedbackLoaded(base::ListValue feedback) {
  feedback_part_ = std::move(feedback);
  barrier_.Run();
}

void DreamMaterialCollector::OnPartDone() {
  base::DictValue pack;
  base::DictValue window;
  window.Set("start", FormatLocalYmdHm(window_start_));
  window.Set("end", FormatLocalYmdHm(window_end_));
  pack.Set("window", std::move(window));
  base::DictValue stats;
  stats.Set("history_domains", static_cast<int>(history_part_.size()));
  stats.Set("search_queries", static_cast<int>(search_part_.size()));
  stats.Set("conversation_sessions",
            static_cast<int>(conversations_part_.size()));
  stats.Set("feedback_scenarios", static_cast<int>(feedback_part_.size()));
  pack.Set("stats", std::move(stats));
  pack.Set("history", std::move(history_part_));
  pack.Set("search_queries", std::move(search_part_));
  pack.Set("conversations", std::move(conversations_part_));
  pack.Set("feedback", std::move(feedback_part_));
  std::move(callback_).Run(std::move(pack));
}

}  // namespace dao
